// // client.cpp
// #include <iostream>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <cstring>
// #include <chrono>

// int main(int argc, char* argv[]) {
//     if (argc != 4) {
//         std::cerr << "Usage: " << argv[0] << " <server_ip> <port>
//         <data_size_MB>\n"; return 1;
//     }

//     std::string server_ip = argv[1];
//     int port = std::stoi(argv[2]);
//     size_t data_size_mb = std::stoull(argv[3]);
//     size_t total_size = data_size_mb * 1024 * 1024;  // MB ¡ú bytes

//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) {
//         perror("socket");
//         return 1;
//     }

//     sockaddr_in addr{};
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(port);
//     inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

//     std::cout << "? Connecting to " << server_ip << ":" << port << "...\n";
//     if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
//         perror("connect");
//         close(sock);
//         return 1;
//     }

//     // Send total size (8 bytes, big-endian)
//     uint64_t size_net = htobe64(total_size);
//     if (send(sock, &size_net, sizeof(size_net), 0) != sizeof(size_net)) {
//         std::cerr << "? Failed to send data size\n";
//         close(sock);
//         return 1;
//     }

//     std::cout << "? Sending " << data_size_mb << " MB data...\n";

//     auto start = std::chrono::steady_clock::now();

//     char buf[65536] = {0};  // dummy data
//     size_t sent = 0;
//     while (sent < total_size) {
//         size_t to_send = std::min(sizeof(buf), total_size - sent);
//         ssize_t n = send(sock, buf, to_send, MSG_NOSIGNAL);
//         if (n <= 0) break;
//         sent += n;
//     }

//     auto end = std::chrono::steady_clock::now();
//     double sec = std::chrono::duration<double>(end - start).count();

//     close(sock);

//     if (sent != total_size) {
//         std::cerr << "? Only sent " << sent << " / " << total_size << "
//         bytes\n"; return 1;
//     }

//     double gbps = (sent * 8.0) / (sec * 1e9);
//     std::cout << "? Sent " << sent / 1024.0 / 1024.0 << " MB in "
//               << sec << " s ¡ú " << gbps << " Gbps\n";
//     return 0;
// }

#include "scoped_timer.hpp"
#include "utils.h"
#include <iostream>
#include <vector>
using namespace std;
void isa_jerasure_test(const int K, const int M, const size_t BLOCK_SIZE);
void generate_opt_decode_matrix_test(const int K, const int M, const int W);
void xor_gen_test(const int k, const size_t block_size);
int64_t xor_gen_and_cpy(const int k, const size_t block_size);
bool access_data(const std::string &key, char *value_buf, size_t value_size);
bool access_data(const std::string &key, char *value_buf,
                 const vector<int> &idxs);
bool store_data(const std::string &key, const char *value, size_t value_size);
bool test_reverse(int w);
int main() {
    for(int i = 1; i <= 16; i++) {
        if(!test_reverse(i)) {
            cout << "not reverse: " << i << endl;
        } else {
            cout << "can all reverse: " << i << endl;
        }
    }
    return 0;
}