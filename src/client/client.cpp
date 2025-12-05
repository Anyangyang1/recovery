#include "client.h"
#include <unistd.h>

namespace ECProject {
Client::Client(std::string ip, int port, std::string coordinator_ip,
               int coordinator_port)
    : ip_(ip), port_(port), coordinator_ip_(coordinator_ip),
      coordinator_port_(coordinator_port),
      acceptor_(io_context_,
                asio::ip::tcp::endpoint(
                    asio::ip::address::from_string(ip.c_str()), port_)) {
    easylog::set_min_severity(easylog::Severity::DEBUG);
    rpc_coordinator_ = std::make_unique<coro_rpc::coro_rpc_client>();
    async_simple::coro::syncAwait(rpc_coordinator_->connect(
        coordinator_ip_, std::to_string(coordinator_port_)));
}

Client::~Client() { acceptor_.close(); }

void Client::set(std::string value) {

    auto result = async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_set>(
            std::chrono::seconds{3}, value.size()));

    if (!result) {
        ELOG(ERROR) << "RPC call failed: " << result.error();
        return; // »òÆäËû fallback
    }

    auto response = std::move(result).value();
    auto client = std::make_unique<coro_rpc::coro_rpc_client>();
    async_simple::coro::syncAwait(
        client->connect(response.node_ip, std::to_string(response.node_port)));
    async_simple::coro::syncAwait(client->call<&Datanode::handle_upload>(
        response.stripe_id, value.size()));
    

    ELOG(DEBUG) << "[SET] Send data"
              << " to node_address:" << response.node_ip << ":"
              << response.node_port;

    asio::ip::tcp::socket socket_(io_context_);
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(response.node_ip),
                                     response.node_port + SOCKET_PORT_OFFSET);
    socket_.connect(endpoint);
    asio::write(socket_, asio::buffer(value, value.size()));
    socket_.close();
    client.reset();
    ELOG(DEBUG) << "Send data completely.";
}

// std::string Client::get(std::string key) {
//     size_t value_len =
//         async_simple::coro::syncAwait(
//             rpc_coordinator_->call<&Coordinator::request_get>(key, ip_,
//             port_)) .value();

//     std::string key_buf(key.size(), 0);
//     std::string value_buf(value_len, 0);

//     if (!IF_SIMULATION) {
//         asio::ip::tcp::socket socket_(io_context_);
//         acceptor_.accept(socket_);

//         std::vector<unsigned char> size_buf(sizeof(int));
//         asio::read(socket_, asio::buffer(size_buf, size_buf.size()));
//         int key_size = bytes_to_int(size_buf);
//         asio::read(socket_, asio::buffer(size_buf, size_buf.size()));
//         int value_size = bytes_to_int(size_buf);
//         if (value_size > 0) {
//             size_t read_len_of_key = asio::read(
//                 socket_, asio::buffer(key_buf.data(), key_buf.size()));
//             my_assert(read_len_of_key == key.size() && key_buf == key);

//             size_t read_len_of_value = asio::read(
//                 socket_, asio::buffer(value_buf.data(), value_buf.size()));
//             my_assert(read_len_of_value == value_len);

//             asio::error_code ignore_ec;
//             socket_.shutdown(asio::ip::tcp::socket::shutdown_both,
//             ignore_ec); socket_.close(ignore_ec);

//             std::cout << "[GET] get key: " << key_buf.data()
//                       << ", valuesize: " << value_len << std::endl;
//         } else {
//             std::cout << "[GET] can not get value of " << key_buf.data()
//                       << std::endl;
//         }
//     }

//     return value_buf;
// }

// void Client::delete_stripe(unsigned int stripe_id) {
//     std::vector<unsigned int> stripe_ids;
//     stripe_ids.push_back(stripe_id);
//     async_simple::coro::syncAwait(
//         rpc_coordinator_->call<&Coordinator::request_delete_by_stripe>(
//             stripe_ids));
//     std::cout << "[DEL] deleting Stripe " << stripe_id << std::endl;
// }

// void Client::delete_all_stripes() {
//     auto stripe_ids = async_simple::coro::syncAwait(
//                           rpc_coordinator_->call<&Coordinator::list_stripes>())
//                           .value();
//     async_simple::coro::syncAwait(
//         rpc_coordinator_->call<&Coordinator::request_delete_by_stripe>(
//             stripe_ids));
//     for (auto it = stripe_ids.begin(); it != stripe_ids.end(); it++) {
//         std::cout << "[DEL] deleting Stripe " << *it << std::endl;
//     }
// }

// RepairResp Client::nodes_repair(std::vector<unsigned int> failed_node_ids) {
//     auto response =
//         async_simple::coro::syncAwait(
//             rpc_coordinator_->call_for<&Coordinator::request_repair>(
//                 std::chrono::seconds{0}, failed_node_ids, -1))
//             .value();
//     return response;
// }

// RepairResp Client::blocks_repair(std::vector<unsigned int> failed_block_ids,
//                                  int stripe_id) {
//     auto response =
//         async_simple::coro::syncAwait(
//             rpc_coordinator_->call_for<&Coordinator::request_repair>(
//                 std::chrono::seconds{0}, failed_block_ids, stripe_id))
//             .value();
//     return response;
// }

// std::vector<unsigned int> Client::list_stripes() {
//     auto response = async_simple::coro::syncAwait(
//                         rpc_coordinator_->call<&Coordinator::list_stripes>())
//                         .value();
//     return response;
// }
} // namespace ECProject