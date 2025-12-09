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