#include "client.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
using namespace ECProject;
std::string generate_random_string(size_t length) {
    static const char charset[] = "0123456789"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(
        0, sizeof(charset) - 2); // -2: exclude trailing '\0'

    std::string str(length, 0);
    for (size_t i = 0; i < length; ++i) {
        str[i] = charset[dis(gen)];
    }
    return str;
}
int main(int argc, char **argv) {
    Client client("0.0.0.0", CLIENT_PORT, "192.168.1.12", COORDINATOR_PORT);
    const int value_size = RS_K * BLOCK_SIZE;
    try {
        string cmd = argv[1];
        if (cmd == "set") {
            unsigned int stripe_num = stoi(argv[2]);
            for (unsigned int i = 0; i < stripe_num; i++) {
                std::string value = generate_random_string(value_size);
                client.set(value);
            }
        } else if (cmd == "repair") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.request_repair(stripe_id, block_id);
        } else if (cmd == "repair_opt") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.request_repair_with_opt(stripe_id, block_id);
        } else if (cmd == "print_stripe_info") {
            client.print_stripe_info();
        } else if (cmd == "print_node_info") {
            client.print_node_info();
        } else if (cmd == "delete_stripe") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.delete_file(stripe_id, block_id);
        } else if (cmd == "delete_node") {
            unsigned int node_id = stoi(argv[2]);
            client.delete_all_file(node_id);
        } else if (cmd == "repair_node") {
            unsigned int node_id = stoi(argv[2]);
            client.request_repair_node(node_id);
        } else if (cmd == "repair_node_opt") {
            unsigned int node_id = stoi(argv[2]);
            client.request_repair_node_with_opt(node_id);
        } else {
            ELOG(ERROR) << "cmd error";
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }

    return 0;
}