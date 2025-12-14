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
    easylog::set_min_severity(easylog::Severity::WARNING);
    rpc_coordinator_ = std::make_unique<coro_rpc::coro_rpc_client>();
    async_simple::coro::syncAwait(rpc_coordinator_->connect(
        coordinator_ip_, std::to_string(coordinator_port_)));
}

Client::~Client() { acceptor_.close(); }

void Client::set(std::string value) {
    // Step 1: Request upload target from coordinator
    auto result = async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_set>(
            std::chrono::seconds{3}, value.size()));

    if (!result) {
        ELOG(ERROR) << "RPC request_set failed: " << result.error();
        return; // 或其他 fallback
    }
    auto response = std::move(result).value();

    // Step 2: Connect to target datanode (RPC port)
    auto rpc_client = std::make_unique<coro_rpc::coro_rpc_client>();
    {
        auto conn_res = async_simple::coro::syncAwait(rpc_client->connect(
            response.node_ip, std::to_string(response.node_port)));
        if (conn_res) {
            ELOG(ERROR) << "Failed to connect datanode RPC ("
                        << response.node_ip << ":" << response.node_port
                        << "): " << conn_res.message();
            return;
        }
    }

    // Step 3: Notify datanode to prepare for upload
    {
        auto call_res = async_simple::coro::syncAwait(
            rpc_client->call<&Datanode::handle_upload>(response.stripe_id,
                                                       value.size()));
        if (!call_res) {
            ELOG(ERROR) << "RPC handle_upload failed: " << call_res.error();
            return;
        }
    }

    // Step 4: Send raw data via socket (data port = node_port + offset)
    int data_port = response.node_port + SOCKET_PORT_OFFSET;
    ELOG(WARNING) << "[SET] Sending stripe_" << response.stripe_id << " ("
                  << value.size() << "B) to " << response.node_ip << ":"
                  << data_port;
    try {
        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::endpoint endpoint(
            asio::ip::make_address(response.node_ip), data_port);

        std::error_code ec;
        socket.connect(endpoint, ec);
        if (ec) {
            ELOG(ERROR) << "[SET] Connect failed to " << response.node_ip << ":"
                        << data_port << ", ec: [" << ec.value() << "] "
                        << ec.message();
            return;
        }

        // 使用 error_code 版本的 write
        size_t n = asio::write(socket, asio::buffer(value), ec);
        if (ec || n != value.size()) {
            ELOG(ERROR) << "[SET] Write failed: expected " << value.size()
                        << "B, wrote " << n << "B"
                        << ", ec: [" << ec.value() << "] "
                        << ec.category().name() << ": " << ec.message();
            socket.close(); // 显式 close（ec 时可能已失效，但 safe）
            return;
        }

        socket.close(ec); // close 也可检查（通常忽略）
        ELOG(WARNING) << "Send data completely (" << n << "B).";
        ELOG(WARNING) << "first data: " << value.substr(0, 1024);
    } catch (const std::exception &e) {
        ELOG(ERROR) << "[SET] Data transfer failed: " << e.what();
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
            std::chrono::seconds{3}, node_id));
}

void Client::request_repair_node_with_opt(unsigned int node_id) {
    async_simple::coro::syncAwait(
        rpc_coordinator_->call_for<&Coordinator::request_repair_node_with_opt>(
            std::chrono::seconds{3}, node_id));
}
void Client::set_data_test(std::string value) {
    // Step 2: Connect to target datanode (RPC port)
    auto rpc_client = std::make_unique<coro_rpc::coro_rpc_client>();
    {
        auto conn_res = async_simple::coro::syncAwait(
            rpc_client->connect("192.168.1.14", "8888"));
        if (conn_res) {
            ELOG(ERROR) << "Failed to connect datanode RPC";
            return;
        }
    }

    // Step 3: Notify datanode to prepare for upload
    {
        auto call_res = async_simple::coro::syncAwait(
            rpc_client->call<&Datanode::handle_upload_test>(0, value.size()));
        if (!call_res) {
            ELOG(ERROR) << "RPC handle_upload failed: " << call_res.error();
            return;
        }
    }

    // Step 4: Send raw data via socket (data port = node_port + offset)
    int data_port = 8888 + SOCKET_PORT_OFFSET;
    ELOG(WARNING) << "[SET] Sending data (" << value.size() << "B) to";
    try {
        asio::ip::tcp::socket socket(io_context_);
        asio::ip::tcp::endpoint endpoint(asio::ip::make_address("192.168.1.14"),
                                         data_port);
        socket.connect(endpoint);
        asio::write(socket, asio::buffer(value, value.size()));
        socket.close();
        ELOG(WARNING) << "Send data completely.";
    } catch (const std::exception &e) {
        ELOG(ERROR) << "[SET] Data transfer failed: " << e.what();
    }
}

} // namespace ECProject