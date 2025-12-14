#include "ylt/easylog.hpp"
#include <asio.hpp>
#include <iostream>
#include <thread>
using asio::ip::tcp;

int main() {
    asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 12345));

    std::cout << "Server listening on 0.0.0.0:12345\n";

    for (;;) {
        tcp::socket socket(io);
        acceptor.accept(socket);
        std::thread([sock = std::move(socket)]() mutable {
            auto remote_ep = sock.remote_endpoint();
            try {
                for (int i = 0; i < 10; ++i) { // ³¢ÊÔ¶Á10´Î
                    asio::streambuf buf;
                    asio::read_until(sock, buf, '\n');
                    std::string msg{buffers_begin(buf.data()),
                                    buffers_end(buf.data())};
                    ELOG(WARNING) << "fd=" << sock.native_handle() << " from "
                               << remote_ep << " msg[" << i << "]=" << msg;
                }
            } catch (...) {
            }
            sock.close();
        }).detach();
    }
}