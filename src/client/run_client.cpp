#include "client.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
using namespace ECProject;


std::string generateBlocks(int k, int block_size) {
    // 要求：k ≥ 1, block_size ≥ 0；且 k ≤ 9（避免数字字符溢出）
    assert(k >= 1 && "k must be at least 1");
    assert(block_size >= 0 && "block_size must be non-negative");
    assert(k <= 9 && "k > 9 not supported for digit characters; use letters or "
                     "custom mapping");

    std::string result;
    result.reserve(static_cast<size_t>(k) *
                   block_size); // 预分配避免多次 realloc

    for (int i = 1; i <= k; ++i) {
        char c = '0' + i;             // '1', '2', ..., '9'
        result.append(block_size, c); // append `block_size` copies of `c`
    }
    return result;
}
int main(int argc, char **argv) {
    Client client("0.0.0.0", CLIENT_PORT, COORDINATOR_IP, COORDINATOR_PORT);
    try {
        string cmd = argv[1];
        if (cmd == "set") {
            int k = stoi(argv[2]);
            int block_size = stoi(argv[3]);
            unsigned int stripe_num = stoi(argv[4]);
            client.set_stripe(k, block_size * MB, stripe_num);
        } else if (cmd == "repair") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.request_repair(stripe_id, block_id);
        } else if (cmd == "repair_opt") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.request_repair_with_opt(stripe_id, block_id);
        } else if (cmd == "repair_no_local") {
            unsigned int stripe_id = stoi(argv[2]);
            unsigned int block_id = stoi(argv[3]);
            client.request_repair_no_local_decode(stripe_id, block_id);
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
        } else if (cmd == "clear") {
            client.clear_repair_file();
        } else if (cmd == "repair_node") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node(node_id) << std::endl;
        } else if (cmd == "repair_node_no_local") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node_non_local_decode(node_id) << std::endl;
        } else if (cmd == "repair_node_opt") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node_with_opt(node_id) << std::endl;
        } else if (cmd == "repair_node_con") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node_con(node_id) << std::endl;
        } else if (cmd == "repair_node_opt_con") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node_with_opt_con(node_id) << std::endl;
        } else if (cmd == "repair_node_no_local_con") {
            client.clear_repair_file();
            unsigned int node_id = stoi(argv[2]);
            std::cout << client.request_repair_node_non_local_decode_con(node_id) << std::endl;
        } else {
            ELOG(ERROR) << "cmd error";
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }

    return 0;
}