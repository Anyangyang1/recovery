#pragma once
#include "erasure_code.h"
#include "rs.h"
#include "utils.h"
#include <unordered_map>
#include <unordered_set>
#define LOG_TO_FILE true
#define IF_SIMULATION false
#define IF_SIMULATE_CROSS_CLUSTER true
#define IF_TEST_TRHROUGHPUT false
#define IF_DEBUG false
#define IF_DIRECT_FROM_NODE                                                    \
    true // proxy can directly access data from nodes in other clusters
#define SOCKET_PORT_OFFSET 500
#define STORAGE_SERVER_OFFSET 1000
// #define IN_MEMORY true
// #define MEMCACHED true
// #define REDIS true
#define COORDINATOR_PORT 12121
#define CLIENT_PORT 21212

namespace ECProject {
struct ECSchema {
    ErasureCode *ec = nullptr;
    size_t block_size; // bytes

    ~ECSchema() {
        if (ec != nullptr) {
            delete ec;
        }
    }

    void set_ec(ErasureCode *new_ec) {
        if (ec != nullptr) {
            delete ec;
            ec = nullptr;
        }
        ec = new_ec;
    }
};

struct Stripe {
    unsigned int stripe_id;
    std::vector<unsigned int> blocks2nodes;
};

struct Node {
    unsigned int node_id;
    std::string node_ip;
    int node_port;
    std::unordered_map<unsigned int, unsigned int> nodes2blocks;
};

struct DecodeRequest {
    // DecodeRequest() {}
    // DecodeRequest(std::string ip, int port,
    //               std::vector<std::vector<int>> matrix)
    //     : ip(ip), port(port), matrix(matrix) {}
    std::string ip;
    int port;
    std::vector<std::vector<int>> matrix; // 解码矩阵（如纠删码矩阵）
};

struct RepairPlan {
    unsigned int stripe_id;             // 待修复的条带id
    std::vector<DecodeRequest> helpers; // 参与修复的helpers
    Node selected_new_node;             // 修复完成后，数据放置的目标节点
};

struct RepairResp {
    double decoding_time;
    double cross_cluster_time;
    double repair_time;
    double meta_time;
    int cross_cluster_transfers;
    int io_cnt;
    bool success;
};

struct GF2BasisResult {
    std::vector<std::vector<int>> basis;          // 基向量（行），size = rank
    std::vector<std::vector<int>> reps;           // reps[i] = {k1, k2, ...} 表示 row_i = basis[k1] ⊕ basis[k2] ⊕ ...
};

} // namespace ECProject