#include "coordinator.h"
#include "metadata.h"
#include <algorithm>
#include <random>
#include <vector>
namespace ECProject {

Coordinator::Coordinator(std::string ip, int port, std::string xml_path)
    : ip_(ip), port_(port), xml_path_(xml_path) {
    easylog::set_min_severity(easylog::Severity::DEBUG);
    rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(4, port_);
    rpc_server_->register_handler<&Coordinator::request_set>(this);
    rpc_server_->register_handler<&Coordinator::request_get>(this);
    rpc_server_->register_handler<&Coordinator::request_repair>(this);
    rpc_server_->register_handler<&Coordinator::get_stripe_info>(this);
    cur_stripe_id_ = 0;
    try {
        init_cluster_info();
    } catch (const std::exception &e) {
        std::cerr << "init_cluster_info failed: " << e.what() << std::endl;
        std::abort(); // 或 throw
    }
    
    ec_schema_.ec = std::make_unique<RSCode>(2, 1);
    ec_schema_.block_size = 16;

}
Coordinator::~Coordinator() { // 1. 先断开所有 datanodes（同步等待）
    for (auto &[uri, client] : datanodes_) {
        if (client) {
            client->close(); // 或 client->stop(); 查 API
            // 可加 syncAwait(client->async_close()) 若支持
        }
    }
    datanodes_.clear(); // 确保 client 析构前已 close

    // 2. 再停 server
    if (rpc_server_) {
        rpc_server_->stop();
    }
}
void Coordinator::run() {
    auto ret = rpc_server_->start();
}

void Coordinator::init_cluster_info() {
    tinyxml2::XMLDocument xml;
    if (xml.LoadFile(xml_path_.c_str()) != tinyxml2::XML_SUCCESS) {
        throw std::runtime_error("Failed to load node config XML");
    }

    tinyxml2::XMLElement *root = xml.RootElement(); // now <datanodes>
    if (!root || std::string(root->Name()) != "datanodes") {
        throw std::runtime_error("Root element must be <datanodes>");
    }
    unsigned int node_id = 0;
    for (tinyxml2::XMLElement *node = root->FirstChildElement("datanode");
         node != nullptr; node = node->NextSiblingElement("datanode")) {

        std::string uri = node->Attribute("uri");
        auto pos = uri.find(':');
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid node URI: missing ':'");
        }
        std::string ip = uri.substr(0, uri.find(':'));
        int port = std::stoi(uri.substr(uri.find(':') + 1, uri.size()));
        datanodes_[uri] = std::make_unique<coro_rpc::coro_rpc_client>();
        auto ec = async_simple::coro::syncAwait(
            datanodes_[uri]->connect(ip, std::to_string(port)));
        if (ec) {
            std::cerr << "Failed to connect to " << uri << ": " << ec.message()
                      << std::endl;
        }
        Node temp;
        temp.node_id = node_id;
        temp.node_ip = std::move(ip);
        temp.node_port = port;
        node_table_[node_id] = temp;
        ++node_id;
    }
    num_of_nodes_ = node_id;
}

Stripe &Coordinator::new_stripe() {
    Stripe temp;
    temp.stripe_id = cur_stripe_id_++;
    temp.blocks2nodes = generateUniqueRandom(
        num_of_nodes_, ec_schema_.ec->k + ec_schema_.ec->m);
    stripe_table_[temp.stripe_id] = temp;

    for (size_t i = 0; i < temp.blocks2nodes.size(); i++) {
        int node_id = temp.blocks2nodes[i];
        node_table_[node_id].nodes2blocks[temp.stripe_id] = i;
    }

    return stripe_table_[temp.stripe_id];
}

StripeInfo Coordinator::get_stripe_info(unsigned int stripe_id) {
    StripeInfo stripe_info;
    stripe_info.stripe_id = stripe_id;
    stripe_info.block_size = ec_schema_.block_size;
    stripe_info.k = ec_schema_.ec->k;
    stripe_info.m = ec_schema_.ec->m;
    stripe_info.w = ec_schema_.ec->w;
    auto node_ids = stripe_table_[stripe_id].blocks2nodes;
    for (auto node_id : node_ids) {
        Node node = node_table_[node_id];
        NodeIpInfo node_ip_info;
        node_ip_info.node_ip = node.node_id;
        node_ip_info.node_port = node.node_port;
        stripe_info.nodes_info.push_back(node_ip_info);
    }
    return stripe_info;
}

UploadInfo Coordinator::request_set(size_t value_size) {
    ELOG(DEBUG) << "value_size: " << value_size;
    my_assert(value_size == ec_schema_.block_size * ec_schema_.ec->k);
    
    Stripe stripe = new_stripe();
    UploadInfo upload_info;
    unsigned int node0_id = stripe.blocks2nodes[0];
    upload_info.stripe_id = stripe.stripe_id;
    upload_info.node_ip = node_table_[node0_id].node_ip;
    upload_info.node_port = node_table_[node0_id].node_port;
    return upload_info;
}

RepairResp Coordinator::request_repair(const Stripe &stripe,
                                       unsigned int failed_block_id) {
    RepairResp response;
    RepairPlan repair_plan = generate_repair_plan(stripe, failed_block_id);
    Node new_node = repair_plan.selected_new_node;
    std::string node_ip_port =
        new_node.node_ip + ":" + std::to_string(new_node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::do_repair>(
            repair_plan.stripe_id, repair_plan.helpers, ec_schema_.block_size,
            ec_schema_.ec->w));
    return response;
}
void Coordinator::request_get(unsigned int stripe_id) {}

RepairPlan Coordinator::generate_repair_plan(const Stripe &stripe,
                                             unsigned int failed_block_id) {
    unsigned int stripe_id = stripe.stripe_id;
    std::vector<std::vector<int>> &decode_matrix =
        opt_decode_matrix_with_all_failed_mode_[failed_block_id];
    std::vector<unsigned int> node_ids = stripe.blocks2nodes;
    RepairPlan repair_plan;
    for (size_t block_id = 0; block_id < node_ids.size(); block_id++) {
        if (block_id != failed_block_id) {
            std::vector<std::vector<int>> local_decode_matrix =
                get_submatrix(decode_matrix, block_id);
            if (local_decode_matrix.size() > 0) {
                unsigned int node_id = node_ids[block_id];
                Node helper_node = node_table_[node_id];
                DecodeRequest helper;
                helper.ip = helper_node.node_ip;
                helper.port = helper_node.node_port;
                helper.matrix = local_decode_matrix;
                repair_plan.helpers.push_back(helper);
            }
        }
    }
    repair_plan.stripe_id = stripe_id;
    unsigned int new_node_id = select_node(node_ids);
    repair_plan.selected_new_node = node_table_[new_node_id];
    return repair_plan;
}

std::vector<std::vector<int>>
Coordinator::get_submatrix(const std::vector<std::vector<int>> &decode_matrix,
                           int i) {
    if (decode_matrix.empty())
        return {};

    size_t w = decode_matrix.size();
    if (w == 0)
        return {};

    // 检查列对齐
    size_t total_cols = decode_matrix[0].size();
    if (total_cols % w != 0 || i < 0)
        return {};

    size_t block_idx = static_cast<size_t>(i);
    size_t blocks = total_cols / w;
    if (block_idx >= blocks)
        return {};

    // 计算第 i 块的列范围
    size_t start_col = block_idx * w;

    // 先检查是否全零（提前退出优化）
    bool all_zero = true;
    for (size_t r = 0; r < w && all_zero; ++r) {
        for (size_t c = 0; c < w && all_zero; ++c) {
            if (decode_matrix[r][start_col + c] != 0) {
                all_zero = false;
            }
        }
    }

    if (all_zero) {
        return {}; // 返回空矩阵
    }

    // 否则拷贝子矩阵
    std::vector<std::vector<int>> result(w, std::vector<int>(w));
    for (size_t r = 0; r < w; ++r) {
        std::copy_n(decode_matrix[r].begin() + start_col, w, result[r].begin());
    }
    return result;
}

unsigned int
Coordinator::select_node(const std::vector<unsigned int> &block2node) {
    int num_node = num_of_nodes_;
    std::vector<bool> used(num_node, false);
    for (int id : block2node) {
        if (id >= 0 && id < num_node) {
            used[id] = true;
        }
    }

    std::vector<int> candidates;
    candidates.reserve(num_node - block2node.size());
    for (int i = 0; i < num_node; ++i) {
        if (!used[i]) {
            candidates.push_back(i);
        }
    }

    if (candidates.empty()) {
        return -1; // 无可用节点（按需可 throw 或 assert）
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, (int)candidates.size() - 1);
    return candidates[dis(gen)];
}

} // namespace ECProject