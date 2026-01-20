#pragma once
#include "erasure_code.h"
#include "rs.h"
#include "rs_xor.h"
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
// #define COORDINATOR_IP "192.168.1.12"
// #define COORDINATOR_IP "10.0.0.2"
#define COORDINATOR_IP "100.0.0.2"
#define CLIENT_PORT 21212

#define RS_K 4
#define RS_M 4
#define RS_W 8
#define KB 1024
#define MB (1024 * 1024)
#define BLOCK_SIZE (8 * MB)
#define PACKET_SIZE BLOCK_SIZE / RS_W
#define RPC_NUM 30

#define SIMD_ALIGNMENT 32 // 64-byte 对齐，兼容 AVX2/AVX-512/cache line

namespace ECProject {

struct ECSchema {
    size_t block_size = 0;
    ECTYPE ec_type;
    std::unique_ptr<ErasureCode> ec; // 改为智能指针，更安全
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

struct NodeIpInfo {
    std::string node_ip;
    int node_port;
};

struct DecodeRequest {
    std::string ip;
    int port;
    std::string file_name;
    std::vector<std::vector<int>> matrix; // 解码矩阵（如纠删码矩阵）
};

struct UploadInfo {
    std::string node_ip;
    int node_port;
    unsigned int stripe_id;
};

struct StripeInfo {
    unsigned int stripe_id;
    std::vector<NodeIpInfo> nodes_info;
    int k;
    int m;
    int w;
    size_t block_size;
    ECTYPE ec_type;
};

struct RepairPlan {
    unsigned int stripe_id;             // 待修复的条带id
    std::vector<DecodeRequest> helpers; // 参与修复的helpers
    Node selected_new_node;             // 修复完成后，数据放置的目标节点
    std::string repair_file_name;
};

struct RepairResp {
    double read_from_disk_time;
    double local_decode_time;
    double send_to_net_time;

    double read_data_time;
    double computing_time;
    double write_disk_time;
    double repair_time;
};

struct GF2BasisResult {
    std::vector<std::vector<int>> basis; // 基向量（行），size = rank
    std::vector<std::vector<int>> reps;  // reps[i] = {k1, k2, ...} 表示 row_i =
                                         // basis[k1] ⊕ basis[k2] ⊕ ...
};

// === 共享数据结构：每个 helper 的 [buf, original_data] ===
struct HelperData {
    std::unique_ptr<char[]> buf;           // 从网络读的小 buffer（basis 大小）
    std::unique_ptr<char[]> original_data; // 恢复出的完整 block_size 数据
    size_t buf_size = 0;
    size_t block_size = 0;
};

} // namespace ECProject