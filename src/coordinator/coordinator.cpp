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

Stripe &Coordinator::new_stripe(const string &key) {
    Stripe temp;
    temp.stripe_id = cur_stripe_id_++;
    temp.key = key;
    temp.blocks2nodes = generateUniqueRandom(
        num_of_nodes_, ec_schema_.ec->k + ec_schema_.ec->m);
    stripe_table_[key] = temp;

    for (size_t i = 0; i < temp.blocks2nodes.size(); i++) {
        int node_id = temp.blocks2nodes[i];
        node_table_[node_id].nodes2blocks[key] = i;
    }

    return stripe_table_[key];
}

void Coordinator::request_set(string key, size_t value_size) {
    if (commited_object_table_.contains(key)) {
        my_assert(false);
    }
    my_assert(value_size == ec_schema_.block_size * ec_schema_.ec->k);
    Stripe stripe = new_stripe(key);
    encode_and_store_object(stripe);
}

void Coordinator::encode_and_store_object(Stripe stripe) {
    auto encode_and_store = [this, stripe]() mutable {
        int k = ec_schema_.ec->k;
        int m = ec_schema_.ec->m;
        int num_of_blocks_each_stripe = k + m;
        asio::ip::tcp::socket socket_(io_context_);
        acceptor_.accept(socket_);
        size_t value_buf_size = k * ec_schema_.block_size;
        std::vector<char> key_buf((int)stripe.key.size());
        std::vector<char> value_buf(value_buf_size, 0);
        std::vector<unsigned char> size_buf(sizeof(int));

        asio::read(socket_, asio::buffer(size_buf.data(), size_buf.size()));
        int key_size = bytes_to_int(size_buf);
        my_assert(key_size == (int)stripe.key.size());

        asio::read(socket_, asio::buffer(size_buf.data(), size_buf.size()));
        int value_size = bytes_to_int(size_buf);
        my_assert(value_size == value_buf_size);

        size_t read_len_of_key =
            asio::read(socket_, asio::buffer(key_buf.data(), key_buf.size()));
        my_assert(read_len_of_key == key_buf.size());

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
            std::string key = stripe.key + "block_" + std::to_string(j);
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
RepairResp Coordinator::request_repair(std::string key,
                                       unsigned int failed_id) {
    RepairResp response;
    do_repair(key, failed_id, response);
    return response;
}
void Coordinator::do_repair(std::string key, unsigned int failed_id,
                            RepairResp &response) {
    std::vector<std::vector<int>> &decode_matrix =
        opt_decode_matrix_with_all_failed_mode_[failed_id];
    ErasureCode *ec = ec_schema_.ec;
    size_t block_size = ec_schema_.block_size;
    size_t packet_size = ec_schema_.packet_size;
    const int k = ec->k, m = ec->m, w = ec->w;
    const int blocks_num_per_stripe = k + m;
    std::unordered_map<unsigned int, std::vector<char>> original_datas;
    std::mutex original_lock;
    Stripe stripe = stripe_table_[key];
    std::vector<unsigned int> blocks2nodes = stripe.blocks2nodes;

    auto get_from_node = [this, &original_datas, &original_lock, packet_size,
                          stripe, key,
                          decode_matrix](unsigned int block_id) mutable {
        Node node = node_table_[stripe.blocks2nodes[block_id]];
        std::vector<std::vector<int>> matrix =
            get_matrix(decode_matrix, block_id);
        if (matrix.size() > 0) {
            std::vector<char> tmp_val(matrix.size() * packet_size);
            bool res = read_from_datanode_with_matrix(
                node.node_ip, node.node_port,
                key + "block_" + std::to_string(block_id), tmp_val.data(),
                tmp_val.size(), matrix);
            if (!res) {
                pthread_exit(NULL);
            }
            original_lock.lock();
            original_datas[block_id] = tmp_val;
            original_lock.unlock();
        }
    };

    auto send_to_datanode = [this, block_size](unsigned int block_id,
                                               char *data, std::string node_ip,
                                               int node_port, std::string key) {
        std::string block_id_str = std::to_string(block_id);
        write_to_datanode(node_ip, node_port,
                          key + "block_" + std::to_string(block_id), data,
                          block_size);
    };

    std::vector<std::thread> readers;
    for (int i = 0; i < blocks_num_per_stripe; i++) {
        readers.push_back(std::thread(get_from_node, i));
    }

    for (int i = 0; i < blocks_num_per_stripe; i++) {
        readers[i].join();
    }

    std::vector<char> decode_data(w * packet_size);
    std::fill_n(decode_data.data(), packet_size * w, 0);
    decode(original_datas, decode_matrix, decode_data);

    unsigned int candinate_node_id = select_node(blocks2nodes);

    auto writer = std::thread(send_to_datanode, failed_id, decode_data.data(),
                              node_table_[candinate_node_id].node_ip,
                              node_table_[candinate_node_id].node_port, key);
    writer.join();
    stripe.blocks2nodes[failed_id] = candinate_node_id;
}

std::vector<std::vector<int>>
Coordinator::get_matrix(const std::vector<std::vector<int>> &decode_matrix,
                        int i) {
    int w = decode_matrix.size(); // matrix 是 w × [(k+m)*w]
    int total_cols = decode_matrix[0].size();
    int blocks = total_cols / w; // 即 (k + m)
    my_assert(i >= 0 && i < blocks);

    std::vector<std::vector<int>> result;
    int start_col = i * w;
    for (int r = 0; r < w; ++r) {
        // 检查第 r 行、从 start_col 开始的 w 个元素是否全 0
        bool all_zero = true;
        for (int c = 0; c < w; ++c) {
            if (decode_matrix[r][start_col + c] != 0) {
                all_zero = false;
                break;
            }
        }
        if (!all_zero) {
            result.push_back(
                std::vector<int>(decode_matrix[r].begin() + start_col,
                                 decode_matrix[r].begin() + start_col + w));
        }
    }
    return result;
}

std::vector<std::vector<int>>
Coordinator::generate_repair_plan(const std::vector<std::vector<int>> &matrix) {
    int w = matrix.size();
    if (w == 0)
        return {};

    std::vector<std::vector<int>> repair_plan(w);
    std::vector<std::bitset<64>>
        basis; // 假设 w <= 64；若 w 更大，改用 vector<bool> 或动态 bitset
    std::vector<int> basis_idx;

    for (int i = 0; i < w; ++i) {
        // 跳过全零行
        bool all_zero = true;
        for (int j = 0; j < w; ++j) {
            if (matrix[i][j] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero)
            continue;

        // 构造当前行的 bitset
        std::bitset<64> row;
        for (int j = 0; j < w; ++j) {
            if (matrix[i][j])
                row.set(j);
        }

        // 高斯消元：尝试用已有基表示该行
        std::bitset<64> temp = row;
        std::vector<int> combo;
        for (size_t b = 0; b < basis.size(); ++b) {
            // 找最高位 1 对齐
            size_t lead = basis[b]._Find_first();
            if (temp.test(lead)) {
                temp ^= basis[b];
                combo.push_back(basis_idx[b]); // 记录参与异或的原始行号
            }
        }

        if (temp.none()) {
            // 可由前面行异或得到 → repair_plan[i] = combo（所有参与行）
            repair_plan[i] = combo;
        } else {
            // 线性无关 → 加入基；repair_plan[i] 保持为空（按题意）
            basis.push_back(temp);
            basis_idx.push_back(i);
            // repair_plan[i] 留空（vector 默认为空）
        }
    }
    return repair_plan;
}

void Coordinator::decode_xor(const std::vector<char> &original_data,
                             const std::vector<std::vector<int>> &matrix,
                             std::vector<char> &decode_data,
                             size_t packet_size) {
    auto repair_plan = generate_repair_plan(matrix);
    size_t w = repair_plan.size();
    assert(decode_data.size() == w * packet_size);

    // 每份 packet_size 字节
    for (size_t i = 0; i < w; ++i) {
        const auto &deps = repair_plan[i];
        if (deps.empty())
            continue; // 不处理：保持 decode_data[i]
                      // 原值（通常已含原始数据或待修复）

        char *out = decode_data.data() + i * packet_size;
        // // 初始化为 0，后续异或累积
        // std::fill_n(out, packet_size, 0);

        for (int idx : deps) {
            assert(idx >= 0 &&
                   static_cast<size_t>(idx) * packet_size + packet_size <=
                       original_data.size());
            const char *src = original_data.data() + idx * packet_size;
            for (size_t j = 0; j < packet_size; ++j) {
                out[j] ^= src[j];
            }
        }
    }
}

void Coordinator::decode(
    const std::unordered_map<unsigned int, std::vector<char>> &original_datas,
    const std::vector<std::vector<int>> &decode_matrix,
    std::vector<char> &decode_data) {
    ErasureCode *ec = ec_schema_.ec;
    size_t packet_size = ec_schema_.packet_size;
    const int k = ec->k, m = ec->m;
    const int blocks_num_per_stripe = k + m;
    for (int i = 0; i < blocks_num_per_stripe; i++) {
        auto matrix = get_matrix(decode_matrix, i);
        auto original_data = original_datas.find(i);
        if (original_data != original_datas.end()) {
            decode_xor(original_data->second, matrix, decode_data, packet_size);
        }
    }
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