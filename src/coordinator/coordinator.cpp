#include "coordinator.h"
#include "metadata.h"
#include <algorithm>
#include <random>
#include <vector>
namespace ECProject {

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

        node_table_[node_id].node_id = node_id;
        node_table_[node_id].node_ip = uri.substr(0, pos);
        node_table_[node_id].node_port = std::stoi(uri.substr(pos + 1));
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

unsigned int Coordinator::request_set(size_t value_size) {
    my_assert(value_size == ec_schema_.block_size * ec_schema_.ec->k);
    Stripe stripe = new_stripe();
    encode_and_store_object(stripe);
    return stripe.stripe_id;
}

bool Coordinator::write_to_datanode(const string &ip, int port,
                                    const string &key, char *value,
                                    size_t value_size) {
    try {
        std::string node_ip_port = ip + ":" + std::to_string(port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_set>(key,
                                                                  value_size));

        asio::error_code error;
        asio::ip::tcp::socket socket_(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        asio::error_code con_error;
        asio::connect(
            socket_,
            resolver.resolve({ip, std::to_string(port + SOCKET_PORT_OFFSET)}),
            con_error);
        if (!con_error) {
#ifdef MY_DEBYG
            std::cout << "Connect to " << ip << ":" << port + SOCKET_PORT_OFFSET
                      << " success!" << std::endl;
#endif
        }

        asio::write(socket_, asio::buffer(value, value_size));

        std::vector<unsigned char> finish_buf(sizeof(int));
        asio::read(socket_, asio::buffer(finish_buf, finish_buf.size()));
        int finish = bytes_to_int(finish_buf);

        asio::error_code ignore_ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
        socket_.close(ignore_ec);

        // if (!finish) {
        //     std::cout << "[DataNode" << self_cluster_id_ << "][SET]"
        //               << " Set errors in datanodes!" << std::endl;
        // } else {
#ifdef MY_DEBUG
        std::cout << "[DataNode" << self_cluster_id_ << "][SET]"
                  << " Set " << key << " success! With length of " << value_size
                  << std::endl;
#endif
        // }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    return true;
}

void Coordinator::encode_and_store_object(Stripe stripe) {
    auto encode_and_store = [this, stripe]() mutable {
        int k = ec_schema_.ec->k;
        int m = ec_schema_.ec->m;
        int num_of_blocks_each_stripe = k + m;
        asio::ip::tcp::socket socket_(io_context_);
        acceptor_.accept(socket_);
        size_t value_buf_size = k * ec_schema_.block_size;
        std::vector<char> value_buf(value_buf_size, 0);

        size_t read_len_of_value =
            asio::read(socket_, asio::buffer(value_buf.data(), value_buf_size));
        my_assert(read_len_of_value == value_buf_size);

        char *object_value = value_buf.data();
        std::vector<char *> data_v(k);
        std::vector<char *> coding_v(m);
        char **data = (char **)data_v.data();
        char **coding = (char **)coding_v.data();

        size_t cur_block_size = ec_schema_.block_size;
        my_assert(cur_block_size > 0);

        std::vector<std::vector<char>> space_for_parity_blocks(
            m, std::vector<char>(cur_block_size));
        for (int j = 0; j < k; j++) {
            data[j] = &object_value[j * cur_block_size];
        }
        for (int j = 0; j < m; j++) {
            coding[j] = space_for_parity_blocks[j].data();
        }
        double encoding_time = 0;
        struct timeval start_time, end_time;
        gettimeofday(&start_time, NULL);
        ec_schema_.ec->encode(data, coding, cur_block_size);
        gettimeofday(&end_time, NULL);
        encoding_time +=
            end_time.tv_sec - start_time.tv_sec +
            (end_time.tv_usec - start_time.tv_usec) * 1.0 / 1000000;
        std::vector<std::thread> writers;
        for (int j = 0; j < num_of_blocks_each_stripe; j++) {
            unsigned int node_id = stripe.blocks2nodes[j];
            auto node = node_table_[node_id];
            std::string key = "stripe" + std::to_string(stripe.stripe_id);
            writers.push_back(std::thread(
                [this, j, k, node, data, coding, cur_block_size, key]() {
                    if (j < k) {
                        write_to_datanode(node.node_ip, node.node_port, key,
                                          data[j], cur_block_size);
                    } else {
                        write_to_datanode(node.node_ip, node.node_port, key,
                                          coding[j - k], cur_block_size);
                    }
                }));
        }
        for (size_t j = 0; j < writers.size(); j++) {
            writers[j].join();
        }
        std::vector<unsigned char> finish = int_to_bytes(1);
        asio::write(socket_, asio::buffer(finish, finish.size()));

        std::vector<unsigned char> encoding_time_buf =
            double_to_bytes(encoding_time);
        asio::write(socket_,
                    asio::buffer(encoding_time_buf, encoding_time_buf.size()));

        asio::error_code ignore_ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
        socket_.close(ignore_ec);
    };
    try {
        std::thread new_thread(encode_and_store);
        new_thread.detach();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
}
RepairResp Coordinator::request_repair(Stripe &stripe,
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

RepairPlan Coordinator::generate_repair_plan(Stripe &stripe,
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