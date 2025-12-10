#pragma once

#include "jerasure_wrapper.h"
#include "metadata.h"
#include <asio.hpp>
#include <coroutine>
#include <fstream>
#include <map>
#include <queue>
#include <thread>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
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
    void stop();
    void run();
    void handle_get_with_local_decode(const std::string &key, size_t value_size,
                                      const vector<vector<int>> &matrix);

    void handle_get(const std::string &key, size_t value_size);

    void handle_set(const std::string &key, size_t value_size);

    void handle_upload(unsigned int stripe_id, size_t value_size);

    void handle_delete_stripe(unsigned int stripe_id,
                              unsigned int failed_block_id);
    void handle_delete_all_file();

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

    /**
     * @brief 执行修复操作
     * @param stripe_id: 待修复的条带id
     * @param helpers: 参与修复的helpers
     * @param block_size: 数据块的大小
     */
    void do_repair_with_opt(std::vector<DecodeRequest> helpers,
                            size_t block_size, int w,
                            std::string repair_file_name);

    /**
     * @brief 执行修复操作
     * @param stripe_id: 待修复的条带id
     * @param helpers: 参与修复的helpers
     * @param block_size: 数据块的大小
     */
    void do_repair(std::vector<DecodeRequest> helpers, size_t block_size, int w,
                   std::string repair_file_name);

  private:
    /**
     * @brief 将数据写入磁盘中
     * @param key: 写入的文件名
     * @param value: 写入的数据
     * @param value_size: 写入数据的大小
     * @return: 数据写入是否成功
     */
    bool store_data(const std::string &key, const char *value,
                    size_t value_size);
    bool access_data(const std::string &key, char *value_buf,
                     size_t value_size);
    bool access_data(const std::string &key, char *value_buf,
                     const vector<int> &idxs);

    bool delete_file(const std::string &path);
    bool clear_directory(const std::string& dir_path);

    void local_decode(const std::vector<std::vector<int>> &matrix,
                      char *data_buf, char *decode_buf, size_t packet_size);

    /**
     * @brief 根据维矩阵，计算基底向量，以及每一行可以由哪些基底向量表示
     * @param matrix: w*w的维矩阵
     * @return: {basis,
     * reps}。其中basis表示基底向量，reps表示原来的矩阵可以由哪些基底向量线性表示
     */
    GF2BasisResult
    compute_basis_gf2_indices(const std::vector<std::vector<int>> &matrix);

    /**
     * @brief 将original_datas中的所有数据执行异或操作并返回
     * @param original_datas: 数据
     * @return: 返回异或后的数据
     */
    std::vector<char>
    decode_xor(const std::vector<std::vector<char>> &original_datas);

    /**
     * @brief 根据局部解码的数据，计算出解码需要的原始数据
     * @param buf: 局部解码数据
     * @param reps: 原始数据可由哪些局部解码数据计算得到
     * @param original_data: 解码需要的原始数据
     * @param packet_size: 包的大小
     */
    void compute_original_data(const char *buf,
                               const std::vector<std::vector<int>> &reps,
                               char *original_data, size_t packet_size);

    void encode_and_distribute(const StripeInfo &stripe_info,
                               std::unique_ptr<char[]> object_data,
                               size_t total_size);

  private:
    std::string ip_;
    int port_;
    int port_for_transfer_data_;
    std::string coordinator_ip_;
    int coordinator_port_;

    asio::io_context io_context_{};

    // data listener
    asio::ip::tcp::acceptor acceptor_;

    // RPC
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};

    // 辅助哈希（C++11 兼容）
    struct VecIntHash {
        size_t operator()(const std::vector<int> &v) const {
            size_t h = v.size();
            for (int x : v) {
                h ^= static_cast<size_t>(x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };
};
} // namespace ECProject
