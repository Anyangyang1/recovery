#include "ylt/easylog.hpp"
#include <asio.hpp>
#include <iostream>
using asio::ip::tcp;
int main() {
    asio::io_context io;
    tcp::socket socket(io);
    socket.connect(
        tcp::endpoint(asio::ip::make_address("192.168.1.12"), 12345));

    // 获取本地地址（本机绑定的 IP:Port）
    auto local_ep = socket.local_endpoint();
    // 获取远端地址（对端的 IP:Port）
    auto remote_ep = socket.remote_endpoint();
    ELOG(WARNING) << "socket fd=" << socket.native_handle()
                  << ", local=" << local_ep.address().to_string() << ":"
                  << local_ep.port()
                  << ", remote=" << remote_ep.address().to_string() << ":"
                  << remote_ep.port();

    for (int i = 0; i < 10; i++) {
        std::string msg =
            "[" + std::to_string(i)  + "]"+ "Hello Asio! I'm " + local_ep.address().to_string() + "\n";
        asio::write(socket, asio::buffer(msg));
    }

    socket.close();
}