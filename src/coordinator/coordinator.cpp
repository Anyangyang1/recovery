#include "coordinator.h"
#include "erasure_code.h"
#include "metadata.h"
#include "scoped_timer.hpp"
#include "sggh.h"
#include "thread_pool.hpp"
#include <algorithm>
#include <random>
#include <vector>
namespace ECProject {

bool writeRepairRespToCSV(const std::vector<RepairResp> &responses,
                          const std::string &filename) {
    std::ofstream file(filename);
    if (!file.is_open())
        return false;

    // 写 header（可选但推荐）
    file << "read_disk_time,local_decode_time,send_to_net_time,read_data_time,"
            "computing_time,write_disk_time,repair_time\n";

    // 按行写：每行一个 RepairResp，按字段顺序输出
    for (const auto &r : responses) {
        file << r.read_from_disk_time << "," << r.local_decode_time << ","
             << r.send_to_net_time << "," << r.read_data_time << ','
             << r.computing_time << ',' // 若原字段名是 conputing_time，替换此处
             << r.write_disk_time << ',' << r.repair_time << '\n';
    }

    return file.good();
}

Coordinator::Coordinator(std::string ip, int port, std::string xml_path, int k,
                         int m, int w, size_t block_size, size_t io_thread_num)
    : ip_(ip), port_(port), xml_path_(xml_path),
      io_pool_(std::make_unique<ThreadPool>(io_thread_num)) {
    easylog::set_min_severity(easylog::Severity::WARNING);
    rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(RPC_NUM, port_);
    rpc_server_->register_handler<&Coordinator::request_set>(this);
    rpc_server_->register_handler<&Coordinator::request_get>(this);
    rpc_server_->register_handler<&Coordinator::request_repair>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_with_opt>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_no_local_decode>(
        this);
    rpc_server_->register_handler<&Coordinator::get_stripe_info>(this);
    rpc_server_->register_handler<&Coordinator::print_stripe_info>(this);
    rpc_server_->register_handler<&Coordinator::print_node_info>(this);
    rpc_server_->register_handler<&Coordinator::delete_failed_block>(this);
    rpc_server_->register_handler<&Coordinator::delete_node>(this);
    rpc_server_->register_handler<&Coordinator::clear>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_node>(this);
    rpc_server_->register_handler<&Coordinator::request_repair_node_with_opt>(
        this);
    rpc_server_->register_handler<&Coordinator::request_repair_node_con>(this);
    rpc_server_
        ->register_handler<&Coordinator::request_repair_node_with_opt_con>(
            this);
    rpc_server_
        ->register_handler<&Coordinator::request_repair_node_non_local_decode>(
            this);
    rpc_server_->register_handler<
        &Coordinator::request_repair_node_non_local_decode_con>(this);
    rpc_server_->register_handler<&Coordinator::clear_repair_file>(this);
    // rpc_server_->register_handler<&Coordinator::time_test>(this);

    cur_stripe_id_ = 0;
    try {
        init_cluster_info();
    } catch (const std::exception &e) {
        ELOG(ERROR) << "init_cluster_info failed: " << e.what();
        std::abort(); // 或 throw
    }

    ec_schema_.ec = std::make_unique<XORCode>(k, m, w);
    ec_schema_.block_size = block_size;
    ec_schema_.ec_type = XOR;

    SimilarityGreedy sg = SimilarityGreedy(k, m, w);
    opt_decode_matrix_with_all_failed_mode_ =
        sg.generateOptDecodeBitMatrixWithAllMode(-1);
    ELOG(WARNING) << "init completely...";
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
    if (io_pool_)
        io_pool_->stop(); // 确保任务完成或取消
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
            ELOG(ERROR) << "Failed to connect to " << uri << ": "
                        << ec.message();
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
        num_of_nodes_, ec_schema_.ec->k + ec_schema_.ec->m, temp.stripe_id);
    stripe_table_[temp.stripe_id] = temp;

    for (size_t i = 0; i < temp.blocks2nodes.size(); i++) {
        int node_id = temp.blocks2nodes[i];
        node_table_[node_id].nodes2blocks[temp.stripe_id] = i;
    }

    return stripe_table_[temp.stripe_id];
}

void Coordinator::print_stripe_info() {
    ELOG(WARNING) << "stripe info:";
    for (const auto &[key, value] : stripe_table_) {
        ELOG(WARNING) << "stripe_" << value.stripe_id << ": "
                      << vecToString(value.blocks2nodes);
    }
}

void Coordinator::print_node_info() {
    ELOG(WARNING) << "nodes info: ";
    for (const auto &[key, value] : node_table_) {
        ELOG(WARNING) << "node_" << value.node_id << ": "
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
    mutex_.lock();
    Stripe stripe = new_stripe();
    UploadInfo upload_info;
    unsigned int node0_id = stripe.blocks2nodes[0];
    upload_info.stripe_id = stripe.stripe_id;
    upload_info.node_ip = node_table_[node0_id].node_ip;
    upload_info.node_port = node_table_[node0_id].node_port;
    mutex_.unlock();
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
    try {
        {
            SCOPED_TIMER("repair_opt stripe_" + std::to_string(stripe_id) +
                         "_" + std::to_string(failed_block_id));
            auto result = async_simple::coro::syncAwait(
                datanodes_[node_ip_port]
                    ->call<&Datanode::do_repair_with_opt_isa>(
                        repair_plan.helpers, ec_schema_.block_size,
                        ec_schema_.ec->w, repair_plan.repair_file_name));
            if (!result) {
                // 处理 RPC 错误（强烈建议！）
                const auto &err = result.error();
                ELOG(ERROR) << "RPC failed for repair: " << err;
                // 可选：填充一个失败的 response
                response.repair_time = -1.0; // 标记失败
                return response;
            }

            response = std::move(result.value());
        }

        // alter_metadata(stripe_id, failed_block_id, new_node.node_id);
        ELOG(WARNING) << "select node_" << new_node.node_id
                      << " to repair stripe_" << stripe_id << "_"
                      << failed_block_id;
    } catch (const std::exception &e) {
        ELOG(ERROR) << e.what() << '\n';
    }
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
    {
        SCOPED_TIMER("repair " + repair_plan.repair_file_name);
        auto result = async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::do_repair>(
                repair_plan.helpers, ec_schema_.block_size, ec_schema_.ec->w,
                repair_plan.repair_file_name));

        if (!result) {
            // 处理 RPC 错误（强烈建议！）
            const auto &err = result.error();
            ELOG(ERROR) << "RPC failed for repair: " << err;
            // 可选：填充一个失败的 response
            response.repair_time = -1.0; // 标记失败
            return response;
        }

        response = std::move(result.value());
    }
    // alter_metadata(stripe_id, failed_block_id, new_node.node_id);
    ELOG(WARNING) << "select node_" << new_node.node_id << " to repair stripe_ "
                  << stripe_id << "_" << failed_block_id;
    return response;
}

RepairResp
Coordinator::request_repair_no_local_decode(unsigned int stripe_id,
                                            unsigned int failed_block_id) {
    const Stripe &stripe = stripe_table_[stripe_id];
    RepairResp response;
    RepairPlan repair_plan;

    repair_plan = generate_repair_plan(stripe, failed_block_id);

    Node new_node = repair_plan.selected_new_node;
    std::string node_ip_port =
        new_node.node_ip + ":" + std::to_string(new_node.node_port);

    {
        SCOPED_TIMER("repair " + repair_plan.repair_file_name);
        auto result = async_simple::coro::syncAwait(
            datanodes_[node_ip_port]
                ->call<&Datanode::do_repair_no_local_decode>(
                    repair_plan.helpers, ec_schema_.block_size,
                    ec_schema_.ec->w, repair_plan.repair_file_name));
        if (!result) {
            // 处理 RPC 错误（强烈建议！）
            const auto &err = result.error();
            ELOG(ERROR) << "RPC failed for repair: " << err;
            // 可选：填充一个失败的 response
            response.repair_time = -1.0; // 标记失败
            return response;
        }

        response = std::move(result.value());
    }

    // alter_metadata(stripe_id, failed_block_id, new_node.node_id);
    ELOG(WARNING) << "select node_" << new_node.node_id << " to repai stripe_"
                  << stripe_id << "_" << failed_block_id;
    return response;
}

double Coordinator::request_repair_node(unsigned int node_id) {
    Node node = node_table_[node_id];
    std::vector<RepairResp> responses;
    double time;

    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&time](double ms) { time = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            responses.emplace_back(request_repair(stripe_id, block_id));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    writeRepairRespToCSV(
        responses, append_timestamp_to_filename(
                       "/home/anyangyang/study/recovery/data/repair_" + std::to_string(ec_schema_.ec->k) +
                       "_" + std::to_string(ec_schema_.ec->m) + "_" +
                       std::to_string(ec_schema_.ec->w) + ".csv"));
    return time;
}

double Coordinator::request_repair_node_non_local_decode(unsigned int node_id) {
    Node node = node_table_[node_id];
    std::vector<RepairResp> responses;
    double time;
    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&time](double ms) { time = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            responses.emplace_back(
                request_repair_no_local_decode(stripe_id, block_id));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    writeRepairRespToCSV(
        responses, append_timestamp_to_filename(
                       "/home/anyangyang/study/recovery/data/non_local_" + std::to_string(ec_schema_.ec->k) +
                       "_" + std::to_string(ec_schema_.ec->m) + "_" +
                       std::to_string(ec_schema_.ec->w) + ".csv"));
    return time;
}

double Coordinator::request_repair_node_with_opt(unsigned int node_id) {
    Node node = node_table_[node_id];
    std::vector<RepairResp> responses;
    double time;
    std::vector<double> timings;
    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&time](double ms) { time = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            responses.emplace_back(
                request_repair_with_opt(stripe_id, block_id));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    writeRepairRespToCSV(
        responses, append_timestamp_to_filename(
                       "/home/anyangyang/study/recovery/data/opt_" + std::to_string(ec_schema_.ec->k) +
                       "_" + std::to_string(ec_schema_.ec->m) + "_" +
                       std::to_string(ec_schema_.ec->w) + ".csv"));
    return time;
}

double Coordinator::request_repair_node_con(unsigned int node_id) {
    // Node node = node_table_[node_id];
    double response;
    auto it = node_table_.find(node_id);
    if (it == node_table_.end()) {
        ELOG(ERROR) << "Node " << node_id << " not found";
        return response;
    }
    const Node &node = it->second;
    std::vector<std::future<RepairResp>> futures;
    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&response](double ms) { response = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            futures.push_back(io_pool_->submit([this, stripe_id, block_id]() {
                return request_repair(stripe_id, block_id);
            }));
        }
        std::exception_ptr first_exception = nullptr;
        for (auto &fut : futures) {
            try {
                fut.get();
            } catch (...) {
                if (!first_exception)
                    first_exception = std::current_exception();
            }
        }
        if (first_exception)
            std::rethrow_exception(first_exception);
    }

    for (const auto &[node_id, node] : node_table_) {
        std::string node_ip_port =
            node.node_ip + ":" + std::to_string(node.node_port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]
                ->call<&Datanode::print_download_data_packet_num>());
    }
    return response;
}

double Coordinator::request_repair_node_with_opt_con(unsigned int node_id) {
    double response;
    auto it = node_table_.find(node_id);
    if (it == node_table_.end()) {
        ELOG(ERROR) << "Node " << node_id << " not found";
        return response;
    }
    const Node &node = it->second;
    std::vector<std::future<RepairResp>> futures;
    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&response](double ms) { response = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            futures.push_back(io_pool_->submit([this, stripe_id, block_id]() {
                return request_repair_with_opt(stripe_id, block_id);
            }));
        }
        std::exception_ptr first_exception = nullptr;
        for (auto &fut : futures) {
            try {
                fut.get();
            } catch (...) {
                if (!first_exception)
                    first_exception = std::current_exception();
            }
        }
        if (first_exception)
            std::rethrow_exception(first_exception);
    }
    for (const auto &[node_id, node] : node_table_) {
        std::string node_ip_port =
            node.node_ip + ":" + std::to_string(node.node_port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]
                ->call<&Datanode::print_download_data_packet_num>());
    }
    return response;
}

double
Coordinator::request_repair_node_non_local_decode_con(unsigned int node_id) {
    // Node node = node_table_[node_id];
    double response;
    auto it = node_table_.find(node_id);
    if (it == node_table_.end()) {
        ELOG(ERROR) << "Node " << node_id << " not found";
        return response;
    }
    const Node &node = it->second;
    std::vector<std::future<RepairResp>> futures;
    {
        SCOPED_TIMER_WITH_CB("repair node_" + std::to_string(node_id),
                             [&response](double ms) { response = ms; });
        for (const auto &[stripe_id, block_id] : node.nodes2blocks) {
            futures.push_back(io_pool_->submit([this, stripe_id, block_id]() {
                return request_repair_no_local_decode(stripe_id, block_id);
            }));
        }
        std::exception_ptr first_exception = nullptr;
        for (auto &fut : futures) {
            try {
                fut.get();
            } catch (...) {
                if (!first_exception)
                    first_exception = std::current_exception();
            }
        }
        if (first_exception)
            std::rethrow_exception(first_exception);
    }

    for (const auto &[node_id, node] : node_table_) {
        std::string node_ip_port =
            node.node_ip + ":" + std::to_string(node.node_port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]
                ->call<&Datanode::print_download_data_packet_num>());
    }
    return response;
}

void Coordinator::alter_metadata(unsigned int stripe_id,
                                 unsigned int failed_block_id,
                                 unsigned int new_node_id) {
    mutex_.lock();
    unsigned int old_node_id =
        stripe_table_[stripe_id].blocks2nodes[failed_block_id];
    stripe_table_[stripe_id].blocks2nodes[failed_block_id] = new_node_id;
    node_table_[old_node_id].nodes2blocks.erase(stripe_id);
    node_table_[new_node_id].nodes2blocks[stripe_id] = failed_block_id;
    mutex_.unlock();
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
void Coordinator::delete_node(unsigned int node_id) {
    Node &node = node_table_[node_id];
    std::string node_ip_port =
        node.node_ip + ":" + std::to_string(node.node_port);
    async_simple::coro::syncAwait(
        datanodes_[node_ip_port]->call<&Datanode::handle_delete_all_file>());
}

void Coordinator::clear() {
    for (int i = 0; i < num_of_nodes_; ++i) {
        delete_node(i);
    }
    stripe_table_.clear();
    cur_stripe_id_ = 0;
    for (auto &[node_id, node] : node_table_) {
        node.nodes2blocks.clear();
    }
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
    /*
        测试使用，之后删除
    */
    mutex_.lock();
    repair_file_placement_[new_node_id].push_back({stripe_id, failed_block_id});
    mutex_.unlock();
    /*
        测试使用，之后删除
    */

    return repair_plan;
}

RepairPlan Coordinator::generate_repair_plan(const Stripe &stripe,
                                             unsigned int failed_block_id) {
    const int k = ec_schema_.ec->k;
    const int m = ec_schema_.ec->m;
    const int w = ec_schema_.ec->w;
    std::vector<std::vector<int>> codingMatrix =
        ErasureCode::cauchy_original_coding_matrix_vector(k, m, w);
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

    /*
        测试使用，之后删除
    */
    mutex_.lock();
    repair_file_placement_[new_node_id].push_back({stripe_id, failed_block_id});
    mutex_.unlock();
    /*
        测试使用，之后删除
    */

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
        ErasureCode::matrix2Bitmatrix(recoveryCoeffs, w);

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

unsigned int Coordinator::select_node(
    const std::vector<unsigned int> &block2node,
    std::optional<unsigned int> seed /* = std::nullopt */) {
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
        return static_cast<unsigned int>(-1); // 或 throw/LOG_FATAL，依策略而定
    }

    // 使用局部 RNG，避免 static 状态污染与线程竞争
    std::mt19937 gen;
    if (seed.has_value()) {
        gen.seed(seed.value());
    } else {
        // 无种子时使用随机设备初始化（每次调用独立，非 static）
        static std::random_device rd; // rd 只用于初始化，可 static
        gen.seed(rd());
    }

    std::uniform_int_distribution<int> dis(
        0, static_cast<int>(candidates.size() - 1));
    return static_cast<unsigned int>(candidates[dis(gen)]);
}
void Coordinator::clear_repair_file() {
    for (auto &[node_id, stripes] : repair_file_placement_) {
        auto node = node_table_[node_id];
        std::string node_ip_port =
            node.node_ip + ":" + std::to_string(node.node_port);
        for (auto &stripe : stripes) {
            unsigned int stripe_id = stripe.first;
            unsigned int block_id = stripe.second;
            async_simple::coro::syncAwait(
                datanodes_[node_ip_port]->call<&Datanode::handle_delete_stripe>(
                    stripe_id, block_id));
        }
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_clear_time>());
    }
    repair_file_placement_.clear();
}

// void Coordinator::set(std::string &value) {
//     auto response = request_set(value.size());
//     // Step 2: 直连 datanode data port (NO RPC!)
//     int data_port = response.node_port + SOCKET_PORT_OFFSET;
//     ELOG(WARNING) << "[SET] Sending stripe_" << response.stripe_id << " ("
//                   << value.size() << "B) to " << response.node_ip << ":"
//                   << data_port;

//     try {
//         asio::ip::tcp::socket socket(io_context_);
//         socket.connect(asio::ip::tcp::endpoint(
//             asio::ip::make_address(response.node_ip), data_port));

//         // 发 header: op + stripe_id + size
//         uint8_t op = static_cast<uint8_t>(DataOp::UPLOAD);
//         asio::write(socket, asio::buffer(&op, 1));
//         uint32_t sid = htonl(response.stripe_id);
//         uint32_t sz = htonl(static_cast<uint32_t>(value.size()));
//         asio::write(socket, asio::buffer(&sid, 4));
//         asio::write(socket, asio::buffer(&sz, 4));

//         // 发 body
//         asio::write(socket, asio::buffer(value));
//         socket.close();
//         ELOG(WARNING) << "Send data completely.";
//     } catch (const std::exception &e) {
//         ELOG(ERROR) << "[SET] failed: " << e.what();
//     }
// }

// void Coordinator::set_stripe(unsigned int stripe_num, const int value_size) {
//     for (unsigned int i = 0; i < stripe_num; i++) {
//         std::string value = generate_random_string(value_size);
//         set(value);
//     }
// }

// void Coordinator::time_test() {
//     std::vector<int> TEST_K{4, 6, 8};
//     std::vector<int> TEST_M{3, 4};
//     std::vector<int> TEST_W{8};
//     std::vector<int> STRIPE_NUM{200, 300, 500};
//     std::unordered_map<std::string, double> time_map;
//     for (auto w : TEST_W) {
//         for (auto m : TEST_M) {
//             for (auto k : TEST_K) {
//                 ec_schema_.ec = std::make_unique<XORCode>(k, m, w);
//                 SimilarityGreedy sg = SimilarityGreedy(k, m, w);
//                 opt_decode_matrix_with_all_failed_mode_ =
//                     sg.generateOptDecodeBitMatrixWithAllMode(0);
//                 const int size = k * BLOCK_SIZE;
//                 clear();
//                 for (auto stripe : STRIPE_NUM) {
//                     std::string key_prefix =
//                         std::to_string(k) + "-" + std::to_string(m) + "-" +
//                         std::to_string(w) + "-" + std::to_string(stripe) +
//                         "-";
//                     set_stripe(stripe, size);
//                     auto no_local_time =
//                         request_repair_node_non_local_decode_con(0).repair_time;
//                     time_map[key_prefix + "no_local"] = no_local_time;
//                     clear_repair_file();ian

//                     auto time = request_repair_node_con(0).repair_time;
//                     time_map[key_prefix + "time"] = time;
//                     clear_repair_file();

//                     auto opt_time =
//                         request_repair_node_with_opt_con(0).repair_time;
//                     time_map[key_prefix + "opt"] = opt_time;
//                     clear_repair_file();
//                 }
//             }
//         }
//     }
//     for (auto &[key, time] : time_map) {
//         ELOG(ERROR) << key << "-" << time;
//     }
// }

} // namespace ECProject