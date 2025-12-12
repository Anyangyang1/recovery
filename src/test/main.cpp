#include "loadbalance.h"
#include "sggh.h"
#include "utils.h"
#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <immintrin.h>
#include <iostream>
#include <thread>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
using namespace std;
using namespace ECProject;
void xorTimeTest(unsigned int value_size, bool use_jerasure);
int main() {
    const int MB = 1024 * 1024;
    vector<unsigned int> value_sizes{1, 2, 4, 8, 16, 32, 64};
    for (auto value : value_sizes) {
        xorTimeTest(value * MB, true);
        xorTimeTest(value * MB, false);
    }

    return 0;
}
