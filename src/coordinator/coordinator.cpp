#include "coordinator.h"
#include "metadata.h"
#include "sggh.h"
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
    rpc_server_->register_handler<&Coordinator::request_repair_with_opt>(this);
    rpc_server_->register_handler<&Coordinator::get_stripe_info>(this);
    rpc_server_->register_handler<&Coordinator::print_stripe_info>(this);
    rpc_server_->register_handler<&Coordinator::print_node_info>(this);
    rpc_server_->register_handler<&Coordinator::delete_failed_block>(this);
    rpc_server_->register_handler<&Coordinator::delete_all_file>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_node>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_node_with_opt>(this);

    cur_stripe_id_ = 0;
    try {
        init_cluster_info();
    } catch (const std::exception &e) {
        std::cerr << "init_cluster_info failed: " << e.what() << std::endl;
        std::abort(); // 或 throw
    }

    ec_schema_.ec = std::make_unique<XORCode>(RS_K, RS_M, RS_W);
    ec_schema_.block_size = BLOCK_SIZE;
    ec_schema_.ec_type = XOR;

    SimilarityGreedy sg = SimilarityGreedy(RS_K, RS_M, RS_W);
    opt_decode_matrix_with_all_failed_mode_ =
        sg.generateOptDecodeBitMatrixWithAllMode(0);
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
void Coordinator::run() { auto ret = rpc_server_->start(); }

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

void Coordinator::print_stripe_info() {
    ELOG(DEBUG) << "stripe info:";
    for (const auto &[key, value] : stripe_table_) {
        ELOG(DEBUG) << "stripe_" << value.stripe_id << ": "
                    << vecToString(value.blocks2nodes);
    }
}

void Coordinator::print_node_info() {
    ELOG(DEBUG) << "nodes info: ";
    for (const auto &[key, value] : node_table_) {
        ELOG(DEBUG) << "node_" << value.node_id << ": "
                    << mapToString(value.nodes2blocks);
    }
}

StripeInfo Coordinator::get_stripe_info(unsigned int stripe_id) {
    StripeInfo stripe_info;
    stripe_info.stripe_id = stripe_id;
    stripe_info.k = ec_schema_.ec->k;
    stripe_info.m = ec_schema_.ec->m;
    stripe_info.w = ec_schema_.ec->w;
    stripe_info.block_size = ec_schema_.block_size;
    stripe_info.ec_type = ec_schema_.ec_type;
    auto node_ids = stripe_table_[stripe_id].blocks2nodes;
    for (auto node_id : node_ids) {
        Node node = node_table_[node_id];
        NodeIpInfo node_ip_info;
        node_ip_info.node_ip = node.node_ip;
        node_ip_info.node_port = node.node_port;
        stripe_info.nodes_info.push_back(node_ip_info);
    }
    return stripe_info;
}

UploadInfo Coordinator::request_set(size_t value_size) {
    my_assert(value_size == ec_schema_.block_size * ec_schema_.ec->k);

    Stripe stripe = new_stripe();
    UploadInfo upload_info;
    unsigned int node0_id = stripe.blocks2nodes[0];
    upload_info.stripe_id = stripe.stripe_id;
    upload_info.node_ip = node_table_[node0_id].node_ip;
    upload_info.node_port = node_table_[node0_id].node_port;
    return upload_info;
}

RepairResp Coordinator::request_repair_with_opt(unsigned int stripe_id,
                                                unsigned int failed_block_id) {
    const Stripe stripe = stripe_table_[stripe_id];
    RepairResp response;
    RepairPlan repair_plan =
        generate_repair_plan_with_opt(stripe, failed_block_id);
    Node new_node = repair_plan.selected_new_node;
    std::string node_ip_port =
        new_node.node_ip + ":" + std::to_string(new_node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::do_repair_with_opt>(
            repair_plan.helpers, ec_schema_.block_size, ec_schema_.ec->w,
            repair_plan.repair_file_name));
    alter_metadata(stripe_id, failed_block_id, new_node.node_id);
    ELOG(DEBUG) << "select node_" << new_node.node_id << " to repair stripe_"
                << stripe_id << "_" << failed_block_id;
    return response;
}

RepairResp Coordinator::request_repair(unsigned int stripe_id,
                                       unsigned int failed_block_id) {
    const Stripe &stripe = stripe_table_[stripe_id];
    RepairResp response;
    RepairPlan repair_plan = generate_repair_plan(stripe, failed_block_id);
    Node new_node = repair_plan.selected_new_node;
    std::string node_ip_port =
        new_node.node_ip + ":" + std::to_string(new_node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::do_repair>(
            repair_plan.helpers, ec_schema_.block_size, ec_schema_.ec->w,
            repair_plan.repair_file_name));
    alter_metadata(stripe_id, failed_block_id, new_node.node_id);
    ELOG(DEBUG) << "select node_" << new_node.node_id << " to repair stripe_"
                << stripe_id << "_" << failed_block_id;
    return response;
}

RepairResp Coordinator::request_repair_node(unsigned int node_id) {
    Node node = node_table_[node_id];
    RepairResp response;
    std::vector<std::future<RepairResp>> futures;
    for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
        futures.push_back(
            std::async(std::launch::async, [this, stripe_id, block_id] {
                return request_repair(stripe_id, block_id);
            }));
    }
    for (auto &f : futures) {
        auto r = f.get(); // 会 rethrow 异常
        // resp.success &= r.success;  // 按需合并
    }
    // for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
    //     request_repair(stripe_id, block_id);
    // }
    return response;
}

RepairResp Coordinator::request_repair_node_with_opt(unsigned int node_id) {
    Node node = node_table_[node_id];
    RepairResp response;
    std::vector<std::future<RepairResp>> futures;
    for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
        futures.push_back(
            std::async(std::launch::async, [this, stripe_id, block_id] {
                return request_repair_with_opt(stripe_id, block_id);
            }));
    }
    for (auto &f : futures) {
        auto r = f.get(); // 会 rethrow 异常
        // resp.success &= r.success;  // 按需合并
    }
    // for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
    //     request_repair_with_opt(stripe_id, block_id);
    // }
    return response;
}


void Coordinator::alter_metadata(unsigned int stripe_id,
                                 unsigned int failed_block_id,
                                 unsigned int new_node_id) {
    unsigned int old_node_id =
        stripe_table_[stripe_id].blocks2nodes[failed_block_id];
    stripe_table_[stripe_id].blocks2nodes[failed_block_id] = new_node_id;
    node_table_[old_node_id].nodes2blocks.erase(stripe_id);
    node_table_[new_node_id].nodes2blocks[stripe_id] = failed_block_id;
}

void Coordinator::delete_failed_block(unsigned int stripe_id,
                                      unsigned int failed_block_id) {
    Stripe &stripe = stripe_table_[stripe_id];
    unsigned int node_id = stripe.blocks2nodes[failed_block_id];
    Node &node = node_table_[node_id];
    std::string node_ip_port =
        node.node_ip + ":" + std::to_string(node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::handle_delete_stripe>(
            stripe_id, failed_block_id));
}
void Coordinator::delete_all_file(unsigned int node_id) {
    Node &node = node_table_[node_id];
    std::string node_ip_port =
        node.node_ip + ":" + std::to_string(node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::handle_delete_all_file>());
}

void Coordinator::request_get(unsigned int stripe_id) {}

RepairPlan
Coordinator::generate_repair_plan_with_opt(const Stripe &stripe,
                                           unsigned int failed_block_id) {
    unsigned int stripe_id = stripe.stripe_id;
    std::vector<std::vector<int>> &decode_matrix =
        opt_decode_matrix_with_all_failed_mode_[failed_block_id];
    // cout << "decode matrix: " << endl;
    // cout << decode_matrix << endl;
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
                helper.file_name = "stripe_" + std::to_string(stripe_id) + "_" +
                                   std::to_string(block_id);
                repair_plan.helpers.push_back(helper);
            }
        }
    }
    repair_plan.stripe_id = stripe_id;
    unsigned int new_node_id = select_node(node_ids);
    repair_plan.selected_new_node = node_table_[new_node_id];
    repair_plan.repair_file_name = "stripe_" + std::to_string(stripe_id) + "_" +
                                   std::to_string(failed_block_id);
    return repair_plan;
}

RepairPlan Coordinator::generate_repair_plan(const Stripe &stripe,
                                             unsigned int failed_block_id) {
    const int k = ec_schema_.ec->k;
    const int m = ec_schema_.ec->m;
    const int w = ec_schema_.ec->w;
    std::vector<std::vector<int>> codingMatrix =
        cauchy_original_coding_matrix_vector(k, m, w);
    // cout << "codingMatrix: " << endl;
    // cout << codingMatrix << endl;
    std::vector<unsigned int> node_ids = stripe.blocks2nodes;
    unsigned int stripe_id = stripe.stripe_id;
    RepairPlan repair_plan;
    repair_plan.stripe_id = stripe_id;
    unsigned int new_node_id = select_node(node_ids);
    repair_plan.selected_new_node = node_table_[new_node_id];
    repair_plan.repair_file_name = "stripe_" + std::to_string(stripe_id) + "_" +
                                   std::to_string(failed_block_id);

    std::vector<std::vector<int>> recoveryCoeffs(1, std::vector<int>(k, 0));
    std::vector<int> recoveryIds(k, 0);
    // 校验块故障
    if (failed_block_id >= (unsigned int)k) {

        int blkIdx = failed_block_id - k;
        for (int i = 0; i < k; i++) {
            recoveryCoeffs[0][i] = codingMatrix[blkIdx][i];
            recoveryIds[i] = i;
        }
    } else { // 数据块故障
        vector<int> codingFlat(m * k);
        for (int i = 0; i < m; ++i) {
            for (int j = 0; j < k; ++j) {
                codingFlat[i * k + j] = codingMatrix[i][j];
            }
        }
        vector<int> erased(k + m, 0);
        erased[failed_block_id] = 1;
        // 调用 Jerasure 生成解码矩阵
        vector<int> decodeMatrix(k * k);
        vector<int> dmIds(k);
        jerasure_make_decoding_matrix(k, m, w, codingFlat.data(), erased.data(),
                                      decodeMatrix.data(), dmIds.data());
        // cout << "decodeMatrix: " << endl;
        // for (int i = 0; i < k; i++) {
        //     for (int j = 0; j < k; j++) {
        //         cout << decodeMatrix[i * k + j] << " ";
        //     }
        //     cout << endl;
        // }
        for (int i = 0; i < k; i++) {
            recoveryCoeffs[0][i] = decodeMatrix[failed_block_id * k + i];
            recoveryIds[i] = dmIds[i];
        }
    }
    // cout << "recoveryIds:" << recoveryIds << endl;
    // cout << "recoveryCoeffs:" << recoveryCoeffs << endl;
    std::vector<std::vector<int>> bitmatrix =
        matrix2Bitmatrix(recoveryCoeffs, w);

    for (int i = 0; i < k; i++) {
        std::vector<std::vector<int>> local_decode_matrix =
            get_submatrix(bitmatrix, i);
        unsigned int node_id = node_ids[recoveryIds[i]];
        Node helper_node = node_table_[node_id];
        DecodeRequest helper;
        helper.ip = helper_node.node_ip;
        helper.port = helper_node.node_port;
        helper.matrix = local_decode_matrix;

        helper.file_name = "stripe_" + std::to_string(stripe_id) + "_" +
                           std::to_string(recoveryIds[i]);

        repair_plan.helpers.push_back(helper);
    }
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