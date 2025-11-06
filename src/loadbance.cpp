#include "../include/loadbalance.h"
#include "../include/sggh.h"
using namespace ECProject;
/**
 * @brief 根据条带的分布情况，以及节点的故障情况，计算每个节点参与修复时的负载
 *
 * @param assigment: 条带分布情况
 * @param codingMatrix: M x W 的编码矩阵
 * @param W: 有限域 W
 * @param N: 节点数量 (必须 N >= 1)
 * @param seed: 随机种子（可选，用于可复现结果）
 * @return vector<vector<int>>: assignment[s][b] = node_id
 */
vector<int> MultiStripeRecovery::getNodeLoad(int failedNodeId) {
    vector<int> nodeReload(nodeNums, 0);
    // 故障节点上的条带分布
    auto failedNodeStripes = node2StripeAssignment[failedNodeId];
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);
    auto decodeMatrixes = sg.generateOptDecodeBitMatrixWithAllMode(0);          // 采用遍历所有行的方式生成解码方案
    cout << "failedNodeStripes.size(): " <<  failedNodeStripes.size() << endl;
    for(auto failedNodeStripe: failedNodeStripes) {
        int stripeId = failedNodeStripe.first;
        int failedBlockId = failedNodeStripe.second;
        // printf("recover strip: %d, block: %d  ", stripeId, failedBlockId);
        auto decodeMatrix = decodeMatrixes[failedBlockId];
        auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
        auto stripeDistribution = stripe2NodeAssignment[stripeId];
        for(size_t i = 0; i < stripeDistribution.size(); i++) {
            nodeReload[stripeDistribution[i]] += decodeRanks[i];
        }
        // cout << "nodeReload: " << nodeReload << endl;
    }
    return nodeReload;
}
double MultiStripeRecovery::computeLoadBalance(const vector<int>& loads) {
    if (loads.empty()) return 0.0;

    double sum = std::accumulate(loads.begin(), loads.end(), 0.0);
    double mean = sum / (loads.size() - 1);
    double maxLoad = *std::max_element(loads.begin(), loads.end());

    if (mean == 0) return 0.0;
    return maxLoad / mean;
}



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
vector<vector<int>> MultiStripeRecovery::assignStripesToNodes(unsigned int seed) {
    const int blocksPerStripe = K + M;

    // 初始化随机数生成器
    std::mt19937 rng(seed);

    // 结果：S x (K+M)
    vector<vector<int>> assignment(stripeNums, vector<int>(blocksPerStripe));

    // 对每个条带独立分配
    for (int s = 0; s < stripeNums; ++s) {
        // 方法 1：简单随机（允许节点负载不均）
        // for (int b = 0; b < blocksPerStripe; ++b) {
        //     assignment[s][b] = rng() % N;
        // }

        // 方法 2：更均衡的随机（推荐）—— 对每个条带，打乱节点顺序后循环分配
        vector<int> nodes(nodeNums);
        std::iota(nodes.begin(), nodes.end(), 0); // [0, 1, ..., N-1]
        std::shuffle(nodes.begin(), nodes.end(), rng);

        for (int b = 0; b < blocksPerStripe; ++b) {
            assignment[s][b] = nodes[b % nodeNums];
        }
    }
    return assignment;
}

/**
 * @brief 将条带存储的方式改为节点存储的方式。
 *
 * @param assignment: 条带存储方式   Stripe 0: 4 11 2 3 10 8 
 * @param S: 条带数
 * @param blocksPerStripe: 块数 per stripe
 * @param N: 节点数量 (必须 N >= 1)
 * @return vector<vector<std::pair<int, int>>: nodeBlocks[i]表示节点i存储的所有块数集合，是一个数组
 *          数组中的元素是个<stripe_id, block_id>元素对
 */
vector<vector<pair<int, int>>> MultiStripeRecovery::getNode2StripeAssignment() {
    const int blocksPerStripe = K + M;
    vector<vector<std::pair<int, int>>> nodeBlocks(nodeNums);
    for (int s = 0; s < stripeNums; ++s) {
        for (int b = 0; b < blocksPerStripe; ++b) {
            int node = stripe2NodeAssignment[s][b];
            nodeBlocks[node].emplace_back(s, b); // (stripe_id, block_id)
        }
    }
    return nodeBlocks;
}
