#include "datanode.h"

namespace ECProject {

Datanode::Datanode(std::string ip, int port, size_t io_thread_num)
    : ip_(ip), port_(port),
      io_pool_(std::make_unique<ThreadPool>(io_thread_num)) {
    easylog::set_min_severity(easylog::Severity::WARNING);
    // port is for rpc
    rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(RPC_NUM, port_);

    rpc_server_->register_handler<&Datanode::do_repair>(this);
    rpc_server_->register_handler<&Datanode::do_repair_with_opt>(this);
    rpc_server_->register_handler<&Datanode::do_repair_with_opt_isa>(this);
    rpc_server_->register_handler<&Datanode::handle_delete_stripe>(this);
    rpc_server_->register_handler<&Datanode::handle_clear_time>(this);
    rpc_server_->register_handler<&Datanode::handle_delete_all_file>(this);
    rpc_server_->register_handler<&Datanode::print_download_data_packet_num>(
        this);
    rpc_server_->register_handler<&Datanode::do_repair_no_local_decode>(this);

    std::string targetdir = "./storage/";
    clear_directory(targetdir);
    if (access(targetdir.c_str(), 0) == -1) {
        mkdir(targetdir.c_str(), S_IRWXU);
    }
    start_data_service();
}

Datanode::~Datanode() {
    running_ = false;
    data_cv_.notify_all();
    if (data_accept_thread_.joinable())
        data_accept_thread_.join();
    if (data_worker_thread_.joinable())
        data_worker_thread_.join();
    rpc_server_->stop();
    if (io_pool_)
        io_pool_->stop(); // 确保任务完成或取消
}

void Datanode::run() { auto ret = rpc_server_->start(); }

// ====== datanode.cpp —— 新增 ======
void Datanode::start_data_service() {
    int data_port = port_ + SOCKET_PORT_OFFSET;

    // 启动 accept 线程
    data_accept_thread_ = std::thread([this, data_port]() {
        asio::io_context ioc;
        asio::ip::tcp::acceptor acceptor(
            ioc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), data_port));
        ELOG(WARNING) << "Data service listening on :" << data_port;

        while (running_) {
            try {
                auto socket = std::make_shared<asio::ip::tcp::socket>(ioc);
                acceptor.accept(*socket);

                // 读 1B op
                uint8_t op_byte;
                asio::read(*socket, asio::buffer(&op_byte, 1));
                DataOp op = static_cast<DataOp>(op_byte);

                DataTask task;
                task.op = op;
                task.socket = socket;

                switch (op) {
                case DataOp::UPLOAD: {
                    std::array<uint32_t, 2> hdr;
                    asio::read(*socket, asio::buffer(hdr));
                    task.stripe_id = ntohl(hdr[0]);
                    task.value_size = ntohl(hdr[1]);
                    break;
                }
                case DataOp::SET:
                case DataOp::GET:
                case DataOp::GET_WITH_DECODE: {
                    // 读 key_len + key + value_size
                    uint32_t key_len;
                    asio::read(*socket, asio::buffer(&key_len, 4));
                    key_len = ntohl(key_len);
                    task.key.resize(key_len);
                    asio::read(*socket, asio::buffer(task.key));
                    uint32_t value_size;
                    asio::read(*socket, asio::buffer(&value_size, 4));
                    task.value_size = ntohl(value_size);
                    // ELOG(ERROR) << "recevie key: " << task.key;

                    if (op == DataOp::GET_WITH_DECODE) {
                        uint32_t rows, cols;
                        asio::read(*socket, asio::buffer(&rows, 4));
                        rows = ntohl(rows);
                        asio::read(*socket, asio::buffer(&cols, 4));
                        cols = ntohl(cols);
                        task.matrix_rows = rows;
                        task.matrix_cols = cols;
                        task.matrix_01.resize(rows * cols);
                        asio::read(*socket, asio::buffer(task.matrix_01));
                    }
                    break;
                }
                default:
                    ELOG(ERROR) << "Unknown op: " << static_cast<int>(op_byte);
                    socket->close();
                    continue;
                }
                std::string keykey = task.key;
                {
                    std::lock_guard lock(data_mutex_);
                    data_queue_.emplace(std::move(task));
                    data_cv_.notify_one();
                }
                // ELOG(ERROR) << "put task: " << task.op << " " << keykey
                            // << " task_size: " << data_queue_.size();
                

            } catch (const std::exception &e) {
                // if (running_)
                ELOG(ERROR) << "Accept loop: " << e.what();
            }
        }
    });

    // 启动 worker 线程
    data_worker_thread_ = std::thread([this]() { data_worker_loop(); });
}

void Datanode::data_worker_loop() {
    while (running_) {
        DataTask task;
        {
            // ELOG(ERROR) << "wait......";
            std::unique_lock lock(data_mutex_);
            data_cv_.wait(lock,
                          [this] { return !data_queue_.empty() || !running_; });
            // ELOG(ERROR) << "wait......successfully";
            if (!running_ && data_queue_.empty())
                break;
            if (!data_queue_.empty()) {
                task = std::move(data_queue_.front());
                data_queue_.pop();
            }
        }
        if (!task.socket)
            continue;
        //  std::thread([this, task = std::move(task)]() mutable {
            // ELOG(ERROR) << "execute task: " << task.op << " " << task.key
                        // << " task_size: " << data_queue_.size();

            auto &socket = *task.socket;
            try {
                switch (task.op) {
                case DataOp::UPLOAD: {
                    char *buf = nullptr;
                    int ret = posix_memalign(reinterpret_cast<void **>(&buf),
                                             SIMD_ALIGNMENT, task.value_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }
                    {
                        SCOPED_TIMER("read data from client...");
                        asio::read(socket, asio::buffer(buf, task.value_size),
                                   asio::transfer_exactly(task.value_size));
                        socket.close();
                    }

                    StripeInfo info;
                    {
                        SCOPED_TIMER("request metadata...");
                        auto coordinator_client =
                            std::make_unique<coro_rpc::coro_rpc_client>();
                        async_simple::coro::syncAwait(
                            coordinator_client->connect(
                                COORDINATOR_IP,
                                std::to_string(COORDINATOR_PORT)));
                        auto result = async_simple::coro::syncAwait(
                            coordinator_client
                                ->call<&Coordinator::get_stripe_info>(
                                    task.stripe_id));

                        if (!result)
                            throw std::runtime_error("get_stripe_info failed");
                        info = std::move(result).value();
                    }
                    {
                        SCOPED_TIMER("encode and distribute...");
                        encode_and_distribute(info, buf, task.value_size);
                    }
                    ELOG(WARNING)
                        << "Stripe " << task.stripe_id << " processed.";
                    free(buf);
                    break;
                }
                case DataOp::SET: {
                    download_data_packet_num_ += task.value_size;
                    char *buf = nullptr;
                    int ret = posix_memalign(reinterpret_cast<void **>(&buf),
                                             SIMD_ALIGNMENT, task.value_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }
                    {
                        SCOPED_TIMER("[NET]read data " + task.key);
                        // ELOG(ERROR) << "begin read data..." << task.key;
                        asio::read(socket, asio::buffer(buf, task.value_size),
                                   asio::transfer_exactly(task.value_size));
                        // ELOG(ERROR)
                        //     << "receive data: " << to_hex_string2(buf, 16)
                        //     << " " << task.key;
                        // ELOG(ERROR) << "read data end..." << task.key;
                    }
                    {
                        SCOPED_TIMER("[DISK]write data " + task.key);
                        bool ok = store_data(task.key, buf, task.value_size);
                        if (!ok)
                            throw std::runtime_error("store_data failed");
                    }
                    socket.close();
                    free(buf);
                    break;
                }
                case DataOp::GET: {
                    double read_disk;
                    double send_to_net;
                    upload_data_packet_num_ += task.value_size;
                    char *buf = nullptr;
                    int ret = posix_memalign(reinterpret_cast<void **>(&buf),
                                             SIMD_ALIGNMENT, task.value_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }
                    {
                        SCOPED_TIMER_WITH_CB(
                            "[DISK]read data " + task.key,
                            [&read_disk](double ms) { read_disk = ms; });
                        bool ok = access_data(task.key, buf, task.value_size);
                        if (!ok)
                            throw std::runtime_error("access_data failed");
                    }
                    {
                        SCOPED_TIMER_WITH_CB(
                            "[NET]send data to node...",
                            [&send_to_net](double ms) { send_to_net = ms; });
                        asio::write(socket, asio::buffer(buf, task.value_size));

                        uint8_t success;
                        asio::read(socket, asio::buffer(&success, sizeof(success)));
                        // ELOG(ERROR) << "success: " << success;
                        if (!success) {
                            ELOG(ERROR) << "send data error...";
                        }
                    }

                    int64_t us = static_cast<int64_t>(
                        std::round(read_disk * 1000)); // 毫秒→微秒，保留精度
                    us = htobe64(us); // 转为网络字节序（big-endian）
                    asio::write(socket, asio::buffer(&us, sizeof(us)));

                    us = static_cast<int64_t>(
                        std::round(send_to_net * 1000)); // 毫秒→微秒，保留精度
                    us = htobe64(us); // 转为网络字节序（big-endian）
                    asio::write(socket, asio::buffer(&us, sizeof(us)));

                    read_disk_time_ += read_disk;
                    net_time_ += send_to_net;

                    socket.close();
                    free(buf);
                    break;
                }
                case DataOp::GET_WITH_DECODE: {
                    double read_disk;
                    double local_decode;
                    double send_to_net;
                    size_t need_packets = task.matrix_rows;
                    size_t packet_size = task.value_size / need_packets;
                    
                    upload_data_packet_num_ += need_packets;
                    char *data_buf = nullptr;
                    auto matrix = string_to_matrix(
                        task.matrix_01, task.matrix_rows, task.matrix_cols);
                    auto idxs = alter_matrix(matrix);
                    size_t data_size = matrix[0].size() * packet_size;

                    // ELOG(ERROR) << "matrix row: " << matrix.size() << " matrix col: " << matrix[0].size();
                    // ELOG(ERROR) << "matrix: " << matrix_to_01_string(matrix);
                    // ELOG(ERROR) << "idxs " << matrix_to_01_string(std::vector<std::vector<int>>{idxs});

                    int ret =
                        posix_memalign(reinterpret_cast<void **>(&data_buf),
                                       SIMD_ALIGNMENT, data_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }

                    {
                        SCOPED_TIMER_WITH_CB(
                            "[DISK]read data " + task.key,
                            [&read_disk](double ms) { read_disk = ms; });
                        bool ok = access_data(task.key, data_buf, idxs);
                        if (!ok)
                            throw std::runtime_error("access_data failed");
                    }
                    
                    char *decode_buf = nullptr;
                    ret = posix_memalign(reinterpret_cast<void **>(&decode_buf),
                                         SIMD_ALIGNMENT, task.value_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }
                    {
                        SCOPED_TIMER_WITH_CB(
                            "local decode...",
                            [&local_decode](double ms) { local_decode = ms; });
                        local_decode_isa(matrix, data_buf, decode_buf,
                                         packet_size);
                    }
                    {
                        SCOPED_TIMER_WITH_CB(
                            "[NET]send data to node...",
                            [&send_to_net](double ms) { send_to_net = ms; });
                        asio::write(socket,
                                    asio::buffer(decode_buf, task.value_size));

                        uint8_t success;
                        asio::read(socket, asio::buffer(&success, sizeof(success)));
                        // ELOG(ERROR) << "success: " << success;
                        if (!success) {
                            ELOG(ERROR) << "send data error...";
                        }
                    }
                    int64_t us = static_cast<int64_t>(
                        std::round(read_disk * 1000)); // 毫秒→微秒，保留精度
                    us = htobe64(us); // 转为网络字节序（big-endian）
                    asio::write(socket, asio::buffer(&us, sizeof(us)));

                    us = static_cast<int64_t>(
                        std::round(local_decode * 1000)); // 毫秒→微秒，保留精度
                    us = htobe64(us); // 转为网络字节序（big-endian）
                    asio::write(socket, asio::buffer(&us, sizeof(us)));

                    us = static_cast<int64_t>(
                        std::round(send_to_net * 1000)); // 毫秒→微秒，保留精度
                    us = htobe64(us); // 转为网络字节序（big-endian）
                    asio::write(socket, asio::buffer(&us, sizeof(us)));

                    // ELOG(ERROR) << "read:disk: " << read_disk
                    //             << " local_decode: " << local_decode
                    //             << " send_to_net: " << send_to_net;
                    read_disk_time_ += read_disk;
                    computing_time_ += local_decode;
                    net_time_ += send_to_net;

                    socket.close();
                    free(data_buf);
                    free(decode_buf);
                    break;
                }
                }
            } catch (const std::exception &e) {
                ELOG(ERROR)
                    << "Data worker error (op=" << static_cast<int>(task.op)
                    << "): " << e.what();
                if (socket.is_open())
                    socket.close();
            }
        // });

        // ELOG(ERROR) << "running_:  " << running_;
    }
    ELOG(ERROR) << "exit.. successfully successfully";
}

std::vector<int> Datanode::alter_matrix(std::vector<std::vector<int>> &matrix) {
    if (matrix.empty() || matrix[0].empty()) return {};

    int m = matrix.size(), n = matrix[0].size();
    std::vector<int> mask;
    std::vector<std::vector<int>> new_matrix(m);

    for (int j = 0; j < n; ++j) {
        bool all_zero = true;
        for (int i = 0; i < m; ++i) {
            if (matrix[i][j] != 0) {
                all_zero = false;
                break;
            }
        }
        mask.push_back(all_zero ? 0 : 1);
        if (!all_zero) {
            for (int i = 0; i < m; ++i) {
                new_matrix[i].push_back(matrix[i][j]);
            }
        }
    }

    matrix = std::move(new_matrix);
    return mask;
}

void Datanode::handle_delete_stripe(unsigned int stripe_id,
                                    unsigned int failed_block_id) {
    const std::string &path = "./storage/stripe_" + std::to_string(stripe_id) +
                              "_" + std::to_string(failed_block_id);
    ELOG(WARNING) << "delete file " << path;
    bool success = delete_file(path);
    if (!success) {
        ELOG(ERROR) << "Failed to delete " << path;
    }
}

bool Datanode::delete_file(const std::string &path) {
    if (std::remove(path.c_str()) != 0) {
        // 注意：文件不存在时也可能返回非0（errno=ENOENT）
        if (errno != ENOENT) {
            ELOG(ERROR) << "Failed to delete '" << path
                        << "': " << std::strerror(errno) << '\n';
            return false;
        }
    }
    return true; // 包括“文件不存在”视为成功删除
}

void Datanode::handle_delete_all_file() {
    const std::string dir_path = "./storage";
    clear_directory(dir_path);
    upload_data_packet_num_ = 0;
    download_data_packet_num_ = 0;
}

void Datanode::handle_clear_time() {
    upload_data_packet_num_ = 0;
    download_data_packet_num_ = 0;

    read_disk_time_ = 0.0;
    computing_time_ = 0.0;
    net_time_ = 0.0;
}

bool Datanode::clear_directory(const std::string &dir_path) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(dir_path, ec) || !fs::is_directory(dir_path, ec)) {
        ELOG(ERROR) << "Invalid directory: " << dir_path << "\n";
        return false;
    }

    // 遍历目录下每一项（非递归 iterator 即可）
    for (const auto &entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) {
            ELOG(ERROR) << "Iterate error: " << ec.message() << "\n";
            return false;
        }

        // 递归删除：文件 or 目录都删（remove_all 能删非空目录）
        if (!fs::remove_all(entry.path(), ec)) {
            ELOG(ERROR) << "Failed to remove " << entry.path() << ": "
                        << ec.message() << "\n";
            return false; // 或继续删其他项：不 return，仅记录失败
        }
    }
    return true;
}

// // 获取本地地址（本机绑定的 IP:Port）
// auto local_ep = socket.local_endpoint();
// // 获取远端地址（对端的 IP:Port）
// auto remote_ep = socket.remote_endpoint();
// ELOG(WARNING) << key << " socket fd=" <<
// socket.native_handle()
//               << ", local=" << local_ep.address().to_string()
// <<
//               ":"
//               << local_ep.port()
//               << ", remote=" <<
// remote_ep.address().to_string()
//               << ":" << remote_ep.port();

bool Datanode::access_data(const std::string &key, char *value_buf,
                           size_t value_size) {
    std::string readpath = "./storage/" + key;
    if (access(readpath.c_str(), 0) == -1) {
        ELOG(ERROR) << "[Disk][Get] file does not exist!" << readpath;
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
    std::string readpath = "./storage/" + key;
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
    // ELOG(WARNING) << "[store_data] write data to disk. key = " << key;
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
        ELOG(WARNING) << "[Disk] failed to set!";
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

void Datanode::local_decode_isa(const std::vector<std::vector<int>> &matrix,
                                char *data_buf, char *decode_buf,
                                size_t packet_size) {
    // ELOG(ERROR) << matrix_to_01_string(matrix);
    if (matrix.empty() || matrix[0].empty() || packet_size == 0)
        return;
    size_t need_packet = matrix.size();
    size_t w = matrix[0].size();
    if (w == 0 || packet_size == 0)
        return;

    char *out = decode_buf;
    for (size_t i = 0; i < need_packet; ++i) {
        std::vector<void *> srcs;
        for (size_t j = 0; j < w; ++j) {
            if (matrix[i][j]) {
                void *src = (data_buf + j * packet_size);
                srcs.push_back(src);
            }
        }
        void *current_dest = out;
        srcs.push_back(current_dest);
        if (srcs.size() == 2) {
            {
                SCOPED_TIMER("memcpy: " + std::to_string(packet_size));
                memcpy(current_dest, srcs[0], packet_size);
            }
        } else {
            {
                SCOPED_TIMER("xor_gen: " + std::to_string(srcs.size()) +
                             ", size: " + std::to_string(packet_size));
                xor_gen(static_cast<int>(srcs.size()),
                        static_cast<int>(packet_size), srcs.data());
            }
        }

        out += packet_size; // 移到下一块输出位置
    }
}

void Datanode::print_download_data_packet_num() {
    ELOG(ERROR) << "download data packets: " << download_data_packet_num_;
    ELOG(ERROR) << "upload data packets: " << upload_data_packet_num_;
    ELOG(ERROR) << "read_disk_time_: " << read_disk_time_;
    ELOG(ERROR) << "computing_time_: " << computing_time_;
    ELOG(ERROR) << "net_time_: " << net_time_;
}
// ====== datanode.cpp —— 替换 read_from_datanode_with_local_decode ======
RepairResp Datanode::read_from_datanode_with_local_decode(
    const string &ip, int port, const string &key, char *value,
    size_t value_size, const vector<vector<int>> &matrix) {
    download_data_packet_num_ += matrix.size();
    RepairResp resp;
    try {
        int data_port = port + SOCKET_PORT_OFFSET;
        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(ip, std::to_string(data_port));
        asio::connect(socket, endpoints);

        // 发 header: op + key + size + matrix
        uint8_t op = static_cast<uint8_t>(DataOp::GET_WITH_DECODE);
        asio::write(socket, asio::buffer(&op, 1));

        uint32_t key_len = htonl(key.size());
        asio::write(socket, asio::buffer(&key_len, 4));
        asio::write(socket, asio::buffer(key));

        uint32_t sz = htonl(value_size);
        asio::write(socket, asio::buffer(&sz, 4));

        // 发 matrix: rows + cols + 01 string
        uint32_t rows = htonl(matrix.size());
        uint32_t cols = htonl(matrix.empty() ? 0 : matrix[0].size());
        asio::write(socket, asio::buffer(&rows, 4));
        asio::write(socket, asio::buffer(&cols, 4));
        std::string mat_str = matrix_to_01_string(matrix);
        asio::write(socket, asio::buffer(mat_str));

        // 读结果
        asio::read(socket, asio::buffer(value, value_size));

        uint8_t success = 1;
        asio::write(socket, asio::buffer(&success, 1));

        int64_t us;
        asio::read(socket, asio::buffer(&us, sizeof(us)));
        us = be64toh(us);
        resp.read_from_disk_time = us / 1000.0;

        asio::read(socket, asio::buffer(&us, sizeof(us)));
        us = be64toh(us);
        resp.local_decode_time = us / 1000.0;

        asio::read(socket, asio::buffer(&us, sizeof(us)));
        us = be64toh(us);
        resp.send_to_net_time = us / 1000.0;

        socket.close();
    } catch (const std::exception &e) {
        ELOG(ERROR) << "read_from_datanode_with_local_decode failed: "
                    << e.what();
    }
    return resp;
}

// ====== read_from_datanode (GET) ======
RepairResp Datanode::read_from_datanode(const string &ip, int port,
                                        const string &key, char *value,
                                        size_t value_size) {
    RepairResp resp;
    try {
        int data_port = port + SOCKET_PORT_OFFSET;
        asio::ip::tcp::socket socket(io_context_);
        asio::connect(socket, asio::ip::tcp::resolver(io_context_)
                                  .resolve(ip, std::to_string(data_port)));

        uint8_t op = static_cast<uint8_t>(DataOp::GET);
        asio::write(socket, asio::buffer(&op, 1));
        uint32_t key_len = htonl(key.size());
        asio::write(socket, asio::buffer(&key_len, 4));
        asio::write(socket, asio::buffer(key));
        uint32_t sz = htonl(value_size);
        asio::write(socket, asio::buffer(&sz, 4));

        // 读结果
        asio::read(socket, asio::buffer(value, value_size));

        uint8_t success = 1;
        asio::write(socket, asio::buffer(&success, 1));


        int64_t us;
        asio::read(socket, asio::buffer(&us, sizeof(us)));
        us = be64toh(us);
        resp.read_from_disk_time = us / 1000.0;

        asio::read(socket, asio::buffer(&us, sizeof(us)));
        us = be64toh(us);
        resp.send_to_net_time = us / 1000.0;

        socket.close();

    } catch (const std::exception &e) {
        ELOG(ERROR) << "read_from_datanode_with_local_decode failed: "
                    << e.what();
    }
    return resp;
}

// ====== write_to_datanode (SET) ======
bool Datanode::write_to_datanode(const string &ip, int port, const string &key,
                                 char *value, size_t value_size) {
    try {
        int data_port = port + SOCKET_PORT_OFFSET;
        asio::ip::tcp::socket socket(io_context_);
        asio::connect(socket, asio::ip::tcp::resolver(io_context_)
                                  .resolve(ip, std::to_string(data_port)));

        uint8_t op = static_cast<uint8_t>(DataOp::SET);
        asio::write(socket, asio::buffer(&op, 1));
        uint32_t key_len = htonl(key.size());
        asio::write(socket, asio::buffer(&key_len, 4));
        asio::write(socket, asio::buffer(key));
        uint32_t sz = htonl(value_size);
        asio::write(socket, asio::buffer(&sz, 4));

        {
            SCOPED_TIMER("[NET]write to datanode: " + ip + "/" + key);
            // ELOG(ERROR) << "begin to send: " << ip << "/" << key;
            // ELOG(ERROR) << "send data: " << to_hex_string2(value, 16) << ip
                        // << "/" << key;
            asio::write(socket, asio::buffer(value, value_size));
            // ELOG(ERROR) << "send successfully: " << ip << "/" << key;
        }

        socket.close();
        ELOG(WARNING) << "write successfully.. " << key;
        return true;
    } catch (...) {
        ELOG(ERROR) << "write failed.. " << key;
        return false;
    }
}

void Datanode::do_repair_with_opt(std::vector<DecodeRequest> helpers,
                                  size_t block_size, int w,
                                  std::string repair_file_name) {
    {
        SCOPED_TIMER("repair " + repair_file_name);
        assert(block_size % w == 0);
        size_t packet_size = block_size / w;
        // === 日志 ===
        std::string helpers_info;
        for (const auto &h : helpers)
            helpers_info += h.ip + "/" + h.file_name + ", ";
        ELOG(WARNING) << "do_repair_with_opt: " << repair_file_name
                      << " read from [" << helpers_info << "]";

        std::vector<char *> original_datas(helpers.size());

        // === 分配数据 buffer（shared_ptr 保活）===
        for (size_t i = 0; i < helpers.size(); ++i) {
            int ret =
                posix_memalign(reinterpret_cast<void **>(&original_datas[i]),
                               SIMD_ALIGNMENT, block_size);
            if (ret != 0) {
                ELOG(ERROR) << "posix_memalign failed";
                return;
            }
        }

        {
            SCOPED_TIMER("read & compute originals from survivors for repair " +
                         repair_file_name);
            std::vector<std::future<void>> futures;
            futures.reserve(helpers.size());
            for (size_t i = 0; i < helpers.size(); ++i) {
                const auto &helper = helpers[i];
                futures.push_back(io_pool_->submit([this, i, helper,
                                                    original_datas, packet_size,
                                                    block_size]() {
                    GF2BasisResult basis_result =
                        compute_basis_gf2_indices(helper.matrix);
                    size_t buf_size = packet_size * basis_result.basis.size();
                    char *buf = nullptr;
                    int ret = posix_memalign(reinterpret_cast<void **>(&buf),
                                             SIMD_ALIGNMENT, buf_size);
                    if (ret != 0) {
                        ELOG(ERROR) << "posix_memalign failed";
                        return;
                    }
                    ELOG(WARNING) << "allocate successfully";
                    // Step 2: 读 basis 数据
                    {
                        SCOPED_TIMER("read basis " + helper.file_name + " (" +
                                     std::to_string(buf_size) + "B)");
                        read_from_datanode_with_local_decode(
                            helper.ip, helper.port, helper.file_name, buf,
                            buf_size, basis_result.basis);
                    }
                    // Step 3: 还原原始块
                    {
                        SCOPED_TIMER("compute original " + helper.file_name);
                        compute_original_data(buf, basis_result.reps,
                                              original_datas[i], packet_size);
                    }
                    free(buf);
                }));
            }

            // 聚合异常
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

        // === XOR 修复 ===
        char *decode_data;
        int ret = posix_memalign(reinterpret_cast<void **>(&decode_data),
                                 SIMD_ALIGNMENT, block_size);
        if (ret != 0) {
            ELOG(ERROR) << "posix_memalign failed";
            return;
        }
        {
            SCOPED_TIMER("recompute XOR for " + repair_file_name);
            decode_xor(original_datas, block_size, decode_data);
        }

        // === 存盘 ===
        {
            SCOPED_TIMER("store repaired data: " + repair_file_name);
            if (!store_data(repair_file_name, decode_data, block_size)) {
                throw std::runtime_error("Failed to store repaired file: " +
                                         repair_file_name);
            }
        }
        free(decode_data);
        for (char *data_ptr : original_datas) {
            free(data_ptr);
        }

        ELOG(WARNING) << "Optimized repair completed: " << repair_file_name;
    }
}

void compute_repair_resp(std::vector<RepairResp> &response_from_helper,
                         RepairResp &response) {
    double read_from_disk_time = 0.0, local_decode_time = 0.0,
           send_to_net_time = 0.0;
    for (auto helper_time : response_from_helper) {
        read_from_disk_time += helper_time.read_from_disk_time;
        local_decode_time += helper_time.local_decode_time;
        send_to_net_time = max(helper_time.send_to_net_time, send_to_net_time);
    }
    response.read_from_disk_time =
        read_from_disk_time / response_from_helper.size();
    response.local_decode_time =
        local_decode_time / response_from_helper.size();
    response.send_to_net_time = send_to_net_time;
}

RepairResp Datanode::do_repair_with_opt_isa(std::vector<DecodeRequest> helpers,
                                            size_t block_size, int w,
                                            std::string repair_file_name) {
    RepairResp response;
    std::vector<RepairResp> response_from_helper(helpers.size());
    {
        SCOPED_TIMER_WITH_CB(
            "repair " + repair_file_name,
            [&response](double ms) { response.repair_time = ms; });
        assert(block_size % w == 0);
        size_t packet_size = block_size / w;
        // === 日志 ===
        std::string helpers_info;
        for (const auto &h : helpers)
            helpers_info += h.ip + "/" + h.file_name + ", ";
        ELOG(WARNING) << "do_repair_with_opt: " << repair_file_name
                      << " read from [" << helpers_info << "]";

        std::vector<GF2BasisResult> basis_results;
        basis_results.reserve(helpers.size());
        std::vector<char *> data_buf;
        data_buf.reserve(helpers.size());
        for (size_t i = 0; i < helpers.size(); i++) {
            const auto &helper = helpers[i];
            GF2BasisResult basis_result =
                compute_basis_gf2_indices(helper.matrix);
            size_t buf_size = packet_size * basis_result.basis.size();
            char *buf = nullptr;
            int ret = posix_memalign(reinterpret_cast<void **>(&buf),
                                     SIMD_ALIGNMENT, buf_size);
            if (ret != 0) {
                ELOG(ERROR) << "posix_memalign failed";
                throw std::runtime_error("posix_memalign failed");
            }
            basis_results.push_back(basis_result);
            data_buf.push_back(buf);
        }
        {
            SCOPED_TIMER_WITH_CB(
                "read all data from survivors for repair " + repair_file_name,
                [&response](double ms) { response.read_data_time = ms; });
            std::vector<std::future<void>> futures;

            futures.reserve(helpers.size());
            for (size_t i = 0; i < helpers.size(); ++i) {
                const auto &helper = helpers[i];
                futures.push_back(io_pool_->submit([this, i, helper,
                                                    basis_results, data_buf,
                                                    packet_size,
                                                    &response_from_helper]() {
                    size_t buf_size =
                        packet_size * basis_results[i].basis.size();

                    // Step 2: 读 basis 数据
                    {
                        SCOPED_TIMER("read basis " + helper.file_name + " (" +
                                     std::to_string(buf_size) + "B)");
                        response_from_helper[i] =
                            read_from_datanode_with_local_decode(
                                helper.ip, helper.port, helper.file_name,
                                data_buf[i], buf_size, basis_results[i].basis);
                    }
                }));
            }

            // 聚合异常
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

        // === XOR 修复 ===
        char *decode_data;
        int ret = posix_memalign(reinterpret_cast<void **>(&decode_data),
                                 SIMD_ALIGNMENT, block_size);
        if (ret != 0) {
            ELOG(ERROR) << "posix_memalign failed";
            throw std::runtime_error("posix_memalign failed");
        }
        {
            SCOPED_TIMER_WITH_CB(
                "recompute XOR for " + repair_file_name,
                [&response](double ms) { response.computing_time = ms; });
            decode_xor_with_basis(basis_results, data_buf, decode_data,
                                  packet_size);
        }

        // === 存盘 ===
        {
            SCOPED_TIMER_WITH_CB(
                "store repaired data: " + repair_file_name,
                [&response](double ms) { response.write_disk_time = ms; });
            if (!store_data(repair_file_name, decode_data, block_size)) {
                throw std::runtime_error("Failed to store repaired file: " +
                                         repair_file_name);
            }
        }
        computing_time_ += response.computing_time;
        free(decode_data);
        for (char *data_ptr : data_buf) {
            free(data_ptr);
        }

        ELOG(WARNING) << "Optimized repair completed: " << repair_file_name;
    }
    compute_repair_resp(response_from_helper, response);
    return response;
}

RepairResp
Datanode::do_repair_no_local_decode(std::vector<DecodeRequest> helpers,
                                    size_t block_size, int w,
                                    std::string repair_file_name) {
    RepairResp response;
    std::vector<RepairResp> response_from_helper(helpers.size());
    {
        SCOPED_TIMER_WITH_CB(
            "repair " + repair_file_name,
            [&response](double ms) { response.repair_time = ms; });
        assert(block_size % w == 0);

        // === 日志 ===
        std::string helpers_info;
        for (const auto &h : helpers)
            helpers_info += h.ip + "/" + h.file_name + ", ";
        ELOG(WARNING) << "do_repair: " << repair_file_name
                      << " read data from [" << helpers_info << "]";

        std::vector<char *> original_datas(helpers.size());

        // === 分配数据 buffer（shared_ptr 保活）===
        for (size_t i = 0; i < helpers.size(); ++i) {
            int ret =
                posix_memalign(reinterpret_cast<void **>(&original_datas[i]),
                               SIMD_ALIGNMENT, block_size);
            if (ret != 0) {
                ELOG(ERROR) << "posix_memalign failed";
                throw std::runtime_error("posix_memalign failed");
            }
        }

        {
            SCOPED_TIMER_WITH_CB(
                "read all data from survivors for repair " + repair_file_name,
                [&response](double ms) { response.read_data_time = ms; });
            std::vector<std::future<void>> futures;
            futures.reserve(helpers.size());

            for (size_t i = 0; i < helpers.size(); ++i) {
                const auto &helper = helpers[i];
                futures.push_back(
                    io_pool_->submit([this, i, helper, original_datas,
                                      block_size, &response_from_helper]() {
                        {
                            SCOPED_TIMER("read " + helper.file_name + " (" +
                                         std::to_string(block_size) + "B)");
                            response_from_helper[i] = read_from_datanode(
                                helper.ip, helper.port, helper.file_name,
                                original_datas[i], block_size);
                        }
                    }));
            }

            // 聚合异常（保留首个异常）
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

        // === XOR 修复 ===
        std::vector<std::vector<int>> bitmatrix = concatMatrices(helpers);
        char *decode_data;
        int ret = posix_memalign(reinterpret_cast<void **>(&decode_data),
                                 SIMD_ALIGNMENT, block_size);
        if (ret != 0) {
            ELOG(ERROR) << "posix_memalign failed";
            throw std::runtime_error("posix_memalign failed");
        }
        {
            SCOPED_TIMER_WITH_CB(
                "compute XOR for " + repair_file_name,
                [&response](double ms) { response.computing_time = ms; });
            decode_xor_with_matrix_isa(bitmatrix, original_datas, decode_data,
                                       block_size,
                                       block_size / bitmatrix.size());
        }

        // === 存盘 ===
        {
            SCOPED_TIMER_WITH_CB(
                "store repaired data: " + repair_file_name,
                [&response](double ms) { response.write_disk_time = ms; });
            if (!store_data(repair_file_name, decode_data, block_size)) {
                throw std::runtime_error("Failed to store repaired file: " +
                                         repair_file_name);
            }
        }
        computing_time_ += response.computing_time;
        free(decode_data);
        for (char *data_ptr : original_datas) {
            free(data_ptr);
        }
        ELOG(WARNING) << "Repair completed: " << repair_file_name;
    }
    compute_repair_resp(response_from_helper, response);
    response.local_decode_time = 0.0;
    return response;
}

RepairResp Datanode::do_repair(std::vector<DecodeRequest> helpers,
                               size_t block_size, int w,
                               std::string repair_file_name) {
    RepairResp response;
    std::vector<RepairResp> response_from_helper(helpers.size());
    {
        SCOPED_TIMER_WITH_CB(
            "repair " + repair_file_name,
            [&response](double ms) { response.repair_time = ms; });
        assert(block_size % w == 0);

        // === 日志 ===
        std::string helpers_info;
        for (const auto &h : helpers)
            helpers_info += h.ip + "/" + h.file_name + ", ";
        ELOG(WARNING) << "do_repair: " << repair_file_name
                      << " read data from [" << helpers_info << "]";

        std::vector<char *> original_datas(helpers.size());

        // === 分配数据 buffer（shared_ptr 保活）===
        for (size_t i = 0; i < helpers.size(); ++i) {
            int ret =
                posix_memalign(reinterpret_cast<void **>(&original_datas[i]),
                               SIMD_ALIGNMENT, block_size);
            if (ret != 0) {
                ELOG(ERROR) << "posix_memalign failed";
                throw std::runtime_error("posix_memalign failed");
            }
        }

        {
            SCOPED_TIMER_WITH_CB(
                "read all data from survivors for repair " + repair_file_name,
                [&response](double ms) { response.read_data_time = ms; });
            std::vector<std::future<void>> futures;
            futures.reserve(helpers.size());

            for (size_t i = 0; i < helpers.size(); ++i) {
                const auto &helper = helpers[i];
                futures.push_back(io_pool_->submit([this, i, helper,
                                                    original_datas, block_size,
                                                    &response_from_helper]() {
                    {
                        SCOPED_TIMER("read " + helper.file_name + " (" +
                                     std::to_string(block_size) + "B)");
                        response_from_helper[i] =
                            read_from_datanode_with_local_decode(
                                helper.ip, helper.port, helper.file_name,
                                original_datas[i], block_size, helper.matrix);
                    }
                }));
            }

            // 聚合异常（保留首个异常）
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

        // === XOR 修复 ===
        char *decode_data;
        int ret = posix_memalign(reinterpret_cast<void **>(&decode_data),
                                 SIMD_ALIGNMENT, block_size);
        if (ret != 0) {
            ELOG(ERROR) << "posix_memalign failed";
            throw std::runtime_error("posix_memalign failed");
        }
        {
            SCOPED_TIMER_WITH_CB(
                "recompute XOR for " + repair_file_name,
                [&response](double ms) { response.computing_time = ms; });
            decode_xor_isa(original_datas, block_size, decode_data);
        }

        // === 存盘 ===
        {
            SCOPED_TIMER_WITH_CB(
                "store repaired data: " + repair_file_name,
                [&response](double ms) { response.write_disk_time = ms; });
            if (!store_data(repair_file_name, decode_data, block_size)) {
                throw std::runtime_error("Failed to store repaired file: " +
                                         repair_file_name);
            }
        }
        computing_time_ += response.computing_time;
        free(decode_data);
        for (char *data_ptr : original_datas) {
            free(data_ptr);
        }
        ELOG(WARNING) << "Repair completed: " << repair_file_name;
    }
    compute_repair_resp(response_from_helper, response);
    return response;
}

void Datanode::decode_xor(const std::vector<char *> &original_datas,
                          size_t block_size, char *decode_data) {
    if (original_datas.empty())
        return;
    std::memset(decode_data, 0, block_size);
    for (char *block : original_datas) {
        galois_region_xor(decode_data, block, decode_data, block_size);
    }
}

void Datanode::decode_xor_with_basis(std::vector<GF2BasisResult> &basis_results,
                                     std::vector<char *> &data_buf,
                                     char *decode_data, size_t packet_size) {
    size_t w = basis_results[0].reps.size();
    char *out = decode_data;
    for (size_t i = 0; i < w; i++) {
        std::vector<void *> srcs;
        void *dest = out;
        for (size_t j = 0; j < basis_results.size(); j++) {
            auto indices = basis_results[j].reps[i];
            for (size_t k = 0; k < indices.size(); k++) {
                void *src = data_buf[j] + indices[k] * packet_size;
                srcs.push_back(src);
            }
        }
        srcs.push_back(dest);
        // {
        //     SCOPED_TIMER("xor_gen: " + std::to_string(srcs.size()) +
        //                  "size: " + std::to_string(packet_size));
        xor_gen(static_cast<int>(srcs.size()), static_cast<int>(packet_size),
                srcs.data());
        // }
        out += packet_size;
    }
}

void Datanode::decode_xor_isa(std::vector<char *> &original_datas,
                              size_t block_size, char *decode_data) {
    if (original_datas.empty())
        return;
    void *dest_ptr = decode_data;
    std::vector<void *> srcs(original_datas.begin(), original_datas.end());
    srcs.push_back(dest_ptr);
    // ELOG(ERROR) << "xor_gen: " << srcs.size() << " size: " << block_size;
    xor_gen(srcs.size(), block_size, srcs.data());
}

void Datanode::compute_original_data(char *buf,
                                     const std::vector<std::vector<int>> &reps,
                                     char *original_data, size_t packet_size) {
    if (reps.empty() || packet_size == 0)
        return;

    // key 改为 uint64_t，value 改为 offset（更安全）
    std::unordered_map<uint64_t, size_t> cache; // offset in original_data

    for (size_t i = 0; i < reps.size(); ++i) {
        auto indices = reps[i];
        std::sort(indices.begin(), indices.end());

        uint64_t key = indices_to_bitmask(indices);
        size_t dst_offset = i * packet_size;
        char *dst = original_data + dst_offset;

        auto it = cache.find(key);
        if (it != cache.end()) {
            // 命中：memcpy from cached offset
            const char *src_cached = original_data + it->second;
            std::memcpy(dst, src_cached, packet_size);
        } else {
            // 未命中：计算
            if (indices.empty()) {
                std::memset(dst, 0, packet_size);
            } else {
                std::memcpy(dst, buf + indices[0] * packet_size, packet_size);
                for (size_t k = 1; k < indices.size(); ++k) {
                    char *src = buf + indices[k] * packet_size;
                    galois_region_xor(dst, src, dst, packet_size);
                }
            }
            cache[key] = dst_offset; // 存 offset，而非指针，绝对安全
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
                                     char *data_buf, size_t total_size) {
    int k = stripe_info.k;
    int m = stripe_info.m;
    int w = stripe_info.w;
    size_t block_size = stripe_info.block_size;

    std::string stripe_info_str;
    for (auto ip_info : stripe_info.nodes_info) {
        stripe_info_str += ip_info.node_ip + ", ";
    }
    ELOG(WARNING) << "encode and distribute. stripe_" << stripe_info.stripe_id
                  << " will be stored in (" << stripe_info_str << ")";

    // 校验数据完整性
    if (total_size != static_cast<size_t>(k) * block_size) {
        throw std::runtime_error("Invalid object size: expected" +
                                 std::to_string(k * block_size) + ", got " +
                                 std::to_string(total_size));
    }
    char *coding_buf = nullptr;
    int ret = posix_memalign(reinterpret_cast<void **>(&coding_buf),
                             SIMD_ALIGNMENT, m * block_size);
    if (ret != 0) {
        ELOG(ERROR) << "posix_memalign failed";
        return;
    }

    char *data_start = data_buf;
    char *coding_start = coding_buf;

    // 准备数据指针（指向 buf 内存）
    std::vector<char *> data_ptrs(k), coding_ptrs(m);
    for (int i = 0; i < k; ++i)
        data_ptrs[i] = data_start + i * block_size;

    for (int i = 0; i < m; ++i)
        coding_ptrs[i] = coding_start + i * block_size;

    // 执行编码
    std::unique_ptr<ErasureCode> ec;
    if (stripe_info.ec_type == XOR) {
        ec = std::make_unique<XORCode>(k, m, w);
    } else {
        ec = std::make_unique<RSCode>(k, m);
    }

    {
        SCOPED_TIMER("encode stripe_" + std::to_string(stripe_info.stripe_id));
        ec->encode(data_ptrs.data(), coding_ptrs.data(), block_size);
    }

    // === 并发写入所有块（含本地）===

    std::vector<std::future<bool>> futures;
    futures.reserve(k + m);

    {
        SCOPED_TIMER("distribute stripe_" +
                     std::to_string(stripe_info.stripe_id));
        // 提交所有写任务（含本地存储）
        for (int idx = 0; idx < k + m; ++idx) {
            const auto &node = stripe_info.nodes_info[idx];
            char *block_data =
                (idx < k) ? data_ptrs[idx] : coding_ptrs[idx - k];
            size_t offset = block_data - data_buf;
            std::string key = "stripe_" +
                              std::to_string(stripe_info.stripe_id) + "_" +
                              std::to_string(idx);
            // 若是本节点 → 直接 store_data（避免 network loopback）
            if (idx == 0) {
                futures.push_back(io_pool_->submit([this, key, data_buf, offset,
                                                    block_size]() {
                    return this->store_data(key, data_buf + offset, block_size);
                }));
            } else {
                // 远程节点 → 异步写（注意：coding_buf 需 capture shared_ptr
                // 延长生命周期）
                futures.push_back(io_pool_->submit([this, node, key, data_buf,
                                                    offset, block_size]() {
                    return this->write_to_datanode(node.node_ip, node.node_port,
                                                   key, data_buf + offset,
                                                   block_size);
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
    }
    free(coding_buf);
}

void Datanode::decode_xor_with_matrix(
    const std::vector<std::vector<int>> &matrix,
    const std::vector<char *> &original_datas, char *decode_data,
    size_t block_size, size_t packet_size) {
    size_t k = original_datas.size();
    size_t w = matrix.size(); // matrix is w x (w * k)
    if (w == 0 || k == 0)
        return;
    if (block_size != w * packet_size) {
        // 可选：加 assert 或 throw，依工程规范
        return;
    }

    // 遍历每个大块（通常仅1个块；若支持 multi-block 可外层加循环）
    // 此处按 Jerasure 风格：支持 block_size 是 w*packet_size 的整数倍（多个
    // stripe）
    for (size_t stripe = 0; stripe < block_size; stripe += w * packet_size) {
        // 对每一输出 packet j（j in [0, w)）
        for (size_t j = 0; j < w; ++j) {
            char *out_ptr = decode_data + stripe + j * packet_size;
            bool started = false;

            // 遍历所有源块 i 和其内部 packet y
            for (size_t i = 0; i < k; ++i) {
                char *src_block = original_datas[i] + stripe;
                for (size_t y = 0; y < w; ++y) {
                    if (matrix[j][i * w + y]) { // coefficient == 1
                        char *in_ptr = src_block + y * packet_size;
                        if (!started) {
                            memcpy(out_ptr, in_ptr, packet_size);
                            started = true;
                        } else {
                            galois_region_xor(out_ptr, in_ptr, out_ptr,
                                              packet_size);
                        }
                    }
                }
            }

            // 若全为 0 行（理论上不应出现），清零
            if (!started) {
                memset(out_ptr, 0, packet_size);
            }
        }
    }
}
void Datanode::decode_xor_with_matrix_isa(
    const std::vector<std::vector<int>> &matrix,
    const std::vector<char *> &original_datas, char *decode_data,
    size_t block_size, size_t packet_size) {
    // ELOG(ERROR) << matrix_to_01_string(matrix);
    size_t k = original_datas.size();
    size_t w = matrix.size(); // matrix is w x (w * k)
    if (w == 0 || k == 0)
        return;
    if (block_size != w * packet_size) {
        return;
    }
    // 对每一输出 packet j（j in [0, w)）
    for (size_t j = 0; j < w; ++j) {
        void *out_ptr = decode_data + j * packet_size;
        std::vector<void *> srcs;
        for (size_t i = 0; i < k; ++i) {
            char *src_block = original_datas[i];
            for (size_t y = 0; y < w; ++y) {
                if (matrix[j][i * w + y]) { // coefficient == 1
                    void *in_ptr = src_block + y * packet_size;
                    srcs.push_back(in_ptr);
                }
            }
        }
        srcs.push_back(out_ptr);
        if (srcs.size() == 2) {
            {
                SCOPED_TIMER("memcpy: " + std::to_string(packet_size));
                memcpy(out_ptr, srcs[0], packet_size);
            }
        } else {
            // {
            //     SCOPED_TIMER("xor_gen: " + std::to_string(srcs.size()) +
            //                  "size: " + std::to_string(packet_size));
            xor_gen(static_cast<int>(srcs.size()),
                    static_cast<int>(packet_size), srcs.data());
            // }
        }
    }
}

std::vector<std::vector<int>>
Datanode::concatMatrices(const std::vector<DecodeRequest> &requests) {
    if (requests.empty())
        return {};

    size_t w = requests[0].matrix.size();
    size_t k = requests.size();
    std::vector<std::vector<int>> result(w, std::vector<int>(w * k));

    for (size_t idx = 0; idx < k; ++idx) {
        const auto &mat = requests[idx].matrix;
        for (size_t i = 0; i < w; ++i) {
            std::copy(mat[i].begin(), mat[i].end(),
                      result[i].begin() + idx * w);
        }
    }
    return result;
}

} // namespace ECProject