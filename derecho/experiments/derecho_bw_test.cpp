#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <time.h>
#include <vector>

#include "derecho/derecho.h"
#include "block_size.h"
#include "aggregate_bandwidth.h"
#include "block_size.h"
#include "log_results.h"
#include "rdmc/rdmc.h"
#include "rdmc/util.h"

using namespace std;
using namespace derecho;

unique_ptr<rdmc::barrier_group> universal_barrier_group;

int main(int argc, char *argv[]) {
    srand(time(NULL));

    uint32_t server_rank = 0;
    uint32_t node_rank;
    uint32_t num_nodes;

    map<uint32_t, std::string> node_addresses;

    rdmc::query_addresses(node_addresses, node_rank);
    num_nodes = node_addresses.size();

    vector<uint32_t> members(num_nodes);
    for(uint32_t i = 0; i < num_nodes; ++i) {
        members[i] = i;
    }

    long long unsigned int max_msg_size = atoll(argv[1]);
    long long unsigned int block_size = get_block_size(max_msg_size);
    int num_senders_selector = atoi(argv[2]);
    int num_messages = 1000;
    max_msg_size -= 16;

    bool done = false;
    auto stability_callback = [
        &num_messages,
        &done,
        &num_nodes,
        num_senders_selector,
        num_last_received = 0u
    ](int sender_id, long long int index, char *buf,
      long long int msg_size) mutable {
        // cout << "In stability callback; sender = " << sender_id
        //      << ", index = " << index << endl;
        // DERECHO_LOG(sender_id, index, "stability_callback");
        if(num_senders_selector == 0) {
            if(index == num_messages - 1 && sender_id == (int)num_nodes - 1) {
	      done = true;
            }
        } else if(num_senders_selector == 1) {
            if(index == num_messages - 1) {
                ++num_last_received;
            }
            if(num_last_received == num_nodes / 2) {
                done = true;
            }
        } else {
            if(index == num_messages - 1) {
                done = true;
            }
        }
    };

    unsigned int window_size = 6;
    rpc::Dispatcher<> empty_dispatcher(node_rank);
    std::unique_ptr<derecho::Group<rpc::Dispatcher<>>> managed_group;
    if(node_rank == server_rank) {
        managed_group = std::make_unique<derecho::Group<rpc::Dispatcher<>>>(
                node_addresses[node_rank], std::move(empty_dispatcher),
                derecho::CallbackSet{stability_callback, nullptr},
                derecho::DerechoParams{max_msg_size, block_size, std::string(), window_size});
    } else {
        managed_group = std::make_unique<derecho::Group<rpc::Dispatcher<>>>(
                node_rank, node_addresses[node_rank], server_rank,
                node_addresses[server_rank], std::move(empty_dispatcher),
                derecho::CallbackSet{stability_callback, nullptr});
    }

    cout << "Finished constructing/joining ManagedGroup" << endl;

    while(managed_group->get_members().size() < num_nodes) {
    }

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

    auto members_order = managed_group->get_members();
    // cout << "The order of members is :" << endl;
    // for(auto id : members_order) {
    //     cout << id << " ";
    // }
    // cout << endl;

    auto send_all = [&]() {
        for(int i = 0; i < num_messages; ++i) {
            // cout << "Asking for a buffer" << endl;
            char *buf = managed_group->get_sendbuffer_ptr(max_msg_size);
            while(!buf) {
                buf = managed_group->get_sendbuffer_ptr(max_msg_size);
            }
            // cout << "Obtained a buffer, sending" << endl;	    
            managed_group->send();
        }
    };
    auto send_one = [&]() {
        char *buf = managed_group->get_sendbuffer_ptr(1, num_messages);
        while(!buf) {
            buf = managed_group->get_sendbuffer_ptr(1, num_messages);
        }
        managed_group->send();
    };

    managed_group->barrier_sync();
    
    struct timespec start_time;
    // start timer
    clock_gettime(CLOCK_REALTIME, &start_time);
    if(num_senders_selector == 0) {
        send_all();
    } else if(num_senders_selector == 1) {
        if(node_rank > (num_nodes - 1) / 2) {
            send_all();
        } else {
            send_one();
        }
    } else {
        if(node_rank == num_nodes - 1) {
            // cout << "Sending all messages" << endl;
            send_all();
        } else {
            // cout << "Sending one message" << endl;
            send_one();
        }
    }
    while(!done) {
    }
    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);
    long long int nanoseconds_elapsed =
        (end_time.tv_sec - start_time.tv_sec) * (long long int)1e9 +
        (end_time.tv_nsec - start_time.tv_nsec);
    double bw;
    if(num_senders_selector == 0) {
        bw = (max_msg_size * num_messages * num_nodes + 0.0) /
             nanoseconds_elapsed;
    } else if(num_senders_selector == 1) {
        bw = (max_msg_size * num_messages * (num_nodes / 2) + 0.0) /
             nanoseconds_elapsed;
    } else {
        bw = (max_msg_size * num_messages + 0.0) / nanoseconds_elapsed;
    }
    double avg_bw = aggregate_bandwidth(members, node_rank, bw);
    log_results(num_nodes, num_senders_selector, max_msg_size, avg_bw,
                "data_derecho_bw");

    managed_group->barrier_sync();
    // flush_events();
    managed_group->barrier_sync();
    // std::string log_filename =
    //     (std::stringstream() << "events_node" << node_rank << ".csv").str();
    // std::ofstream logfile(log_filename);
    // managed_group->print_log(logfile);
    exit(0);
    managed_group->leave();
    cout << "Finished destroying managed_group" << endl;
}
