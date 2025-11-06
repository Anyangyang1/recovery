#include <iostream>
#include <chrono>
#include "../include/sggh.h"
#include "../include/utils.h"
#include "../include/loadbalance.h"
using namespace std;
using namespace ECProject;

vector<double> reducePacketsTest(int K, int M, int W, int failedBlock) {
    // Éú³É Cauchy ±àÂë¾ØÕó
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix1 = sg.generateOptDecodeBitMatrix(failedBlock);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix1, W);
    auto time1 = duration1.count() / 1e6;
    auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
    cout << "***********K,M,W,FailedBlock(Only One)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks1 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum1 << endl;
    std::cout << "duration: " << time1 << " ms\n";

    auto start2 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix2 = sg.generateOptDecodeBitMatrix(failedBlock, 0);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
    auto ranks2 = SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix2, W);
    auto time2 = duration2.count() / 1e6;
    auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
    cout << "***********K,M,W,FailedBlock(ALL)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks2 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum2 << endl;
    std::cout << "duration: " << time2 << " ms\n";

    auto start3 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix3 = sg.generateOptDecodeBitMatrix(failedBlock, 100, 42);
    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 = std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3);
    auto ranks3 = SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix3, W);
    auto time3 = duration3.count() / 1e6;
    auto rankSum3 = accumulate(ranks3.begin(), ranks3.end(), 0);
    cout << "***********K,M,W,FailedBlock(Random)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks3 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum3 << endl;
    std::cout << "duration: " << time3 << " ms\n";

    vector<double> data {(double)K * W, (double)rankSum1, time1, (double)rankSum2, time2, (double)rankSum3, time3};
    return data;
}

void loadBalanceTest(int K, int M, int W, int N, int S, int failedNodeId) {
    MultiStripeRecovery msr = MultiStripeRecovery(K, M, W, N, S);
    
    auto start1 = std::chrono::high_resolution_clock::now();
    auto nodeReload = msr.getNodeLoad(failedNodeId);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto time1 = duration1.count() / 1e6;

    cout << "nodeReload: " << nodeReload << endl;
    cout << "sum: " << getSum(nodeReload) << endl;
    cout << "loadBanlance: " << MultiStripeRecovery::computeLoadBalance(nodeReload) << endl;
    std::cout << "duration: " << time1 << " ms\n";
}


int main() {
    const int K = 48, M = 3, W = 8, failedBlock = 0;
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);

    auto start1 = std::chrono::high_resolution_clock::now();
    // auto decodeBitMatrix1 = sg.generateOptDecodeBitMatrix(failedBlock, 300, 0);
    auto decodeBitMatrix1 = sg.generateOptDecodeBitMatrix(failedBlock, 0);
    
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix1, W);
    auto time1 = duration1.count() / 1e6;
    auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
    cout << "***********K,M,W,FailedBlock(Only One)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks1 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum1 << endl;
    std::cout << "duration: " << time1 << " ms\n";
    return 0;
}