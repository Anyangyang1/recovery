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
    Coordinator(std::string ip, int port, std::string xml_path);
    ~Coordinator();
    void run();
    unsigned int request_set(size_t value_size);
    void request_get(unsigned int stripe_id);

    /**
     * @brief 发生单块故障时，请求修复
     * @param stripe: 待修复条带
     * @param failed_block_id: 故障块号
     * @return: 修复响应结果
     */
    RepairResp request_repair(Stripe &stripe, unsigned int failed_block_id);

  

  private:
    void init_cluster_info();
    Stripe &new_stripe();

    void encode_and_store_object(Stripe stripe);

    /**
     * @brief 获取矩阵decode_matrix的第i个子矩阵
     * @return: 返回第i个子矩阵，如果第i个子矩阵为全0，返回{}
     */
    std::vector<std::vector<int>>
    get_submatrix(const std::vector<std::vector<int>> &decode_matrix, int i);

    /**
     * @brief 根据条带和故障节点，生成修复计划
     * @param stripe: 待修复条带
     * @param failed_node: 故障节点
     * @return: 修复计划
     */
    RepairPlan generate_repair_plan(Stripe &stripe,
                                    unsigned int failed_block_id);

    unsigned int select_node(const std::vector<unsigned int> &block2node);

  private:
    std::unordered_map<std::string, std::unique_ptr<coro_rpc::coro_rpc_client>>
        datanodes_;
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};
    std::string ip_;
    int port_;
    int port_for_transfer_data_;
    std::unordered_map<unsigned int, Node> node_table_;
    std::unordered_map<unsigned int, Stripe> stripe_table_;
    std::string xml_path_;
    asio::io_context io_context_{};
    asio::ip::tcp::acceptor acceptor_;
    unsigned int cur_stripe_id_;
    int num_of_nodes_;
    std::mutex mutex_;
    std::condition_variable cv_;
    ECSchema ec_schema_;
    std::unordered_map<std::string, ObjectInfo> commited_object_table_;
    std::unordered_map<std::string, ObjectInfo> updating_object_table_;
    std::vector<std::vector<std::vector<int>>>
        opt_decode_matrix_with_all_failed_mode_;
};

} // namespace ECProject
