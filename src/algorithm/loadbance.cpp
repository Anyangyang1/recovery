#include "loadbalance.h"
#include "sggh.h"
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
    auto decodeMatrixes = sg.generateOptDecodeBitMatrixWithAllMode(-1);          // 采用遍历所有行的方式生成解码方案
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






vector<int> MultiStripeRecovery::getBalancedRecoveryPlan(int failedNodeId) {
    // 故障节点上的条带分布
    auto failedNodeStripes = node2StripeAssignment[failedNodeId];
    
    // 获取所有可选的解码方案（四维数组）
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);
    auto allDecodeMatrixes = sg.generateAllOptDecodeBitMatrixWithAllMode();

    vector<StripeOption> stripeOptions;
    for (auto& failedNodeStripe : failedNodeStripes) {
        int stripeId = failedNodeStripe.first;
        int failedBlockId = failedNodeStripe.second;
        
        StripeOption option;
        option.stripeId = stripeId;
        option.failedBlockId = failedBlockId;
        option.decodeOptions = allDecodeMatrixes[failedBlockId];
        option.stripeDistribution = stripe2NodeAssignment[stripeId];
        
        stripeOptions.push_back(option);
    }
    
    // 用于存储最优方案
    vector<int> bestSelection(stripeOptions.size(), 0);
    double minLoadBalanceRatio = numeric_limits<double>::max();
    vector<int> bestNodeLoad;
    
    // 回溯函数，穷举所有可能的方案组合
    vector<int> currentSelection(stripeOptions.size(), 0);
    
    function<void(int)> backtrack = [&](int idx) {
        if (idx == stripeOptions.size()) {
            // 计算当前方案的节点负载
            vector<int> nodeLoad(nodeNums, 0);
            
            for (size_t i = 0; i < stripeOptions.size(); i++) {
                auto& stripeOpt = stripeOptions[i];
                int selectedOption = currentSelection[i];
                
                // 获取选中的解码矩阵
                auto& decodeMatrix = stripeOpt.decodeOptions[selectedOption];
                auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
                
                // 累加节点负载
                for (size_t j = 0; j < stripeOpt.stripeDistribution.size(); j++) {
                    int nodeId = stripeOpt.stripeDistribution[j];
                    if (nodeId != failedNodeId) {  // 跳过故障节点
                        nodeLoad[nodeId] += decodeRanks[j];
                    }
                }
            }
            
            // 计算负载均衡率
            double loadBalanceRatio = calculateLoadBalanceRatio(nodeLoad, failedNodeId);
            
            // 更新最优方案
            if (loadBalanceRatio < minLoadBalanceRatio) {
                minLoadBalanceRatio = loadBalanceRatio;
                bestSelection = currentSelection;
                bestNodeLoad = nodeLoad;
            }
            
            return;
        }
        
        // 尝试当前条带的所有可选方案
        int numOptions = stripeOptions[idx].decodeOptions.size();
        for (int opt = 0; opt < numOptions; opt++) {
            currentSelection[idx] = opt;
            backtrack(idx + 1);
        }
    };
    
    // 如果组合数太多，使用启发式方法
    long long totalCombinations = 1;
    for (auto& opt : stripeOptions) {
        // cout << "size: " << opt.decodeOptions.size() << endl;
        totalCombinations *= opt.decodeOptions.size();
    }
    if (totalCombinations <= 100000 && totalCombinations > 0) {  // 设置阈值，避免组合爆炸
        backtrack(0);
    } else {
        // 使用贪心启发式方法
        cout << "Too many combinations (" << totalCombinations 
             << "), using greedy heuristic instead" << endl;
        bestSelection = greedyBalancedSelection(stripeOptions, failedNodeId);
        
        // 计算最终负载
        vector<int> nodeLoad(nodeNums, 0);
        for (size_t i = 0; i < stripeOptions.size(); i++) {
            auto& stripeOpt = stripeOptions[i];
            int selectedOption = bestSelection[i];
            auto& decodeMatrix = stripeOpt.decodeOptions[selectedOption];
            auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
            
            for (size_t j = 0; j < stripeOpt.stripeDistribution.size(); j++) {
                int nodeId = stripeOpt.stripeDistribution[j];
                if (nodeId != failedNodeId) {
                    nodeLoad[nodeId] += decodeRanks[j];
                }
            }
        }
        bestNodeLoad = nodeLoad;
        minLoadBalanceRatio = calculateLoadBalanceRatio(nodeLoad, failedNodeId);
    }
    
    cout << "Best load balance ratio: " << minLoadBalanceRatio << endl;
    
    // 返回最优方案：每个条带选择的解码方案索引
    return bestSelection;
}

// 计算负载均衡率（排除故障节点）
double MultiStripeRecovery::calculateLoadBalanceRatio(const vector<int>& nodeLoad, int failedNodeId) {
    vector<int> validLoads;
    for (int i = 0; i < nodeLoad.size(); i++) {
        if (i != failedNodeId && nodeLoad[i] > 0) {
            validLoads.push_back(nodeLoad[i]);
        }
    }
    
    if (validLoads.empty()) return 0.0;
    
    int maxLoad = *max_element(validLoads.begin(), validLoads.end());
    double sumLoad = accumulate(validLoads.begin(), validLoads.end(), 0);
    double avgLoad = sumLoad / validLoads.size();
    
    return (avgLoad > 0) ? (maxLoad / avgLoad) : 0.0;
}

// 贪心启发式选择：每次选择使当前负载均衡率最小的方案
vector<int> MultiStripeRecovery::greedyBalancedSelection(
    vector<StripeOption>& stripeOptions, int failedNodeId) {
    
    vector<int> selection(stripeOptions.size(), 0);
    vector<int> currentLoad(nodeNums, 0);
    
    for (size_t i = 0; i < stripeOptions.size(); i++) {
        auto& stripeOpt = stripeOptions[i];
        int bestOption = 0;
        double minRatio = numeric_limits<double>::max();
        
        // 尝试当前条带的所有可选方案
        for (size_t opt = 0; opt < stripeOpt.decodeOptions.size(); opt++) {
            auto& decodeMatrix = stripeOpt.decodeOptions[opt];
            auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
            
            // 临时计算负载
            vector<int> tempLoad = currentLoad;
            for (size_t j = 0; j < stripeOpt.stripeDistribution.size(); j++) {
                int nodeId = stripeOpt.stripeDistribution[j];
                if (nodeId != failedNodeId) {
                    tempLoad[nodeId] += decodeRanks[j];
                }
            }
            
            double ratio = calculateLoadBalanceRatio(tempLoad, failedNodeId);
            if (ratio < minRatio) {
                minRatio = ratio;
                bestOption = opt;
            }
        }
        
        // 应用最优选择
        selection[i] = bestOption;
        auto& decodeMatrix = stripeOpt.decodeOptions[bestOption];
        auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
        
        for (size_t j = 0; j < stripeOpt.stripeDistribution.size(); j++) {
            int nodeId = stripeOpt.stripeDistribution[j];
            if (nodeId != failedNodeId) {
                currentLoad[nodeId] += decodeRanks[j];
            }
        }
    }
    
    return selection;
}

// 应用最优修复方案，返回各节点负载
vector<int> MultiStripeRecovery::applyBalancedRecovery(
    int failedNodeId, const vector<int>& selection) {
    
    vector<int> nodeLoad(nodeNums, 0);
    auto failedNodeStripes = node2StripeAssignment[failedNodeId];
    
    SimilarityGreedy sg = SimilarityGreedy(K, M, W);
    auto allDecodeMatrixes = sg.generateAllOptDecodeBitMatrixWithAllMode();
    
    size_t idx = 0;
    for (auto& failedNodeStripe : failedNodeStripes) {
        int stripeId = failedNodeStripe.first;
        int failedBlockId = failedNodeStripe.second;
        
        int selectedOption = selection[idx++];
        auto& decodeMatrix = allDecodeMatrixes[failedBlockId][selectedOption];
        auto decodeRanks = SimilarityGreedy::computeBinaryMatrixRank(decodeMatrix, W);
        
        auto stripeDistribution = stripe2NodeAssignment[stripeId];
        for (size_t i = 0; i < stripeDistribution.size(); i++) {
            nodeLoad[stripeDistribution[i]] += decodeRanks[i];
        }
    }
    
    return nodeLoad;
}
