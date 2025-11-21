#pragma once
#include<vector>
#include<set>
#include<random>
#include "utils.h"
using namespace std;
namespace ECProject {
class SimilarityGreedy {
public:
    SimilarityGreedy(int K, int M, int W):K(K), M(M), W(W), N(K + M) {
        codingMatrix = ECProject::cauchy_original_coding_matrix_vector(K, M, W);
    }
    ~SimilarityGreedy() {}

    vector<vector<int>> generateOptDecodeBitMatrix(
        int failedBlock, int mode = -1, unsigned int seed = std::random_device{}());
    
    vector<vector<vector<int>>> generateAllOptDecodeBitMatrix(int failedBlock);

    vector<vector<vector<int>>> generateOptDecodeBitMatrixWithAllMode(
        int mode = -1, unsigned int seed = std::random_device{}());

    vector<vector<vector<vector<int>>>> generateAllOptDecodeBitMatrixWithAllMode();
    static vector<int> computeBinaryMatrixRank(vector<vector<int>>& bitMmatrix, int W);

private:
    vector<vector<int>> generateAllDecodingMatrix(int failedBlock);
    void updateXorClosure(set<int>& closure, int val);
    void updateXorClosures(vector<set<int>>& closures, const vector<int>& candidate);
    int computeSimilarity(const vector<std::set<int>>& closures, const vector<int>& candidate);
    vector<vector<int>> generateOptDecodeBitMatrixWithFirstSelect(
        const vector<vector<int>>& bitMatrix, int firstSelect);
    
    

private:
    int K;
    int M;
    int W;
    int N;
    vector<vector<int>> codingMatrix;
};

}
