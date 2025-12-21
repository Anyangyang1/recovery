// server.cpp
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "? Server listening on port " << port << "...\n";

    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    // Read total data size (8 bytes, big-endian)
    uint64_t total_size = 0;
    if (recv(client_fd, &total_size, sizeof(total_size), MSG_WAITALL) != sizeof(total_size)) {
        std::cerr << "? Failed to read data size\n";
        close(client_fd);
        close(server_fd);
        return 1;
    }
    total_size = be64toh(total_size);  // network ¡ú host

    std::cout << "? Receiving " << total_size / 1024.0 / 1024.0 << " MB data...\n";

    auto start = std::chrono::steady_clock::now();

    char buf[65536];
    size_t received = 0;
    while (received < total_size) {
        size_t to_read = std::min(sizeof(buf), total_size - received);
        ssize_t n = recv(client_fd, buf, to_read, 0);
        if (n <= 0) break;
        received += n;
    }

    auto end = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();

    close(client_fd);
    close(server_fd);

    if (received != total_size) {
        std::cerr << "? Only received " << received << " / " << total_size << " bytes\n";
        return 1;
    }

    double gbps = (received * 8.0) / (sec * 1e9);
    std::cout << "? Received " << received / 1024.0 / 1024.0 << " MB in "
              << sec << " s ¡ú " << gbps << " Gbps\n";
    return 0;
}