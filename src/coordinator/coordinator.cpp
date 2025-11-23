#include "coordinator.h"
#include "metadata.h"
namespace ECProject {

void Coordinator::write_to_datanode(const string &ip, int port,
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

        if (!finish) {
            std::cout << "[Coordinator" << self_cluster_id_ << "][SET]"
                      << " Set errors in datanodes!" << std::endl;
        } else {
#ifdef MY_DEBUG
            std::cout << "[Coordinator" << self_cluster_id_ << "][SET]"
                      << " Set " << key << " success! With length of "
                      << value_size << std::endl;
#endif
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
}

bool Coordinator::read_from_datanode_with_matrix(
    const string &ip, int port, const string &key, char *value,
    size_t value_size, const vector<vector<int>> &matrix) {
    bool res = true;
    try {
        std::string node_ip_port = ip + ":" + std::to_string(port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_get>(
                key, value_size, matrix));
#ifdef MY_DEBYG
        std::cout << "[Coordinator" << self_cluster_id_ << "][GET]"
                  << "Call datanode to handle get " << key << std::endl;
#endif
        asio::error_code ec;
        asio::ip::tcp::socket socket_(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        asio::error_code con_error;
        asio::connect(
            socket_,
            resolver.resolve({ip, std::to_string(port + SOCKET_PORT_OFFSET)}),
            con_error);

        asio::read(socket_, asio::buffer(value, value_size), ec);

        std::vector<unsigned char> finish = int_to_bytes(1);
        asio::write(socket_, asio::buffer(finish, finish.size()));
#ifdef MY_DEBYG
        std::cout << "[Coordinator" << self_cluster_id_ << "][GET]"
                  << "Read data from socket with length of " << value_size
                  << std::endl;
#endif
        asio::error_code ignore_ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
        socket_.close(ignore_ec);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    return res;
}

bool Coordinator::read_from_datanode(const string &ip, int port,
                                     const string &key, char *value,
                                     size_t value_size) {
    bool res = true;
    try {
        std::string node_ip_port = ip + ":" + std::to_string(port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_get>(key,
                                                                  value_size));
#ifdef MY_DEBYG
        std::cout << "[Coordinator" << self_cluster_id_ << "][GET]"
                  << "Call datanode to handle get " << key << std::endl;
#endif
        asio::error_code ec;
        asio::ip::tcp::socket socket_(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        asio::error_code con_error;
        asio::connect(
            socket_,
            resolver.resolve({ip, std::to_string(port + SOCKET_PORT_OFFSET)}),
            con_error);

        asio::read(socket_, asio::buffer(value, value_size), ec);

        std::vector<unsigned char> finish = int_to_bytes(1);
        asio::write(socket_, asio::buffer(finish, finish.size()));
#ifdef MY_DEBYG
        std::cout << "[Coordinator" << self_cluster_id_ << "][GET]"
                  << "Read data from socket with length of " << value_size
                  << std::endl;
#endif
        asio::error_code ignore_ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
        socket_.close(ignore_ec);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    return res;
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

        node_table_[node_id].node_id = node_id;
        node_table_[node_id].node_ip = uri.substr(0, pos);
        node_table_[node_id].node_port = std::stoi(uri.substr(pos + 1));
        ++node_id;
    }
    num_of_nodes_ = node_id;
}

Stripe &Coordinator::new_stripe(const string &key) {
    my_assert(ec != nullptr);
    Stripe temp;
    temp.stripe_id = cur_stripe_id_++;
    temp.key = key;
    temp.blocks2nodes = generateUniqueRandom(
        num_of_nodes_, ec_schema_.ec->k + ec_schema_.ec->m);
    stripe_table_[temp.stripe_id] = temp;

    for (int i = 0; i < temp.blocks2nodes.size(); i++) {
        int node_id = temp.blocks2nodes[i];
        node_table_[node_id].node2blocks[key] = i;
    }

    return temp;
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
        for (int j = 0; j < ec->k; j++) {
            data[j] = &object_value[j * cur_block_size];
        }
        for (int j = 0; j < ec->m; j++) {
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
            unsigned int node = stripe.blocks2nodes[j];
            std::string key = stripe.key + "block_" + j;
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
        for (auto j = 0; j < writers.size(); j++) {
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
        try {
            std::thread new_thread(encode_and_store);
            new_thread.detach();
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }
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
    ECSchema ec = ec_schmea_.ec;
    size_t block_size = ec_schema_.block_size;
    size_t packet_size = ec.schema_.packet_size;
    const int k = ec->k, m = ec->m, w = ec->w;
    const int blocks_num_per_stripe = k + m;
    std::unordered_map<unsigned int, std::vector<char>> original_datas;
    std::mutex original_lock;
    Stripe stripe = stripe_table_[key];
    std::vector<unsigned int> blocks2nodes = stripe.blocks2nodes;

    auto get_from_node = [this, &original_datas, &original_lock, packet_size,
                          stripe, key, decode_matrix](int block_id) mutable {
        Node node = stripe.blocks2nodes[block_id];
        std::vector<std::vector<int>> matrix =
            get_matrix(decode_matrix, block_id);
        if (matrix.size() > 0) {
            std::vector<char> tmp_val(matrix.size() * packet_size);
            bool res = read_from_datanode_with_matrix(
                node.ip, node.port, key + "block_" + block_id, tmp_val.data,
                tmp_val.size());
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
        write_to_datanode(node_ip, node_port, key + "block_" + block_id, data,
                          block_size);
    };

    std::vector<std::thread> readers;
    for (int i = 0; i < blocks_num_per_stripe; i++) {
        readers.push_back(std::thread(get_from_node, i));
    }

    for (int i = 0; i < num_of_original_blocks; i++) {
        readers[i].join();
    }

    // decode

    auto writer = std::thread(send_to_datanode, failed_id, decode_data,
                              new_node_ip, new_node_port, key);

    writer.join();
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

} // namespace ECProject