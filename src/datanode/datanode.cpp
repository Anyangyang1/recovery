#include "datanode.h"

namespace ECProject {
Datanode::Datanode(std::string ip, int port)
    : ip_(ip), port_(port), port_for_transfer_data_(port + SOCKET_PORT_OFFSET),
      acceptor_(io_context_, asio::ip::tcp::endpoint(
                                 asio::ip::address::from_string(ip.c_str()),
                                 port_for_transfer_data_)) {
    easylog::set_min_severity(easylog::Severity::ERROR);
    // port is for rpc
    rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(1, port_);
    // rpc_server_->register_handler<&Datanode::checkalive>(this);
    rpc_server_->register_handler<&Datanode::handle_set>(this);
    rpc_server_->register_handler<&Datanode::handle_get_original>(this);
    rpc_server_->register_handler<&Datanode::handle_get_local_decode>(this);
    // rpc_server_->register_handler<&Datanode::handle_delete>(this);
    // rpc_server_->register_handler<&Datanode::handle_transfer>(this);

    std::string targetdir = "./storage/";
    if (access(targetdir.c_str(), 0) == -1) {
        mkdir(targetdir.c_str(), S_IRWXU);
    }
}

Datanode::~Datanode() {
    acceptor_.close();
    rpc_server_->stop();
}

void Datanode::run() { auto err = rpc_server_->start(); }

void Datanode::handle_set(const std::string &key, size_t value_size) {
    auto handler = [this, key, value_size]() mutable {
        try {
            asio::error_code ec;
            asio::ip::tcp::socket socket_(io_context_);
            acceptor_.accept(socket_);

            std::string value_buf(value_size, 0);

            asio::read(socket_,
                       asio::buffer(value_buf.data(), value_buf.size()), ec);

            bool ret = store_data(key, value_buf.data(), value_size);

            if (ret) { // response
                std::vector<unsigned char> finish = int_to_bytes(1);
                asio::write(socket_, asio::buffer(finish, finish.size()));
            } else {
                std::vector<unsigned char> finish = int_to_bytes(0);
                asio::write(socket_, asio::buffer(finish, finish.size()));
            }

            asio::error_code ignore_ec;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
            socket_.close(ignore_ec);

#ifdef MY_DEBUG
            std::cout << "[Datanode" << port_ << "][Write] successfully write "
                      << key << " with " << value_size << "bytes" << std::endl;
#endif

        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }
    };
}

// std::shared_ptr<coro_rpc::coro_rpc_client> get_rpc_client(const std::string &ip,
//                                                           int port) {
//     std::string key = ip + ":" + std::to_string(port);

//     auto it = datanodes_.find(key);
//     if (it == datanodes_.end()) {
//         auto client = std::make_shared<coro_rpc::coro_rpc_client>();
//         async_simple::coro::syncAwait(client->connect(ip, port));
//         // 线程不安全时用以下方式插入：
//         it = datanodes_.emplace(std::move(key), client).first;
//     }
//     return it->second;
// }
// 其中 datanodes_ 类型应为：
// std::unordered_map<std::string, std::shared_ptr<coro_rpc::coro_rpc_client>>
// datanodes_;

void Datanode::handle_get_local_decode(const std::string &key,
                                       size_t value_size,
                                       const vector<vector<int>> &matrix) {
    auto handler = [this, key, value_size, matrix]() mutable {
        asio::error_code ec;
        asio::ip::tcp::socket socket_(io_context_);
        acceptor_.accept(socket_);

        size_t need_packets = matrix.size();
        size_t packet_size = value_size / need_packets;
        size_t w = matrix[0].size();

        std::string data_buf(w * packet_size, 0);
        bool ret = access_data(key, data_buf.data(), packet_size * w);

        std::string decode_buf(value_size, 0);
        local_decode(matrix, data_buf.data(), decode_buf.data(), packet_size);

        asio::write(socket_,
                    asio::buffer(decode_buf.data(), decode_buf.size()));
        std::vector<unsigned char> finish_buf(sizeof(int));
        asio::read(socket_, asio::buffer(finish_buf, finish_buf.size()));
        int finish = bytes_to_int(finish_buf);
        if (!finish) {
            std::cout << "[Datanode" << port_
                      << "][GET] destination set failed!" << std::endl;
            asio::error_code ignore_ec;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
            socket_.close(ignore_ec);
#ifdef MY_DEBUG
            std::cout << "[Datanode" << port_ << "][GET] write to socket!"
                      << std::endl;
#endif
        }
    };
    try {
#ifdef MY_DEBUG
        std::cout << "[Datanode" << port_ << "][GET] ready to handle get!"
                  << std::endl;
#endif
        std::thread my_thread(handler);
        my_thread.detach();
    } catch (std::exception &e) {
        std::cout << "exception" << std::endl;
        std::cout << e.what() << std::endl;
    }
}

void Datanode::handle_get_original(const std::string &key, size_t value_size) {
    auto handler = [this, key, value_size]() mutable {
        asio::error_code ec;
        asio::ip::tcp::socket socket_(io_context_);
        acceptor_.accept(socket_);

        std::string data_buf(value_size, 0);
        bool ret = access_data(key, data_buf.data(), value_size);

        asio::write(socket_, asio::buffer(data_buf.data(), data_buf.size()));
        std::vector<unsigned char> finish_buf(sizeof(int));
        asio::read(socket_, asio::buffer(finish_buf, finish_buf.size()));
        int finish = bytes_to_int(finish_buf);
        if (!finish) {
            std::cout << "[Datanode" << port_
                      << "][GET] destination set failed!" << std::endl;
            asio::error_code ignore_ec;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
            socket_.close(ignore_ec);
#ifdef MY_DEBUG
            std::cout << "[Datanode" << port_ << "][GET] write to socket!"
                      << std::endl;
#endif
        }
    };
    try {
#ifdef MY_DEBUG
        std::cout << "[Datanode" << port_ << "][GET] ready to handle get!"
                  << std::endl;
#endif
        std::thread my_thread(handler);
        my_thread.detach();
    } catch (std::exception &e) {
        std::cout << "exception" << std::endl;
        std::cout << e.what() << std::endl;
    }
}

bool Datanode::access_data(const std::string &key, char *value_buf,
                           size_t value_size) {
    std::string readpath = "./storage/" + key;
    if (access(readpath.c_str(), 0) == -1) {
        std::cout << "[Datanode" << port_ << "][Disk][Get] file does not exist!"
                  << readpath << std::endl;
        return false;
    } else {
        std::ifstream ifs(readpath, std::ios::binary);
        ifs.read(value_buf, static_cast<std::streamsize>(value_size));
        if (!ifs || ifs.gcount() != static_cast<std::streamsize>(value_size)) {
            return false;
        }
        ifs.close();
    }
    return true;
}

bool Datanode::access_data(const std::string &key, char *value_buf,
                           const vector<int> &idxs) {
    std::string targetdir = "./storage/" + std::to_string(port_) + "/";
    std::string readpath = targetdir + key;
    std::ifstream file(readpath, std::ios::binary);
    if (!file)
        return false;

    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0);

    size_t n = idxs.size();
    if (n == 0)
        return true;
    if (file_size % n != 0)
        return false; // 不满足“刚好整除”前提 → 错误

    std::streamsize packet_size = file_size / n;
    char *out = value_buf;

    for (size_t i = 0; i < n; ++i) {
        if (idxs[i] != 1)
            continue;

        file.seekg(i * packet_size);
        file.read(out, packet_size);
        if (!file)
            return false;
        out += packet_size;
    }
    return true;
}

bool Datanode::store_data(const std::string &key, const char *value,
                          size_t value_size) {
    std::string targetdir = "./storage/" + std::to_string(port_) + "/";
    std::string writepath = targetdir + key;
    if (access(targetdir.c_str(), 0) == -1) {
        mkdir(targetdir.c_str(), S_IRWXU);
    }
    std::ofstream ofs(writepath,
                      std::ios::binary | std::ios::out | std::ios::trunc);
    ofs.write(value, value_size);
    ofs.flush();
    ofs.close();
    if (!ofs) {
        std::cout << "[Datanode" << port_ << "][Disk] failed to set!\n";
        return false;
    }
    return true;
}

void Datanode::local_decode(const std::vector<std::vector<int>> &matrix,
                            char *data_buf, char *decode_buf,
                            size_t packet_size) {

    size_t need_packet = matrix.size();
    size_t w = matrix[0].size();
    if (w == 0 || packet_size == 0)
        return;

    char *out = decode_buf;

    for (size_t i = 0; i < need_packet; ++i) {
        // 初始化输出块为全0（异或起点）
        std::memset(out, 0, packet_size);

        // 按列异或：若 matrix[i][j]==1，则 data_buf + j*packet_size 异或入 out
        for (size_t j = 0; j < w; ++j) {
            if (matrix[i][j]) {
                char *src = data_buf + j * packet_size;
                galois_region_xor(out, src, out, packet_size);
            }
        }

        out += packet_size; // 移到下一块输出位置
    }
}



bool Datanode::read_from_datanode_with_local_decode(
    const string &ip, int port, const string &key, char *value,
    size_t value_size, const vector<vector<int>> &matrix) {
    bool res = true;
    try {
        std::unique_ptr<coro_rpc::coro_rpc_client> client = std::make_unique<coro_rpc::coro_rpc_client>();
        async_simple::coro::syncAwait(
            client->connect(ip, std::to_string(port)));
        async_simple::coro::syncAwait(
            client->call<&Datanode::handle_get_local_decode>(
                key, value_size, matrix));
#ifdef MY_DEBYG
        std::cout << "[DataNode" << self_cluster_id_ << "][GET]"
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
#ifdef MY_DEBYG
        std::cout << "[DataNode" << self_cluster_id_ << "][GET]"
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

bool Datanode::read_from_datanode(const string &ip, int port, const string &key,
                                  char *value, size_t value_size) {
    bool res = true;
    try {
        std::unique_ptr<coro_rpc::coro_rpc_client> client = std::make_unique<coro_rpc::coro_rpc_client>();
        async_simple::coro::syncAwait(
            client->connect(ip, std::to_string(port)));
        async_simple::coro::syncAwait(
            client->call<&Datanode::handle_get_original>(
                key, value_size));
#ifdef MY_DEBYG
        std::cout << "[DataNode" << self_cluster_id_ << "][GET]"
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
        std::cout << "[DataNode" << self_cluster_id_ << "][GET]"
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


void Datanode::do_repair(unsigned int stripe_id,
                         std::vector<DecodeRequest> helpers, size_t block_size,
                         int w) {
    assert(block_size % w == 0);
    std::string file_name = "stripe" + std::to_string(stripe_id);
    size_t packet_size = block_size / w;

    std::vector<std::vector<char>> original_datas(
        helpers.size(), std::vector<char>(block_size));

    auto get_from_node = [&](int idx, const DecodeRequest &helper) {
        auto result = compute_basis_gf2_indices(helper.matrix);
        size_t buf_size = packet_size * result.basis.size();
        std::vector<char> buf(buf_size);

        bool ok = read_from_datanode_with_local_decode(helper.ip, helper.port,
                                                       file_name, buf.data(),
                                                       buf_size, result.basis);
        if (!ok) {
            throw std::runtime_error("Read failed from " + helper.ip);
        }

        compute_original_data(buf.data(), result.reps,
                              original_datas[idx].data(), packet_size);
    };

    std::vector<std::thread> readers;
    std::vector<std::exception_ptr> exceptions(helpers.size());

    for (size_t i = 0; i < helpers.size(); ++i) {
        readers.emplace_back([&, i]() {
            try {
                get_from_node(i, helpers[i]);
            } catch (...) {
                exceptions[i] = std::current_exception();
            }
        });
    }

    for (auto &t : readers)
        t.join();

    for (auto &e : exceptions) {
        if (e)
            std::rethrow_exception(e);
    }

    auto decode_data = decode_xor(original_datas);
    assert(decode_data.size() == block_size);
    store_data(file_name, decode_data.data(), decode_data.size());
}


std::vector<char>
Datanode::decode_xor(const std::vector<std::vector<char>> &original_datas) {
    if (original_datas.empty())
        return {};

    size_t len = original_datas[0].size();
    for (const auto &d : original_datas) {
        assert(d.size() == len && "All data blocks must have same length");
    }

    std::vector<char> result(len, 0);
    for (const auto &block : original_datas) {
        for (size_t i = 0; i < len; ++i) {
            result[i] ^= block[i];
        }
    }
    return result;
}

void Datanode::compute_original_data(const char *buf,
                                     const std::vector<std::vector<int>> &reps,
                                     char *original_data, size_t packet_size) {

    if (reps.empty() || packet_size == 0)
        return;
    // 缓存：key = 排序后的索引列表，value = 已计算结果的起始地址（在
    // original_data 中）
    std::unordered_map<std::vector<int>, const char *, VecIntHash> cache;

    for (size_t i = 0; i < reps.size(); ++i) {
        auto indices = reps[i];
        std::sort(indices.begin(), indices.end()); // 保证 {0,1} ≡ {1,0}

        auto it = cache.find(indices);
        char *dst = original_data + i * packet_size;

        if (it != cache.end()) {
            // 命中：直接 memcpy
            std::memcpy(dst, it->second, packet_size);
        } else {
            // 未命中：计算
            if (indices.empty()) {
                std::memset(dst, 0, packet_size);
            } else {
                // 初始化为第一个块
                std::memcpy(dst, buf + indices[0] * packet_size, packet_size);
                // 异或其余块
                for (size_t k = 1; k < indices.size(); ++k) {
                    const char *src = buf + indices[k] * packet_size;
                    for (size_t j = 0; j < packet_size; ++j) {
                        dst[j] ^= src[j];
                    }
                }
            }
            cache[indices] = dst; // 缓存当前结果地址（生命周期安全：dst 在
                                  // original_data 内）
        }
    }
}

GF2BasisResult
Datanode::compute_basis_gf2_indices(const std::vector<std::vector<int>> &A) {
    int w = static_cast<int>(A.size());
    if (w == 0 || A[0].size() != static_cast<size_t>(w)) {
        return {};
    }

    // Step 1: 高斯消元求 RREF 和主元映射
    auto R = A;
    std::vector<int> pivot_row_for_col(
        w, -1); // col c → 哪一行是其主元行（在 R 中的行号）
    int rank = 0;

    for (int c = 0; c < w && rank < w; ++c) {
        // 找主元行
        int pivot = -1;
        for (int r = rank; r < w; ++r) {
            if (R[r][c] == 1) {
                pivot = r;
                break;
            }
        }
        if (pivot == -1)
            continue;

        std::swap(R[rank], R[pivot]);
        pivot_row_for_col[c] = rank;

        // 消去其他所有行的第 c 列
        for (int r = 0; r < w; ++r) {
            if (r != rank && R[r][c] == 1) {
                for (int j = 0; j < w; ++j) {
                    R[r][j] ^= R[rank][j];
                }
            }
        }
        ++rank;
    }

    // 提取基（前 rank 行）
    std::vector<std::vector<int>> basis(R.begin(), R.begin() + rank);

    // Step 2: 对每个原始行 A[i]，求其由哪些基向量异或而成
    std::vector<std::vector<int>> reps(w);

    for (int i = 0; i < w; ++i) {
        auto v = A[i]; // 当前待表出行

        // 遍历所有主元列（按列递增顺序 = 基向量顺序）
        for (int c = 0; c < w; ++c) {
            if (v[c] == 1 && pivot_row_for_col[c] != -1) {
                int basis_idx =
                    pivot_row_for_col[c]; // 该主元对应的基索引（0 ~ rank-1）
                reps[i].push_back(basis_idx);

                // v ^= basis[basis_idx]
                const auto &b = basis[basis_idx];
                for (int j = 0; j < w; ++j) {
                    v[j] ^= b[j];
                }
            }
        }
        // 理论上 v 应全 0；可加 assert 检查
    }

    return {basis, reps};
}

} // namespace ECProject