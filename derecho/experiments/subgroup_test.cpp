#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <map>

using std::string;
using std::cin;
using std::cout;
using std::endl;
using std::map;

#include "derecho/derecho_group.h"
#include "derecho/derecho_caller.h"
#include "block_size.h"
#include "derecho/managed_group.h"
#include "derecho/view.h"

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
        uint32_t num_nodes = std::atoi(argv[1]);
        derecho::node_id_t node_id;
        derecho::ip_addr my_ip;
        derecho::node_id_t leader_id = 0;
        derecho::ip_addr leader_ip;

        query_node_info(node_id, my_ip, leader_ip);

        long long unsigned int max_msg_size = 1000000;
        long long unsigned int block_size = 100000;

        int num_messages = 100;

        auto stability_callback = [&node_id, &num_messages](
            uint32_t subgroup_num, int sender_rank, long long int index, char *buf,
            long long int msg_size) {
	  if (subgroup_num == 1 && index == 10 && node_id == 0) {
	    cout << "Exiting" << endl;
	    cout << "The last message is: " << endl;
	    cout << buf << endl;
	    exit(0);
	  }
	  if (subgroup_num == 1 && index == 50 && (node_id == 3 || node_id == 5)) {
	    cout << "Exiting" << endl;
	    cout << "The last message is: " << endl;
	    cout << buf << endl;
	    exit(0);
	  }
	  if (index == 100) {
	    cout << "Received the last message in subgroup " << subgroup_num << " from sender " << sender_rank << endl;
	    cout << "The last message is: " << endl;
	    cout << buf << endl;
	  }
            // cout << "In stability callback; sender = " << sender_rank
	    // << ", index = " << index << endl;
        };
	
        derecho::CallbackSet callbacks{stability_callback, nullptr};
        derecho::DerechoParams param_object{max_msg_size, block_size};
        Dispatcher<> empty_dispatcher(node_id);
        std::unique_ptr<derecho::ManagedGroup<Dispatcher<>>> managed_group;

        derecho::SubgroupInfo subgroup_info{[](uint32_t num_members) {
	    if (num_members >= 7) {
	      return 3;
	    }
	    else if (num_members >= 5) {
	      return 2;
	    }
	    return 0;
	  },
	    [](uint32_t num_members, uint32_t subgroup_num) {
	      if (subgroup_num == 0) {
	      	return 2;
	      }
	      else if (subgroup_num == 1) {
	      	return 3;
	      }
	      else if (subgroup_num == 2) {
	      	return 1;
	      }
	      else {
	      	return 1;
	      }
	    },
	      [](std::vector<uint32_t> all_members, uint32_t subgroup_num, uint32_t shard_num) {
		std::vector<uint32_t> members;
		uint32_t step = 0;
		if (subgroup_num == 0) {
		  step = 2;
		}
		else if (subgroup_num == 1) {
		  step = 3;
		}
		else {
		  step = 4;
		}
		for (auto n : all_members) {
		  if (n%step == shard_num) {
		    members.push_back(n);
		  }
		}
		return members;
	      }};
        if(node_id == leader_id) {
            assert(my_ip == leader_ip);
            managed_group = std::make_unique<derecho::ManagedGroup<Dispatcher<>>>(
                my_ip, std::move(empty_dispatcher), callbacks, subgroup_info, param_object);
        } else {
            managed_group = std::make_unique<derecho::ManagedGroup<Dispatcher<>>>(
                node_id, my_ip, leader_id, leader_ip, std::move(empty_dispatcher), callbacks, subgroup_info);
        }

        cout << "Finished constructing/joining ManagedGroup" << endl;

        while(managed_group->get_members().size() < num_nodes) {
        }

        auto send = [&]() {
	  for(int i = 0; i < num_messages; ++i) {
	    for (uint j = 0; j < 3; ++j) {
	      // random message size between 1 and 100
	      unsigned int msg_size = (rand() % 7 + 2) * (max_msg_size / 10);
	      char *buf = managed_group->get_sendbuffer_ptr(j, msg_size);
	      bool not_sending = false;
	      //        cout << "After getting sendbuffer for message " << i <<
	      //        endl;
	      //        managed_group.debug_print_status();
	      while(!buf) {
		buf = managed_group->get_sendbuffer_ptr(j, msg_size);
		if ((managed_group->get_members().size() == 7) && (j == 2)) {
		  not_sending = true;
		  break;
		}
		if ((managed_group->get_members().size() < 6)) {
		  not_sending = true;
		  break;
		}
	      }
	      if (not_sending) {
		continue;
	      }
	      for(unsigned int k = 0; k < msg_size-1; ++k) {
		buf[k] = 'a' + j;
	      }
	      buf[msg_size-1] = 0;
	      //        cout << "Client telling DerechoGroup to send message " <<
	      //        i << "
	      //        with size " << msg_size << endl;;
	      managed_group->send(j);
	    }
	  }
	};

	send();
	
        // everything that follows is rendered irrelevant
        while(true) {
        }

        cout << "Done" << endl;
        managed_group->barrier_sync();

        managed_group->leave();

    } catch(const std::exception &e) {
        cout << "Main got an exception: " << e.what() << endl;
        throw e;
    }

    cout << "Finished destroying managed_group" << endl;
}
