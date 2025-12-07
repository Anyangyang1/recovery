#include "client.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
using namespace ECProject;
std::string generate_random_string(size_t length) {
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2); // -2: exclude trailing '\0'

    std::string str(length, 0);
    for (size_t i = 0; i < length; ++i) {
        str[i] = charset[dis(gen)];
    }
    return str;
}
int main(int argc, char **argv) {
    Client client("0.0.0.0", CLIENT_PORT, "192.168.1.12", COORDINATOR_PORT);
    const int k = 2;
    const int m = 1;
    const int block_size = 512;
    const int value_size = k * block_size;
    std::string value = generate_random_string(value_size);
    ELOG(DEBUG) << "value: " << value;
    client.set(value);
    return 0;
}