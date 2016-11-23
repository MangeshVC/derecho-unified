
#ifndef GROUP_SEND_H
#define GROUP_SEND_H

#include "rdmc.h"
#include "schedule.h"
#include "verbs_helper.h"

#include <boost/optional.hpp>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

using boost::optional;
using std::vector;
using std::map;
using std::unique_ptr;
using std::shared_ptr;
using rdmc::incoming_message_callback_t;
using rdmc::completion_callback_t;

class group {
protected:
    const vector<uint32_t> members;  // first element is the sender
    const uint16_t group_number;
    const size_t block_size;
    const uint32_t num_members;
    const uint32_t member_index;  // our index in the members list

	const unique_ptr<schedule> transfer_schedule;

    std::mutex monitor;

    std::shared_ptr<rdma::memory_region> mr;
    size_t mr_offset;
    size_t message_size;
    size_t num_blocks;

    completion_callback_t completion_callback;
    incoming_message_callback_t incoming_message_upcall;

    group(uint16_t group_number, size_t block_size,
          vector<uint32_t> members, uint32_t member_index,
          incoming_message_callback_t upcall,
          completion_callback_t callback,
          unique_ptr<schedule> transfer_schedule);

public:
    virtual ~group();

    virtual void receive_block(uint32_t send_imm, size_t size) = 0;
    virtual void receive_ready_for_block(uint32_t step, uint32_t sender) = 0;
    virtual void complete_block_send() = 0;
    virtual void send_message(std::shared_ptr<rdma::memory_region> message_mr,
                              size_t offset, size_t length) = 0;
};

class polling_group : public group {
private:
    // Set of receivers who are ready to receive the next block from us.
    std::set<uint32_t> receivers_ready;

    unique_ptr<rdma::memory_region> first_block_mr;
    optional<size_t> first_block_number;
    unique_ptr<char[]> first_block_buffer;

    size_t incoming_block;
    size_t message_number = 0;

    size_t outgoing_block;
    bool sending = false;  // Whether a block send is in progress
    size_t send_step = 0;  // Number of blocks sent/stalls so far

    // Total number of blocks received and the number of chunks
    // received for ecah block, respectively.
    size_t num_received_blocks = 0;
    size_t receive_step = 0;
    vector<bool> received_blocks;

    // maps from member_indices to the queue pairs
    map<size_t, rdma::queue_pair> queue_pairs;
    map<size_t, rdma::queue_pair> rfb_queue_pairs;
	
	static struct {
        rdma::message_type data_block;
        rdma::message_type ready_for_block;
    } message_types;
	
public:
	static void initialize_message_types();

    polling_group(uint16_t group_number, size_t block_size,
                  vector<uint32_t> members, uint32_t member_index,
                  incoming_message_callback_t upcall,
                  completion_callback_t callback,
                  unique_ptr<schedule> transfer_schedule);

    virtual void receive_block(uint32_t send_imm, size_t size);
    virtual void receive_ready_for_block(uint32_t step, uint32_t sender);
    virtual void complete_block_send();

    virtual void send_message(std::shared_ptr<rdma::memory_region> message_mr,
                              size_t offset, size_t length);

private:
    void post_recv(schedule::block_transfer transfer);
    void send_next_block();
    void complete_message();
    void prepare_for_next_message();
    void send_ready_for_block(uint32_t neighbor);
    void connect(uint32_t neighbor);
};

class cross_channel_group : public group {
private:
    unique_ptr<rdma::memory_region> first_block_mr;
    optional<size_t> first_block_number;
    unique_ptr<char[]> first_block_buffer;

    size_t message_number = 0;

	bool message_in_progress = false;

	rdma::memory_region init_mr;
	
    // maps from member_indices to the queue pairs
	map<size_t, rdma::queue_pair> init_queue_pairs;
    map<size_t, rdma::managed_queue_pair> queue_pairs;
    map<size_t, rdma::managed_queue_pair> rfb_queue_pairs;
	shared_ptr<rdma::manager_queue_pair> mqp;

    // Map from member index to number of block sends/receives that have been
	// posted for them. Used for enabling/waiting on sends and receives.
    map<size_t, size_t> send_counts;
	map<size_t, size_t> recv_counts;
	
    // Map from member index to number of ready_for_block message sends/receives
	// that have been posted for them.
	map<size_t, size_t> rfb_send_counts;
	map<size_t, size_t> rfb_recv_counts;

	unsigned int init_ack_count = 0;
	std::mutex init_ack_count_mutex;
	std::condition_variable init_ack_count_cv;
	
	static struct {
		rdma::message_type init;
		rdma::message_type init_ack;
		rdma::message_type completed;
    } message_types;

	struct init_message {
		size_t size;
	};
	
public:
	static void initialize_message_types();

    cross_channel_group(uint16_t group_number, size_t block_size,
                  vector<uint32_t> members, uint32_t member_index,
                  incoming_message_callback_t upcall,
                  completion_callback_t callback,
                  unique_ptr<schedule> transfer_schedule);
	~cross_channel_group(){}
	
    virtual void receive_block(uint32_t send_imm, size_t size) {}
    virtual void receive_ready_for_block(uint32_t step, uint32_t sender) {}
    virtual void complete_block_send() {}

    virtual void send_message(std::shared_ptr<rdma::memory_region> message_mr,
                              size_t offset, size_t length);

private:
    void complete_message();
    void prepare_for_next_message();
    void connect(uint32_t neighbor);
	void receive_init();
	void post_relay_task();
};

#endif /* GROUP_SEND_H */
