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
    rpc_server_->register_handler<&Datanode::handle_get>(this);
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
                      << key_buf << " with " << value_size << "bytes"
                      << std::endl;
#endif

        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }
    };
}

void Datanode::handle_get(const std::string &key, size_t value_size,
                          const vector<vector<int>> &matrix) {
    auto handler = [this, key, value_size, matrix]() mutable {
        asio::error_code ec;
        asio::ip::tcp::socket socket_(io_context_);
        acceptor_.accept(socket_);

        size_t need_packets = matrix.size();
        size_t packet_size = value_size / need_packets;
        size_t w = matrix[0].size();

        std::string data_buf(w * packet_size);
        bool ret = access_data(key, data_buf.data(), packet_size * w);

        std::string decode_buf(value_size);
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

bool Datanode::store_data(std::string &key, const char *value,
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

} // namespace ECProject