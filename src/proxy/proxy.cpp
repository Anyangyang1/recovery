#include "proxy.h"

namespace ECProject {

void Proxy::write_to_datanode(const string &ip, int port, const string &key,
                              char *value, size_t value_size) {
    try {
        std::string node_ip_port = ip + ":" + std::to_string(port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_set>(
                key, value_size));

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
            std::cout << "[Proxy" << self_cluster_id_ << "][SET]"
                      << " Set errors in datanodes!" << std::endl;
        } else {
#ifdef MY_DEBUG
            std::cout << "[Proxy" << self_cluster_id_ << "][SET]"
                      << " Set " << key << " success! With length of "
                      << value_len << std::endl;
#endif
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
}

bool Proxy::read_from_datanode(const string &ip, int port, const string &key,
                               char *value, size_t value_size,
                               const vector<vector<int>> &matrix) {
    bool res = true;
    try {
        std::string node_ip_port = ip + ":" + std::to_string(port);
        async_simple::coro::syncAwait(
            datanodes_[node_ip_port]->call<&Datanode::handle_get>(
                key, value_size, matrix));
#ifdef MY_DEBYG
        std::cout << "[Proxy" << self_cluster_id_ << "][GET]"
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
        std::cout << "[Proxy" << self_cluster_id_ << "][GET]"
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

} // namespace ECProject