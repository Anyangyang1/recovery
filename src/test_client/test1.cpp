#include "loadbalance.h"
#include "sggh.h"
#include "utils.h"
#include <asio.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
using namespace std;
using namespace ECProject;

void test2() {
    const int K = 4, M = 3, W = 8;
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);
    auto matrixs = sg.generateOptDecodeBitMatrixWithAllMode(0);
    for(auto matrix: matrixs) {
        auto ranks = SimilarityGreedy::computeBinaryMatrixRank(matrix, W);
        cout << "ranks: " << ranks << endl;;
        cout << getSum(ranks) << endl;
    }
}


void reducePacketsTest() {
    vector<int> KK{12};
    vector<int> MM{3, 4};
    vector<int> NUM{1};
    const int W = 8, failedBlock = 0;
    for (int num : NUM) {
        for (int M : MM) {
            for (int K : KK) {
                SimilarityGreedy sg = SimilarityGreedy(K, M, W);
                auto start1 = std::chrono::high_resolution_clock::now();
                // auto decodeBitMatrix1 =
                // sg.generateOptDecodeBitMatrix(failedBlock, num, 0);
                auto decodeBitMatrix1 =
                    sg.generateOptDecodeBitMatrix(failedBlock, 0);

                auto end1 = std::chrono::high_resolution_clock::now();
                auto duration1 =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end1 - start1);
                auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(
                    decodeBitMatrix1, W);
                auto time1 = duration1.count() / 1e6;
                auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
                // cout << decodeBitMatrix << endl;
                cout << "ranks: " << ranks1 << endl;
                cout << "origin packets: " << K * W << endl;
                cout << "need packets: " << rankSum1 << endl;
                std::cout << "duration: " << time1 << " ms\n";
                cout << "^^^^^^^^^^^^^^K,M,W,FailedBlock,num^^^^^^^^^^^^^^" << K
                     << "," << M << "," << W << "," << num << endl;
            }
        }
    }
}

vector<double> reducePacketsTest(int K, int M, int W, int failedBlock) {
    // 生成 Cauchy 编码矩阵
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix1 = sg.generateOptDecodeBitMatrix(failedBlock);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto ranks1 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix1, W);
    auto time1 = duration1.count() / 1e6;
    auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
    cout << "***********K,M,W,FailedBlock(Only One)***********" << K << "," << M
         << "," << W << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks1 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum1 << endl;
    std::cout << "duration: " << time1 << " ms\n";

    auto start2 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix2 = sg.generateOptDecodeBitMatrix(failedBlock, 0);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
    auto ranks2 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix2, W);
    auto time2 = duration2.count() / 1e6;
    auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
    cout << "***********K,M,W,FailedBlock(ALL)***********" << K << "," << M
         << "," << W << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks2 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum2 << endl;
    std::cout << "duration: " << time2 << " ms\n";

    auto start3 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix3 = sg.generateOptDecodeBitMatrix(failedBlock, 100, 42);
    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3);
    auto ranks3 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix3, W);
    auto time3 = duration3.count() / 1e6;
    auto rankSum3 = accumulate(ranks3.begin(), ranks3.end(), 0);
    cout << "***********K,M,W,FailedBlock(Random)***********" << K << "," << M
         << "," << W << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks3 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum3 << endl;
    std::cout << "duration: " << time3 << " ms\n";

    vector<double> data{
        (double)K * W, (double)rankSum1, time1, (double)rankSum2,
        time2,         (double)rankSum3, time3};
    return data;
}

void loadBalanceTest(int K, int M, int W, int N, int S, int failedNodeId) {
    MultiStripeRecovery msr = MultiStripeRecovery(K, M, W, N, S);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto nodeReload = msr.getNodeLoad(failedNodeId);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto time1 = duration1.count() / 1e6;

    cout << "nodeReload: " << nodeReload << endl;
    cout << "sum: " << getSum(nodeReload) << endl;
    cout << "loadBanlance: "
         << MultiStripeRecovery::computeLoadBalance(nodeReload) << endl;
    std::cout << "duration: " << time1 << " ms\n";
}

void xorTimeTest(unsigned int value_size, bool use_jerasure) {

    // auto aligned_alloc_char = [](size_t n) {
    //     void *p = aligned_alloc(32, n); // C11，POSIX 保证
    //     if (!p)
    //         throw std::bad_alloc();
    //     return std::unique_ptr<char[], decltype(&free)>(static_cast<char
    //     *>(p),
    //                                                     free);
    // };

    // auto value1 = aligned_alloc_char(value_size);
    // std::fill_n(value1.get(), value_size, 'a');

    // auto value2 = aligned_alloc_char(value_size);
    // std::fill_n(value2.get(), value_size, 'b');

    // auto value3 = aligned_alloc_char(value_size);
    std::vector<char> value1(value_size, 'a');
    std::vector<char> value2(value_size, 'b');
    std::vector<char> value3(value_size, 0);
    // printf("v1 align: %ld, v2 align: %ld, v3 align: %ld\n",
    // (uintptr_t)value1.data() % 32,
    // (uintptr_t)value2.data() % 32,
    // (uintptr_t)value3.data() % 32);
    {

        SCOPED_TIMER("test");
        if (use_jerasure) {
            galois_region_xor(value1.data(), value2.data(), value3.data(),
                              value_size);
        } else {
            for (size_t i = 0; i < value_size; i++) {
                value3[i] = value1[i] ^ value2[i];
            }
        }
    }
}

int add(int a, int b) {
    cout << a + b << endl;
    return a + b;
}

void test1() {
    vector<int> KK{24, 48, 72, 96};
    vector<int> MM{3, 4};
    vector<int> NUM{1};
    const int W = 8, failedBlock = 0;
    for (int num : NUM) {
        for (int M : MM) {
            for (int K : KK) {
                SimilarityGreedy sg = SimilarityGreedy(K, M, W);
                auto start1 = std::chrono::high_resolution_clock::now();
                auto decodeBitMatrix1 =
                    sg.generateOptDecodeBitMatrix(failedBlock, num, 0);
                // auto decodeBitMatrix1 =
                // sg.generateOptDecodeBitMatrix(failedBlock, 0);

                auto end1 = std::chrono::high_resolution_clock::now();
                auto duration1 =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end1 - start1);
                auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(
                    decodeBitMatrix1, W);
                auto time1 = duration1.count() / 1e6;
                auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
                cout << "***********K,M,W,NUM,FailedBlock(Only One)***********"
                     << K << "," << M << "," << W << "," << num << endl;
                // cout << decodeBitMatrix << endl;
                cout << "ranks: " << ranks1 << endl;
                cout << "origin packets: " << K * W << endl;
                cout << "need packets: " << rankSum1 << endl;
                std::cout << "duration: " << time1 << " ms\n";
            }
        }
    }
}