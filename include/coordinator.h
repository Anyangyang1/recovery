#pragma once

#include "datanode.h"
#include "metadata.h"
#include "tinyxml2.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>

namespace ECProject {
class Coordinator {
  public:
    Coordinator(std::string ip, int port, std::string config_path);
    ~Coordinator();
    void run();
    void request_set(std::string key, size_t value_size);
    void request_get(std::string key);

    /**
     * @brief 发生单块故障时，请求修复
     * @param stripe: 待修复条带
     * @param failed_block_id: 故障块号
     * @return: 修复响应结果
     */
    RepairResp request_repair(Stripe &stripe, unsigned int failed_block_id);

  private:
    void init_cluster_info();
    Stripe &new_stripe(const std::string &key);

    void encode_and_store_object(Stripe stripe);

    void do_repair(std::string key, unsigned int failed_id,
                   RepairResp &response);
    std::vector<std::vector<int>>
    get_matrix(const std::vector<std::vector<int>> &decode_matrix, int i);

    /**
     * @brief 获取矩阵decode_matrix的第i个子矩阵
     * @return: 返回第i个子矩阵，如果第i个子矩阵为全0，返回{}
     */
    std::vector<std::vector<int>>
    get_submatrix(const std::vector<std::vector<int>> &decode_matrix, int i);

    std::vector<std::vector<int>>
    generate_repair_plan(const std::vector<std::vector<int>> &matrix);

    /**
     * @brief 根据条带和故障节点，生成修复计划
     * @param stripe: 待修复条带
     * @param failed_node: 故障节点
     * @return: 修复计划
     */
    RepairPlan generate_repair_plan(Stripe &stripe,
                                    unsigned int failed_block_id);

    void decode_xor(const std::vector<char> &original_data,
                    const std::vector<std::vector<int>> &repair_plan,
                    std::vector<char> &decode_data, size_t packet_size);

    void decode(const std::unordered_map<unsigned int, std::vector<char>>
                    &original_datas,
                const std::vector<std::vector<int>> &decode_matrix,
                std::vector<char> &decode_data);

    unsigned int select_node(const std::vector<unsigned int> &block2node);

  private:
    std::unordered_map<std::string, std::unique_ptr<coro_rpc::coro_rpc_client>>
        datanodes_;
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};
    std::string ip_;
    int port_;
    int port_for_transfer_data_;
    std::unordered_map<unsigned int, Node> node_table_;
    std::unordered_map<std::string, Stripe> stripe_table_;
    // std::string networkcore_;
    std::string config_path_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
    unsigned int cur_stripe_id_;
    int num_of_nodes_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<unsigned int> free_nodes_;
    ECSchema ec_schema_;
    std::string xml_path_;
    std::unordered_map<std::string, ObjectInfo> commited_object_table_;
    std::unordered_map<std::string, ObjectInfo> updating_object_table_;
    std::vector<std::vector<std::vector<int>>>
        opt_decode_matrix_with_all_failed_mode_;
};

} // namespace ECProject
