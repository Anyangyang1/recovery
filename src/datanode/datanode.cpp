#include "datanode.h"
#include "coordinator.h"
namespace ECProject {
Datanode::Datanode(std::string ip, int port)
    : ip_(ip), port_(port), port_for_transfer_data_(port + SOCKET_PORT_OFFSET),
      coordinator_ip_("192.168.1.12"), coordinator_port_(COORDINATOR_PORT),
      acceptor_(io_context_, asio::ip::tcp::endpoint(
                                 asio::ip::address::from_string(ip.c_str()),
                                 port_for_transfer_data_)) {
    easylog::set_min_severity(easylog::Severity::DEBUG);
    // port is for rpc
    rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(4, port_);
    rpc_server_->register_handler<&Datanode::handle_set>(this);
    rpc_server_->register_handler<&Datanode::handle_get>(this);
    rpc_server_->register_handler<&Datanode::handle_get_with_local_decode>(
        this);
    rpc_server_->register_handler<&Datanode::handle_upload>(this);

    std::string targetdir = "./storage/";
    if (access(targetdir.c_str(), 0) == -1) {
        mkdir(targetdir.c_str(), S_IRWXU);
    }
}

Datanode::~Datanode() {
    acceptor_.close();
    rpc_server_->stop();
}

void Datanode::run() { auto ret = rpc_server_->start(); }

void Datanode::handle_set(const std::string &key, size_t value_size) {
    auto handler = [this, key, value_size]() mutable {
        try {
            asio::ip::tcp::socket socket(io_context_);
            acceptor_.accept(socket);

            auto value_buf = std::make_unique<char[]>(value_size);
            asio::read(socket, asio::buffer(value_buf.get(), value_size));

            bool ret = store_data(key, value_buf.get(), value_size);
            if (!ret) {
                ELOG(ERROR) << "store_data error";
            }
            socket.close();

        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }
    };
    std::thread(handler).detach();
}

void Datanode::handle_get(const std::string &key, size_t value_size) {
    auto handler = [this, key, value_size]() mutable {
        try {
            asio::ip::tcp::socket socket(io_context_);
            acceptor_.accept(socket);

            auto data_buf = std::make_unique<char[]>(value_size);
            bool ret = access_data(key, data_buf.get(), value_size);
            if (!ret) {
                ELOG(ERROR) << "access_data error";
            }

            asio::write(socket, asio::buffer(data_buf.get(), value_size));
            socket.close();
        } catch (std::exception &e) {
            std::cout << "exception" << std::endl;
            std::cout << e.what() << std::endl;
        }
    };
    std::thread(handler).detach();
}

// ====== Datanode::handle_upload (重构后) ======
void Datanode::handle_upload(unsigned int stripe_id, size_t value_size) {
    ELOG(DEBUG) << "[RPC] handle_upload(" << stripe_id << ", " << value_size
                << ")";
    // === Step 1: 读取数据 ===
    auto handler = [this, stripe_id, value_size]() mutable {
        try {
            asio::ip::tcp::socket socket(io_context_);
            std::error_code ec;
            // Sync accept — safe in dedicated RPC thread
            ELOG(DEBUG) << "prepare reveive data...";
            acceptor_.accept(socket, ec);
            if (ec) {
                ELOG(ERROR) << "Accept failed: " << ec.message();
                return;
            }
            ELOG(DEBUG) << "start reveive data...";
            // Sync read
            auto value_buf = std::make_unique<char[]>(value_size);
            size_t n = asio::read(
                socket, asio::buffer(value_buf.get(), value_size), ec);
            socket.close();
            if (ec || n != value_size) {
                ELOG(DEBUG) << "reveive data... error";
                return;
            }
            std::string value_str(value_buf.get(), value_size);
            ELOG(DEBUG) << "receive data: " << value_str;

            // === Step 2: 同步获取元数据 ===
            auto client = std::make_unique<coro_rpc::coro_rpc_client>();
            auto conn_res = async_simple::coro::syncAwait(client->connect(
                coordinator_ip_, std::to_string(coordinator_port_)));
            if (conn_res) {
                ELOG(ERROR)
                    << "Failed to connect coordinator RPC (" << coordinator_ip_
                    << ":" << coordinator_port_ << "): " << conn_res.message();
            }
            StripeInfo stripe_info =
                async_simple::coro::syncAwait(
                    client->call<&Coordinator::get_stripe_info>(stripe_id))
                    .value();
            client.reset();

            // === Step 3: 同步执行编码 +
            // 分发（可改为协程异步，但先保正确性）===
            encode_and_distribute(stripe_info, std::move(value_buf),
                                  value_size);
        } catch (const std::exception &e) {
            ELOG(ERROR) << "[Datanode] handle_upload failed: " << e.what();
        }
    };
    std::thread(handler).detach();
}

void Datanode::handle_get_with_local_decode(const std::string &key,
                                            size_t value_size,
                                            const vector<vector<int>> &matrix) {
    auto handler = [this, key, value_size, matrix]() mutable {
        try {
            asio::ip::tcp::socket socket(io_context_);
            acceptor_.accept(socket);

            size_t need_packets = matrix.size();
            size_t packet_size = value_size / need_packets;
            size_t w = matrix[0].size();

            size_t data_size = w * packet_size;
            auto data_buf = std::make_unique<char[]>(data_size);
            bool ret = access_data(key, data_buf.get(), data_size);
            if (!ret) {
                ELOG(ERROR) << "access_data error";
            }
            auto decode_buf = std::make_unique<char[]>(value_size);
            local_decode(matrix, data_buf.get(), decode_buf.get(), packet_size);

            asio::write(socket, asio::buffer(decode_buf.get(), value_size));
            socket.close();

        } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
        }
    };
    std::thread(handler).detach();
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
    ELOG(DEBUG) << "write data to disk...key = " << key
                << ",value_size = " << value_size;
    std::string targetdir = "./storage/";
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

        // 按列异或：若 matrix[i][j]==1，则 data_buf + j*packet_size 异或入
        // out
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

    try {
        auto client = std::make_unique<coro_rpc::coro_rpc_client>();
        async_simple::coro::syncAwait(
            client->connect(ip, std::to_string(port)));
        async_simple::coro::syncAwait(
            client->call<&Datanode::handle_get_with_local_decode>(
                key, value_size, matrix));
        client.reset();

        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints =
            resolver.resolve(ip, std::to_string(port + SOCKET_PORT_OFFSET));
        asio::connect(socket, endpoints);

        asio::read(socket, asio::buffer(value, value_size));
        socket.close();
        return true;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool Datanode::read_from_datanode(const string &ip, int port, const string &key,
                                  char *value, size_t value_size) {
    try {
        auto client = std::make_unique<coro_rpc::coro_rpc_client>();
        async_simple::coro::syncAwait(
            client->connect(ip, std::to_string(port)));
        async_simple::coro::syncAwait(
            client->call<&Datanode::handle_get>(key, value_size));
        client.reset();

        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints =
            resolver.resolve(ip, std::to_string(port + SOCKET_PORT_OFFSET));
        asio::connect(socket, endpoints);

        asio::read(socket, asio::buffer(value, value_size));

        socket.close();
        return true;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool Datanode::write_to_datanode(const string &ip, int port, const string &key,
                                 char *value, size_t value_size) {
    ELOG(DEBUG) << "prepare to write to(" << ip << ":" << port << ")";
    try {
        auto client = std::make_unique<coro_rpc::coro_rpc_client>();
        async_simple::coro::syncAwait(
            client->connect(ip, std::to_string(port)));
        async_simple::coro::syncAwait(
            client->call<&Datanode::handle_set>(key, value_size));
        client.reset();

        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints =
            resolver.resolve({ip, std::to_string(port + SOCKET_PORT_OFFSET)});
        asio::connect(socket, endpoints);

        asio::write(socket, asio::buffer(value, value_size));

        socket.close();
        return true;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return false;
    }
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
                int basis_idx = pivot_row_for_col[c]; // 该主元对应的基索引（0
                                                      // ~ rank-1）
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

// ====== 新增：Datanode::encode_and_distribute ======
void Datanode::encode_and_distribute(const StripeInfo &stripe_info,
                                     std::unique_ptr<char[]> object_data,
                                     size_t total_size) {

    int k = stripe_info.k;
    int m = stripe_info.m;
    int w = stripe_info.w;
    size_t block_size = stripe_info.block_size;

    // 校验数据完整性
    if (total_size != static_cast<size_t>(k) * block_size) {
        throw std::runtime_error("Invalid object size");
    }

    // 准备数据指针（指向 object_data 内存）
    std::vector<char *> data_ptrs(k);
    for (int i = 0; i < k; ++i) {
        data_ptrs[i] = object_data.get() + i * block_size;
    }

    // 分配校验块内存（用 shared_ptr 保证生命周期）
    auto coding_buf = std::make_shared<std::vector<char>>(m * block_size);
    std::vector<char *> coding_ptrs(m);
    for (int i = 0; i < m; ++i) {
        coding_ptrs[i] = coding_buf->data() + i * block_size;
    }

    // 执行编码
    // stripe_info.ec_schema.ec->encode(
    //     data_ptrs.data(), coding_ptrs.data(), block_size);
    int *matrix = cauchy_original_coding_matrix(k, m, w);
    jerasure_matrix_encode(k, m, w, matrix, data_ptrs.data(),
                           coding_ptrs.data(), block_size);

    // === 并发写入所有块（含本地）===
    std::string key = "stripe" + std::to_string(stripe_info.stripe_id);
    std::vector<std::future<bool>> futures;
    futures.reserve(k + m);

    // 提交所有写任务（含本地存储）
    for (int idx = 0; idx < k + m; ++idx) {
        const auto &node = stripe_info.nodes_info[idx];
        char *block_data = (idx < k) ? data_ptrs[idx] : coding_ptrs[idx - k];
        ELOG(DEBUG) << "prepare wirte data to (" << node.node_ip << ":"
                    << std::to_string(node.node_port) << ")";
        // 若是本节点 → 直接 store_data（避免 network loopback）
        if (idx == 0) {
            futures.push_back(std::async(
                std::launch::async, [this, key, block_data, block_size]() {
                    return this->store_data(key, block_data, block_size);
                }));
        } else {
            // 远程节点 → 异步写（注意：coding_buf 需 capture shared_ptr
            // 延长生命周期）
            futures.push_back(
                std::async(std::launch::async, [this, node, key, block_data,
                                                block_size, coding_buf]() {
                    return this->write_to_datanode(
                        node.node_ip, node.node_port, key,
                        block_data, block_size);
                }));
        }
    }

    // === 聚合结果：任一失败 → 全部回滚（TODO：可加部分成功策略）===
    bool all_ok = true;
    for (auto &fut : futures) {
        if (!fut.get()) {
            all_ok = false;
        }
    }

    if (!all_ok) {
        throw std::runtime_error("Failed to write one or more blocks");
    }

    // // === Step 4: 通知 Coordinator 写入完成（可选：异步
    // fire-and-forget）=== try {
    //     auto client = std::make_unique<coro_rpc::coro_rpc_client>();
    //     async_simple::coro::syncAwait(
    //         client->connect(coordinator_ip_, coordinator_port_));
    //     async_simple::coro::syncAwait(
    //         client->call<&Coordinator::mark_stripe_written>(stripe_info.stripe_id));
    // } catch (...) {
    //     // 可容忍：Coordinator 可通过心跳/修复发现缺失条带
    //     std::cerr << "[Datanode] Failed to notify Coordinator, but data
    //     is stored." << std::endl;
    // }
}

} // namespace ECProject