#pragma once

#include <asio.hpp>
#include <fstream>
#include <map>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
extern "C" {
#include "../../Jerasure-1.2A/galois.h"
}

#include "metadata.h"
#ifdef IN_MEMORY
#ifdef MEMCACHED
#include <libmemcached/memcached.h>
#endif
#ifdef REDIS
#include <sw/redis++/redis++.h>
#endif
#else
#include <unistd.h>
#endif

namespace ECProject {
class Datanode {
  public:
    Datanode(std::string ip, int port);
    ~Datanode();
    void run();
    void handle_get(const std::string &key, size_t value_size,
                    const vector<vector<int>> &matrix);

    void handle_set(const std::string &key, size_t value_size);

  private:
    bool store_data(std::string &key, const char *value, size_t value_size);
    bool access_data(const std::string &key, char *value_buf,
                     size_t value_size);
    bool access_data(const std::string &key, char *value_buf,
                     const vector<int> &idxs);

    void local_decode(const std::vector<std::vector<int>> &matrix,
                      char *data_buf, char *decode_buf, size_t packet_size);

  private:
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};
    std::string ip_;
    int port_;
    int port_for_transfer_data_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
};
} // namespace ECProject
