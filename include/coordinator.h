#pragma once

#include "tinyxml2.h"
#include "metadata.h"
#include "proxy.h"
#include <mutex>
#include <condition_variable>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>

namespace ECProject {
        
class Coordinator {
public:
    Coordinator(std::string ip, int port, std::string xml_path);
    ~Coordinator();

    void run();
private:
    std::unique_ptr<coro_rpc::coro_rpc_server> rpc_server_{nullptr};
    std::unordered_map<std::string, std::unique_ptr<coro_rpc::coro_rpc_client>> proxies_;
    ECSchema ec_schema_;
    std::unordered_map<unsigned int, Cluster> cluster_table_;
    std::unordered_map<unsigned int, Node> node_table_;
    std::unordered_map<unsigned int, Stripe> stripe_table_;
    std::unordered_map<std::string, ObjectInfo> commited_object_table_;
    std::unordered_map<std::string, ObjectInfo> updating_object_table_;

    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned int cur_stripe_id_;
    int num_of_clusters_;
    int num_of_nodes_per_cluster_;
    std::string ip_;
    int port_;
    std::string xml_path_;
    double time_;
    unsigned int cur_block_id_;
    unsigned int lucky_cid_;
    std::vector<std::vector<unsigned int>> merge_groups_;
    std::vector<unsigned int> free_clusters_;
    bool merged_flag_ = false;
};

}
