#include "datanode.h"
#include "ylt/easylog.hpp"
#include <iostream>
using namespace ECProject;
int main(int argc, char **argv) {
    ECProject::Datanode datanode("0.0.0.0",
                                   8888); // ½öÓÃÆä read_from_datanode
    datanode.run();
    
    
    return 0;
}