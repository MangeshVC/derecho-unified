#include <iostream>
#include <string>
#include <cstdlib>
#include <map>
#include <cstdint>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>

using namespace std;

#include "rdmc/rdmc.h"
#include "rdmc/util.h"

#include "derecho/derecho.h"
#include "block_size.h"

unique_ptr<rdmc::barrier_group> universal_barrier_group;

vector<uint64_t> start_times;
vector<uint64_t> end_times[32];

void query_node_info(derecho::node_id_t &node_id, derecho::ip_addr &node_ip, derecho::ip_addr &leader_ip) {
    cout << "Please enter this node's ID: ";
    cin >> node_id;
    cout << "Please enter this node's IP address: ";
    cin >> node_ip;
    cout << "Please enter the leader node's IP address: ";
    cin >> leader_ip;
}

int main(int argc, char *argv[]) {
    try {
        if(argc < 2) {
            cout << "Error: Expected number of nodes in experiment as the first argument."
                 << endl;
            return -1;
        }
        if(argc < 3) {
            cout << "Error: Expected message size as second argument."
                 << endl;
            return -1;
        }
        uint32_t num_nodes = std::atoi(argv[1]);
        unsigned int msg_size = std::atoi(argv[2]);
        derecho::node_id_t node_id;
        derecho::ip_addr my_ip;
        derecho::node_id_t leader_id = 0;
        derecho::ip_addr leader_ip;

        query_node_info(node_id, my_ip, leader_ip);

        long long unsigned int max_msg_size = msg_size;
        long long unsigned int block_size = msg_size * 2;
	unsigned int window_size = 3;

        int num_messages = 1000;

        bool done = false;
        auto stability_callback = [&num_messages, &done, &num_nodes](
            int sender_rank, long long int index, char *buf,
            long long int msg_size) {
            // cout << buf << endl;
            cout << "Delivered a message" << endl;
            DERECHO_LOG(sender_rank, index, "complete_send");
            end_times[sender_rank].push_back(get_time());
            if(index == num_messages - 1 && sender_rank == (int)num_nodes - 1) {
                done = true;
            }
        };

        std::this_thread::sleep_for(std::chrono::milliseconds{10 * node_id});

        derecho::CallbackSet callbacks{stability_callback, nullptr};
        derecho::DerechoParams param_object{max_msg_size, block_size, std::string(), window_size};
        rpc::Dispatcher<> empty_dispatcher(node_id);
        std::unique_ptr<derecho::Group<rpc::Dispatcher<>>> managed_group;

        if(node_id == leader_id) {
            managed_group = std::make_unique<derecho::Group<rpc::Dispatcher<>>>(
                my_ip, std::move(empty_dispatcher), callbacks, param_object);
        } else {
            managed_group = std::make_unique<derecho::Group<rpc::Dispatcher<>>>(
                node_id, my_ip, leader_id, leader_ip, std::move(empty_dispatcher), callbacks);
        }

        while(managed_group->get_members().size() < num_nodes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        int my_rank = 0;
        auto group_members = managed_group->get_members();
        while(group_members[my_rank] != node_id) my_rank++;

        vector<uint32_t> members;
        for(uint32_t i = 0; i < num_nodes; i++) members.push_back(i);
        universal_barrier_group = std::make_unique<rdmc::barrier_group>(members);

        universal_barrier_group->barrier_wait();
        uint64_t t1 = get_time();
        universal_barrier_group->barrier_wait();
        uint64_t t2 = get_time();
        reset_epoch();
        universal_barrier_group->barrier_wait();
        uint64_t t3 = get_time();
        printf(
            "Synchronized clocks.\nTotal possible variation = %5.3f us\n"
            "Max possible variation from local = %5.3f us\n",
            (t3 - t1) * 1e-3f, max(t2 - t1, t3 - t2) * 1e-3f);
        fflush(stdout);

        cout << "Finished constructing/joining ManagedGroup" << endl;

        if(node_id == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        for(int i = 0; i < num_messages; ++i) {
            char *buf = managed_group->get_sendbuffer_ptr(msg_size);
            while(!buf) {
                buf = managed_group->get_sendbuffer_ptr(msg_size);
            }
            for(unsigned int j = 0; j < msg_size-1; ++j) {
                buf[j] = 'a' + (i % 26);
            }
	    buf[msg_size-1] = 0;
            start_times.push_back(get_time());
            DERECHO_LOG(my_rank, i, "start_send");
            managed_group->send();

            if(node_id == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        while(!done) {
        }

        managed_group->barrier_sync();

        flush_events();
        // for(int i = 100; i < num_messages - 100; i+= 5){
        // 	printf("%5.3f\n", (end_times[my_rank][i] - start_times[i]) * 1e-3);
        // }
        exit(0);

        managed_group->leave();

    } catch(const std::exception &e) {
        cout << "Main got an exception: " << e.what() << endl;
        throw e;
    }

    cout << "Finished destroying managed_group" << endl;
}
