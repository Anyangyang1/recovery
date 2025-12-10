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
    void request_repair(unsigned int stripe_id, unsigned int failed_block_id);
    void request_repair_with_opt(unsigned int stripe_id, unsigned int failed_block_id);
    void print_stripe_info();
    void print_node_info();
    void delete_file(unsigned int stripe_id, unsigned int failed_block_id);
    void delete_all_file(unsigned int node_id);
    void request_repair_node(unsigned int node_id);
    void request_repair_node_with_opt(unsigned int node_id);
    

  private:
    std::unique_ptr<coro_rpc::coro_rpc_client> rpc_coordinator_{nullptr};
    std::string ip_;
    int port_;
    std::string coordinator_ip_;
    int coordinator_port_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
};
}; // namespace ECProject