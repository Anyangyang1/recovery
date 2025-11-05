// #include <iostream>
// #include <vector>
// #include <set>
// #include <bitset>
// #include <algorithm>
// #include <cassert>
// #include <cstring>
// #include <cmath>
// #include <numeric>
// #include <chrono>
// #include <random>
// #include <fstream>
// #include "../include/utils.h"
// // C 接口
// extern "C" {
// #include "../Jerasure-1.2A/jerasure.h"
// #include "../Jerasure-1.2A/cauchy.h"
// }
#include <iostream>
#include "../include/sggh.h"
#include "../include/utils.h"
using namespace std;
using namespace ECProject;
// /**
//  * @brief 将 S 个条带、每个条带 (K+M) 个块，随机分配到 N 个节点
//  *
//  * @param S: 条带数量
//  * @param K: 数据块数 per stripe
//  * @param M: 校验块数 per stripe
//  * @param N: 节点数量 (必须 N >= 1)
//  * @param seed: 随机种子（可选，用于可复现结果）
//  * @return vector<vector<int>>: assignment[s][b] = node_id
//  */
// vector<vector<int>> assignStripesToNodes(
//     int S, int K, int M, int N, 
//     unsigned int seed = std::random_device{}()
// ) {
//     assert(S > 0 && K > 0 && M >= 0 && N > 0);
//     const int blocksPerStripe = K + M;

//     // 初始化随机数生成器
//     std::mt19937 rng(seed);

//     // 结果：S x (K+M)
//     vector<vector<int>> assignment(S, vector<int>(blocksPerStripe));

//     // 对每个条带独立分配
//     for (int s = 0; s < S; ++s) {
//         // 方法 1：简单随机（允许节点负载不均）
//         // for (int b = 0; b < blocksPerStripe; ++b) {
//         //     assignment[s][b] = rng() % N;
//         // }

//         // 方法 2：更均衡的随机（推荐）—— 对每个条带，打乱节点顺序后循环分配
//         vector<int> nodes(N);
//         std::iota(nodes.begin(), nodes.end(), 0); // [0, 1, ..., N-1]
//         std::shuffle(nodes.begin(), nodes.end(), rng);

//         for (int b = 0; b < blocksPerStripe; ++b) {
//             assignment[s][b] = nodes[b % N];
//         }
//     }

//     return assignment;
// }


// /**
//  * @brief 将条带存储的方式改为节点存储的方式。
//  *
//  * @param assignment: 条带存储方式   Stripe 0: 4 11 2 3 10 8 
//  * @param S: 条带数
//  * @param blocksPerStripe: 块数 per stripe
//  * @param N: 节点数量 (必须 N >= 1)
//  * @return vector<vector<std::pair<int, int>>: nodeBlocks[i]表示节点i存储的所有块数集合，是一个数组
//  *          数组中的元素是个<stripe_id, block_id>元素对
//  */
// vector<vector<std::pair<int, int>>> getNodeToBlocks(
//     const vector<vector<int>>& assignment,
//     int blocksPerStripe, int N
// ) {
//     vector<vector<std::pair<int, int>>> nodeBlocks(N);
//     for (int s = 0; s < assignment.size(); ++s) {
//         for (int b = 0; b < blocksPerStripe; ++b) {
//             int node = assignment[s][b];
//             nodeBlocks[node].emplace_back(s, b); // (stripe_id, block_id)
//         }
//     }
//     return nodeBlocks;
// }


// /**
//  * @brief 根据条带的分布情况，以及节点的故障情况，计算每个节点参与修复时的负载
//  *
//  * @param assigment: 条带分布情况
//  * @param codingMatrix: M x W 的编码矩阵
//  * @param W: 有限域 W
//  * @param N: 节点数量 (必须 N >= 1)
//  * @param seed: 随机种子（可选，用于可复现结果）
//  * @return vector<vector<int>>: assignment[s][b] = node_id
//  */

// vector<int> getNodeReload(vector<vector<int>> assigment, vector<vector<int>> codingMatrix, int W, int N, int failedNodeId) {
//     const int K = codingMatrix[0].size();
//     const int M = codingMatrix.size();
//     auto nodeBlocks = getNodeToBlocks(assigment, K + M, N);
//     vector<int> nodeReload(N, 0);
//     // 故障节点上的条带分布
//     auto failedNodeStripes = nodeBlocks[failedNodeId];
//     auto decodeMatrixes = generateOptDecodeBitMatrixWithAllMode(codingMatrix, W, 0);
//     cout << "failedNodeStripes.size(): " <<  failedNodeStripes.size() << endl;
//     for(auto failedNodeStripe: failedNodeStripes) {
//         int stripeId = failedNodeStripe.first;
//         int failedBlockId = failedNodeStripe.second;
//         // printf("recover strip: %d, block: %d  ", stripeId, failedBlockId);
//         auto decodeMatrix = decodeMatrixes[failedBlockId];
//         auto decodeRanks = computeBinaryMatrixRank(decodeMatrix, W);
//         auto stripeDistribution = assigment[stripeId];
//         for(int i = 0; i < stripeDistribution.size(); i++) {
//             nodeReload[stripeDistribution[i]] += decodeRanks[i];
//         }
//         // cout << "nodeReload: " << nodeReload << endl;
//     }
//     return nodeReload;
// }



// double computeLoadBalance(const vector<int>& loads) {
//     if (loads.empty()) return 0.0;

//     double sum = std::accumulate(loads.begin(), loads.end(), 0.0);
//     double mean = sum / (loads.size() - 1);
//     double maxLoad = *std::max_element(loads.begin(), loads.end());

//     if (mean == 0) return 0.0;
//     return maxLoad / mean;
// }


// vector<double> reducePacketsTest(int K, int M, int W, int failedBlock) {
//     // 生成 Cauchy 编码矩阵
//     vector<vector<int>> codingMatrix = cauchy_original_coding_matrix_vector(K, M, W);
//     auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);
//     auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);

//     auto start1 = std::chrono::high_resolution_clock::now();
//     auto decodeBitMatrix1 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock);
//     auto end1 = std::chrono::high_resolution_clock::now();
//     auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
//     auto ranks1 = computeBinaryMatrixRank(decodeBitMatrix1, W);
//     auto time1 = duration1.count() / 1e6;
//     auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
//     cout << "***********K,M,W,FailedBlock(Only One)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
//     // cout << decodeBitMatrix << endl;
//     cout << "ranks: " << ranks1 << endl;
//     cout << "origin packets: " << K * W << endl;
//     cout << "need packets: " << rankSum1 << endl;
//     std::cout << "duration: " << time1 << " ms\n";

//     auto start2 = std::chrono::high_resolution_clock::now();
//     auto decodeBitMatrix2 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock, 0);
//     auto end2 = std::chrono::high_resolution_clock::now();
//     auto duration2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
//     auto ranks2 = computeBinaryMatrixRank(decodeBitMatrix2, W);
//     auto time2 = duration2.count() / 1e6;
//     auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
//     cout << "***********K,M,W,FailedBlock(ALL)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
//     // cout << decodeBitMatrix << endl;
//     cout << "ranks: " << ranks2 << endl;
//     cout << "origin packets: " << K * W << endl;
//     cout << "need packets: " << rankSum2 << endl;
//     std::cout << "duration: " << time2 << " ms\n";

//     auto start3 = std::chrono::high_resolution_clock::now();
//     auto decodeBitMatrix3 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock, 100, 42);
//     auto end3 = std::chrono::high_resolution_clock::now();
//     auto duration3 = std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3);
//     auto ranks3 = computeBinaryMatrixRank(decodeBitMatrix3, W);
//     auto time3 = duration3.count() / 1e6;
//     auto rankSum3 = accumulate(ranks3.begin(), ranks3.end(), 0);
//     cout << "***********K,M,W,FailedBlock(Random)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
//     // cout << decodeBitMatrix << endl;
//     cout << "ranks: " << ranks3 << endl;
//     cout << "origin packets: " << K * W << endl;
//     cout << "need packets: " << rankSum3 << endl;
//     std::cout << "duration: " << time3 << " ms\n";

//     vector<double> data {(double)K * W, (double)rankSum1, time1, (double)rankSum2, time2, (double)rankSum3, time3};
//     return data;
// }

// void loadBalanceTest(int K, int M, int W, int N, int S, int failedNodeId) {
//     auto assignment = assignStripesToNodes(S, K, M, N, 45); // 固定种子便于调试
//     cout << "stripe assigment:" << endl << assignment << endl;
//     auto matrix = cauchy_original_coding_matrix_vector(K, M, W);

//     auto start1 = std::chrono::high_resolution_clock::now();
//     auto nodeReload = getNodeReload(assignment, matrix, W, N, failedNodeId);
//     auto end1 = std::chrono::high_resolution_clock::now();
//     auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
//     auto time1 = duration1.count() / 1e6;

//     cout << "nodeReload: " << nodeReload << endl;
//     cout << "sum: " << getSum(nodeReload) << endl;
//     cout << "loadBanlance: " << computeLoadBalance(nodeReload) << endl;
//     std::cout << "duration: " << time1 << " ms\n";
// }


// ======================
// 主函数
// ======================

// int main() {
    // int K = 6, M = 4, W = 8, failedBlock = 0, N = 20, S = 100;
    // loadBalanceTest(K, M, W, N, S, 0);
    // vector<vector<int>> codingMatrix = cauchy_original_coding_matrix_vector(K, M, W);
    // auto allDecode = generateAllOptDecodeBitMatrixWithAllMode(codingMatrix, W);
    // int num = 0;
    // for(auto dd: allDecode) {
    //     cout << "--------------------------------------------------------------------------------------------------------" << num++ << endl;
    //     for(auto decodeBitMatrix: dd) {
    //         auto ranks2 = computeBinaryMatrixRank(decodeBitMatrix, W);
    //         auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
    //         cout << "***********K,M,W,FailedBlock(ALL)***********" << K << "," << M << "," << W << endl; 
    //         cout << decodeBitMatrix << endl;
    //         cout << "ranks: " << ranks2 << endl;
    //         cout << "origin packets: " << K * W << endl;
    //         cout << "need packets: " << rankSum2 << endl;
    //     }
    // }
    // cout << comb(11,8)*8 << endl;
    // loadBalanceTest(6,3,8,30,600, 0);
//     return 0;
// }

int main() {
    SimilarityGreedy sg = SimilarityGreedy(6,3,8);
    vector<vector<int>> opt = sg.generateOptDecodeBitMatrix(4);
    cout << opt << endl;
    return 0;
}