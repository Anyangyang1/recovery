#include "loadbalance.h"
#include "logging.hpp"
#include "sggh.h"
#include "utils.h"
#include <asio.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
using namespace std;
using namespace ECProject;
using asio::ip::tcp;
std::string get_data(int len);
int main() {
    coro_rpc::coro_rpc_client client;
    async_simple::coro::syncAwait(client.connect("192.168.1.13", "8888"));
    auto data =
        async_simple::coro::syncAwait(client.call<&get_data>(1024)).value();
    cout << "Received " << data.size() << "bytes\n";
    return 0;
}
// try {
//     asio::io_context io_context;
//     tcp::resolver resolver(io_context);
//     tcp::socket socket(io_context);

//     auto endpoints = resolver.resolve("192.168.1.13", "8888");
//     asio::connect(socket, endpoints);  // 抛异常版（更简洁）

//     std::vector<char> buf(100);  // ? 用 vector 避免 string 的 \0 问题
//     size_t n = asio::read(socket, asio::buffer(buf),
//     asio::transfer_exactly(9));
//     // 或：size_t n = socket.read_some(asio::buffer(buf));

//     std::string received(buf.data(), n);  // 显式构造，避免 \0 截断
//     std::cout << "Received (" << n << " bytes): '" << received << "'" <<
//     std::endl;
//     // 输出：Received (9 bytes): 'hello tom'
// } catch (std::exception& e) {
//     std::cerr << "Error: " << e.what() << std::endl;
// }
