#pragma once

#include "coordinator.h"
#include "metadata.h"
#include <ylt/coro_rpc/coro_rpc_client.hpp>

namespace ECProject {
  class Client {
  public:
    Client(std::string ip, int port, std::string coordinator_ip, int coordinator_port);
    ~Client();

    void set(std::string value);

  private:
    std::unique_ptr<coro_rpc::coro_rpc_client> rpc_coordinator_{nullptr};
    int port_;
    std::string ip_;
    std::string coordinator_ip_;
    int coordinator_port_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
  };
};