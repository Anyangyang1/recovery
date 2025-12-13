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

std::string generateBlocks(int k, int block_size) {
    // “™«Û£∫k °› 1, block_size °› 0£ª«“ k °‹ 9£®±‹√‚ ˝◊÷◊÷∑˚“Á≥ˆ£©
    assert(k >= 1 && "k must be at least 1");
    assert(block_size >= 0 && "block_size must be non-negative");
    assert(k <= 9 && "k > 9 not supported for digit characters; use letters or custom mapping");

    std::string result;
    result.reserve(static_cast<size_t>(k) * block_size); // ‘§∑÷≈‰±‹√‚∂‡¥Œ realloc

    for (int i = 1; i <= k; ++i) {
        char c = '0' + i;  // '1', '2', ..., '9'
        result.append(block_size, c);  // append `block_size` copies of `c`
    }
    return result;
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
            client.delete_node(node_id);
        } else if(cmd == "clear") {
            client.clear();
        } else if (cmd == "repair_node") {
            unsigned int node_id = stoi(argv[2]);
            client.request_repair_node(node_id);
        } else if (cmd == "repair_node_opt") {
            unsigned int node_id = stoi(argv[2]);
            client.request_repair_node_with_opt(node_id);
        }else if(cmd == "data_test") {
            unsigned int stripe_num = stoi(argv[2]);
            for (unsigned int i = 0; i < stripe_num; i++) {
                std::string value = generate_random_string(value_size);
                client.set_data_test(value);
            }
        } else {
            ELOG(ERROR) << "cmd error";
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }

    return 0;
}