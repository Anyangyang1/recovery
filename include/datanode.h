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
    // void handle_get_local_decode(const std::string &key, size_t value_size,
    //                              const vector<vector<int>> &matrix);

    // void handle_get_original(const std::string &key, size_t value_size);

    // void handle_set(const std::string &key, size_t value_size);

    /**
     * @brief 从指定节点读取进行局部解码后的数据
     * @param ip: 读取节点的ip
     * @param port: 读取节点的port
     * @param key: 读取的文件名
     * @param value: 将数据读入value中
     * @param value_size: 读取数据的大小
     * @param matrix: 进行局部解码的矩阵
     * @return: 数据读取是否成功
     */
    bool read_from_datanode_with_local_decode(
        const std::string &ip, int port, const std::string &key, char *value,
        size_t value_size, const vector<vector<int>> &matrix);

    /**
     * @brief 从指定节点读取数据
     * @param ip: 读取节点的ip
     * @param port: 读取节点的port
     * @param key: 读取的文件名
     * @param value: 将数据读入value中
     * @param value_size: 读取数据的大小
     * @return: 数据读取是否成功
     */
    bool read_from_datanode(const std::string &ip, int port,
                            const std::string &key, char *value,
                            size_t value_size);

    /**
     * @brief 将数据写入指定节点
     * @param ip: 写入节点的ip
     * @param port: 写入节点的port
     * @param key: 写入的文件名
     * @param value: 写入的数据
     * @param value_size: 写入数据的大小
     * @return: 数据写入是否成功
     */
    bool write_to_datanode(const std::string &ip, int port,
                           const std::string &key, char *value,
                           size_t value_size);

  private:
    bool store_data(const std::string &key, const char *value,
                    size_t value_size);
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
