#include "client.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
using namespace ECProject;
int main(int argc, char **argv) {
    Client client("0.0.0.0", CLIENT_PORT, "192.168.1.12", COORDINATOR_PORT);
    std::string value("hello client kdfdedrferererftgrf");
    client.set(value);
    return 0;
}