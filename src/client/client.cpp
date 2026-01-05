#include "client.h"
#include <thread>
#include <unistd.h>
namespace ECProject {
Client::Client(std::string ip, int port, std::string coordinator_ip,
               int coordinator_port)
    : ip_(ip), port_(port), coordinator_ip_(coordinator_ip),
      coordinator_port_(coordinator_port),
      acceptor_(io_context_,
                asio::ip::tcp::endpoint(
                    asio::ip::address::from_string(ip.c_str()), port_)) {
    easylog::set_min_severity(easylog::Severity::WARNING);
    rpc_coordinator_ = std::make_unique<coro_rpc::coro_rpc_client>();
    async_simple::coro::syncAwait(rpc_coordinator_->connect(
        coordinator_ip_, std::to_string(coordinator_port_)));
}

Client::~Client() { acceptor_.close(); }

// ====== client.cpp —— 替换 Client::set ======
void Client::set(std::string value) {
    // Step 1: Request upload target from coordinator
    auto result = async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_set>(
            std::chrono::seconds{3}, value.size()));
    if (!result) {
        ELOG(ERROR) << "RPC request_set failed";
        return;
    }
    auto response = std::move(result).value();

    // Step 2: 直连 datanode data port (NO RPC!)
    int data_port = response.node_port + SOCKET_PORT_OFFSET;
    ELOG(WARNING) << "[SET] Sending stripe_" << response.stripe_id << " ("
                  << value.size() << "B) to " << response.node_ip << ":"
                  << data_port;

    try {
        asio::ip::tcp::socket socket(io_context_);
        socket.connect(asio::ip::tcp::endpoint(
            asio::ip::make_address(response.node_ip), data_port));

        // 发 header: op + stripe_id + size
        uint8_t op = static_cast<uint8_t>(DataOp::UPLOAD);
        asio::write(socket, asio::buffer(&op, 1));
        uint32_t sid = htonl(response.stripe_id);
        uint32_t sz = htonl(static_cast<uint32_t>(value.size()));
        asio::write(socket, asio::buffer(&sid, 4));
        asio::write(socket, asio::buffer(&sz, 4));

        // 发 body
        asio::write(socket, asio::buffer(value));
        socket.close();
        ELOG(WARNING) << "Send data completely.";
    } catch (const std::exception &e) {
        ELOG(ERROR) << "[SET] failed: " << e.what();
    }
}

void Client::request_repair(unsigned int stripe_id,
                            unsigned int failed_block_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair>(
            std::chrono::seconds{3}, stripe_id, failed_block_id));
}

void Client::request_repair_with_opt(unsigned int stripe_id,
                                     unsigned int failed_block_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair_with_opt>(
            std::chrono::seconds{3}, stripe_id, failed_block_id));
}
void Client::request_repair_no_local_decode(unsigned int stripe_id,
                                            unsigned int failed_block_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_
            ->call_for<&Coordinator::request_repair_no_local_decode>(
                std::chrono::seconds{3}, stripe_id, failed_block_id));
}

void Client::print_node_info() {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::print_node_info>(
            std::chrono::seconds{3}));
}

void Client::print_stripe_info() {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::print_stripe_info>(
            std::chrono::seconds{3}));
}

void Client::delete_file(unsigned int stripe_id, unsigned int failed_block_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::delete_failed_block>(
            std::chrono::seconds{3}, stripe_id, failed_block_id));
}

void Client::delete_node(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::delete_node>(
            std::chrono::seconds{3}, node_id));
}

void Client::clear() {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::clear>(
            std::chrono::seconds{3}));
}
void Client::request_repair_node(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair_node>(
            std::chrono::seconds{30}, node_id));
}

void Client::request_repair_node_with_opt(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair_node_with_opt>(
            std::chrono::seconds{30}, node_id));
}

void Client::request_repair_node_con(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair_node_con>(
            std::chrono::seconds{30}, node_id));
}

void Client::request_repair_node_with_opt_con(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_
            ->call_for<&Coordinator::request_repair_node_with_opt_con>(
                std::chrono::seconds{30}, node_id));
}

void Client::request_repair_node_non_local_decode_con(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_
            ->call_for<&Coordinator::request_repair_node_non_local_decode_con>(
                std::chrono::seconds{30}, node_id));
}

void Client::request_repair_node_non_local_decode(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_
            ->call_for<&Coordinator::request_repair_node_non_local_decode>(
                std::chrono::seconds{30}, node_id));
}

void Client::clear_repair_file() {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::clear_repair_file>(
            std::chrono::seconds{30}));
}

void Client::set_stripe(unsigned int stripe_num) {
    const int value_size = RS_K * BLOCK_SIZE;
    std::string value = generate_random_string(value_size);
    for (unsigned int i = 0; i < stripe_num; i++) {
        set(value);
    }
}

// void Client::time_test() {
//     async_simple::coro::syncAwait(
//         rpc_coordinator_->call_for<&Coordinator::time_test>(
//             std::chrono::seconds{30}));
// }

void Client::repair_node_test() {
    vector<unsigned int> node_ids{1};
    vector<unsigned int> stripe_nums{200};
    for (auto stripe_num : stripe_nums) {
        // ELOG(ERROR) << "stripe_num: " << stripe_num;
        for (auto node_id : node_ids) {
            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node_non_local_decode_con(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));

            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node_con(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));

            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node_with_opt_con(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));

            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node_non_local_decode(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));

            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));

            clear();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 10));
            set_stripe(stripe_num);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 200));
            request_repair_node_with_opt(node_id);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(stripe_num * 50));
        }
    }
}

} // namespace ECProject