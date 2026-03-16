#pragma once
#include <vector>
#include <random>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include "utils.h"
#include "erasure_code.h"
using namespace std;
namespace ECProject {

// 存储每个故障条带的所有可选方案
struct StripeOption {
    int stripeId;
    int failedBlockId;
    vector<vector<vector<int>>> decodeOptions; // [optionIdx][row][col]
    vector<int> stripeDistribution;
};

class MultiStripeRecovery {

public:
    MultiStripeRecovery(int K, int M, int W, int nodeNums, int stripeNums):
        K(K), M(M), W(W), nodeNums(nodeNums), stripeNums(stripeNums) {
        codingMatrix = ErasureCode::cauchy_original_coding_matrix_vector(K, M, W);
        stripe2NodeAssignment = assignStripesToNodes(-1);
        node2StripeAssignment = getNode2StripeAssignment();
    }
    ~MultiStripeRecovery() {}

    vector<int> getNodeLoad(int failedNodeId);
    static double computeLoadBalance(const vector<int>& loads);

    vector<int> getBalancedRecoveryPlan(int failedNodeId);
    double calculateLoadBalanceRatio(const vector<int>& nodeLoad, int failedNodeId);
    vector<int> greedyBalancedSelection(
    vector<StripeOption>& stripeOptions, int failedNodeId);
    vector<int> applyBalancedRecovery(
    int failedNodeId, const vector<int>& selection);


private:
    vector<vector<int>> assignStripesToNodes(unsigned int seed = random_device{}());

    vector<vector<pair<int, int>>> getNode2StripeAssignment();

private:
    int K;
    int M;
    int W;
    int nodeNums;
    int stripeNums;
    vector<vector<int>> codingMatrix;
    vector<vector<int>> stripe2NodeAssignment;
    vector<vector<pair<int,int>>> node2StripeAssignment;
};

}
