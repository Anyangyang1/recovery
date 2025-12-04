#include "datanode.h"
#include <iostream>
using namespace ECProject;
int main(int argc, char **argv) {
    ECProject::Datanode local_node("127.0.0.1",
                                   9080); // ½öÓÃÆä read_from_datanode

    // std::string value = "hello world hello python";
    // bool ok = local_node.write_to_datanode("192.168.1.13", 8080, "test.dat",
    // value.data(), value.size()); if(ok) {
    //     cout << "write successful" << endl;
    // }

    auto value = std::make_unique<char[]>(10);
    bool ok = local_node.read_from_datanode("192.168.1.13", 8080, "test.dat",
                                            value.get(), 10);
    if (ok) {
        cout << "read data: " << value << endl;
    }

    return 0;
}