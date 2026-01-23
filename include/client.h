#pragma once

#include "coordinator.h"
#include "metadata.h"
#include <ylt/coro_rpc/coro_rpc_client.hpp>

namespace ECProject {
class Client {
  public:
    Client(std::string ip, int port, std::string coordinator_ip,
           int coordinator_port);
    ~Client();

    void set(std::string value);
    // void time_test();
    void request_repair(unsigned int stripe_id, unsigned int failed_block_id);
    void request_repair_with_opt(unsigned int stripe_id, unsigned int failed_block_id);
    void request_repair_no_local_decode(unsigned int stripe_id, unsigned int failed_block_id);
    void print_stripe_info();
    void print_node_info();
    void delete_file(unsigned int stripe_id, unsigned int failed_block_id);
    void delete_node(unsigned int node_id);
    void clear();
    double request_repair_node(unsigned int node_id);
    double request_repair_node_with_opt(unsigned int node_id);
    double request_repair_node_con(unsigned int node_id);
    double request_repair_node_with_opt_con(unsigned int node_id);
    double request_repair_node_non_local_decode_con(unsigned int node_id);
    double request_repair_node_non_local_decode(unsigned int node_id);
    // void repair_node_test();
    void set_stripe(int k, int block_size, unsigned int stripe_num);
    void clear_repair_file();
    

  private:
    std::unique_ptr<coro_rpc::coro_rpc_client> rpc_coordinator_{nullptr};
    std::string ip_;
    int port_;
    std::string coordinator_ip_;
    int coordinator_port_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
    std::mutex mutex_;
};
}; // namespace ECProject