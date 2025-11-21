#pragma once

#include "datanode.h"
#include "metadata.h"
#include "tinyxml2.h"
#include <condition_variable>
#include <mutex>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>

namespace ECProject {
class Proxy {
  public:
    Proxy(std::string ip, int port, std::string networkcore,
          std::string config_path);
    ~Proxy();
    void run();

  private:
    bool read_from_datanode(const string &ip, int port, const string &key,
                            char *value, size_t value_size,
                            const vector<vector<int>> &matrix);

    void write_to_datanode(const string &ip, int port, const string &key,
                           char *value, size_t value_size);

  private:
    std::unordered_map<std::string, std::unique_ptr<coro_rpc::coro_rpc_client>>
        datanodes_;
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};
    int self_cluster_id_;
    int port_;
    int port_for_transfer_data_;
    std::string ip_;
    std::string networkcore_;
    std::string config_path_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace ECProject
