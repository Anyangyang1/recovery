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
    size_t packet_size;

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

struct ObjectInfo {
    size_t value_len;
    std::vector<unsigned int> stripes;
};

struct Stripe {
    unsigned int stripe_id;
    std::string key;
    std::vector<unsigned int> blocks2nodes;
    std::vector<char*> objects; // in order with data blocks
};

struct Node {
    unsigned int node_id;
    std::string node_ip;
    int node_port;
    std::unordered_map<std::string, unsigned int> nodes2blocks;
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


struct MainRepairResp {
    double decoding_time;
    double cross_cluster_time;
};

struct MainRecalResp {
    double computing_time;
    double cross_cluster_time;
};

struct RelocateResp {
    double cross_cluster_time;
};

ECFAMILY check_ec_family(ECTYPE ec_type);
ErasureCode *ec_factory(ECTYPE ec_type, CodingParameters cp);
RSCode *rs_factory(ECTYPE ec_type, CodingParameters cp);
ErasureCode *clone_ec(ECTYPE ec_type, ErasureCode *ec);
void parse_args(ParametersInfo &paras, std::string config_file);
int stripe_wide_after_merge(ParametersInfo paras, int step_size);
} // namespace ECProject