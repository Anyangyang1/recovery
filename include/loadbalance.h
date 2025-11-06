#ifndef LOADBALANCE_H
#define LOADBALANCE_H
#include <vector>
#include <random>
#include "../include/utils.h"
using namespace std;
namespace ECProject {

class MultiStripeRecovery {

public:
    MultiStripeRecovery(int K, int M, int W, int nodeNums, int stripeNums):
        K(K), M(M), W(W), nodeNums(nodeNums), stripeNums(stripeNums) {
        codingMatrix = cauchy_original_coding_matrix_vector(K, M, W);
        stripe2NodeAssignment = assignStripesToNodes();
        node2StripeAssignment = getNode2StripeAssignment();
    }
    ~MultiStripeRecovery() {}

    vector<int> getNodeLoad(int failedNodeId);
    static double computeLoadBalance(const vector<int>& loads);
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

#endif