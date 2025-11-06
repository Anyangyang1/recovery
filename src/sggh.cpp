#include<cassert>
#include "../include/sggh.h"
#include "../include/utils.h"
using namespace ECProject;
vector<vector<int>> SimilarityGreedy::generateOptDecodeBitMatrix(
    int failedBlock, int mode, unsigned int seed) {
    auto bigMatrix = generateAllDecodingMatrix(failedBlock);
    auto bitMatrix = matrix2Bitmatrix(bigMatrix, W);
    cout << "bitMatrix.size(): " << bitMatrix.size() << endl;
    vector<int> firstSelectSet;
    if(mode == -1) {
        firstSelectSet = {0};
    }
    else if(mode == 0) {
        firstSelectSet = generateAllRangeN(bitMatrix.size());
    }
    else {
        firstSelectSet = generateUniqueRandom(bitMatrix.size(), mode, seed);
    } 

    vector<vector<int>> bestMatrix;
    int bestMatrixRank = INT32_MAX;
    for(int r: firstSelectSet) {
        auto optMatrix = generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, r);
        auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
        int ranks = getSum(optMatrixRank);
        // cout << "ranks: " << ranks << endl;
        if(ranks < bestMatrixRank) {
            bestMatrix = optMatrix;
            bestMatrixRank = ranks;
        }
    }
    return bestMatrix;
}

vector<vector<vector<int>>> SimilarityGreedy::generateAllOptDecodeBitMatrix(int failedBlock) {
    auto bigMatrix = generateAllDecodingMatrix(failedBlock);
    auto bitMatrix = matrix2Bitmatrix(bigMatrix, W);
    vector<int> firstSelectSet = generateAllRangeN(bitMatrix.size());
    vector<vector<vector<int>>> bestMatrices;
    int minRank = INT32_MAX;
    for(int r: firstSelectSet) {
        auto optMatrix = generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, r);
        auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
        int ranks = accumulate(optMatrixRank.begin(), optMatrixRank.end(), 0);
        // cout << "ranks: " << ranks << endl;
        if(ranks < minRank) {
            minRank = ranks;
            bestMatrices.clear();
            bestMatrices.push_back(std::move(optMatrix));
        } else if(ranks == minRank) {
            bestMatrices.push_back(std::move(optMatrix));
        }
        if(ranks <= minRank) {
            cout << "----->" << ranks;
        }
        cout << endl;
    }
    return bestMatrices;
}

vector<vector<vector<int>>> SimilarityGreedy::generateOptDecodeBitMatrixWithAllMode(
    int mode, unsigned int seed) {
    vector<vector<vector<int>>> allModeDecodeMatrix(N);
    for(int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateOptDecodeBitMatrix(i, mode, seed);
    }
    return allModeDecodeMatrix;
}

vector<vector<vector<vector<int>>>> SimilarityGreedy::generateAllOptDecodeBitMatrixWithAllMode() {
    vector<vector<vector<vector<int>>>> allModeDecodeMatrix(N);
    for(int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateAllOptDecodeBitMatrix(i);
    }
    return allModeDecodeMatrix;
}

vector<vector<int>> SimilarityGreedy::generateAllDecodingMatrix(int failedBlock) {
    assert(failedBlock >= 0 && failedBlock < N);

    // Step 1: 将 codingMatrix 转为 Jerasure 所需的 int* 格式（row-major, M x K）
    vector<int> codingFlat(M * K);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < K; ++j) {
            codingFlat[i * K + j] = codingMatrix[i][j];
        }
    }

    // Step 2: 构建所有擦除模式：固定擦除 fixedErasedBlock，再从其余 totalBlocks-1 中选 M-1 个
    vector<int> otherBlocks;
    for (int i = 0; i < N; ++i) {
        if (i != failedBlock) {
            otherBlocks.push_back(i);
        }
    }

    // 生成组合（简单递归 or 迭代）
    vector<vector<int>> erasePatterns;
    vector<int> current;
    int patternCount = 0;

    std::function<void(int)> dfs = [&](int start) {
        if (current.size() == static_cast<size_t>(M - 1)) {
            erasePatterns.push_back(current);
            patternCount++;
            return;
        }
        for (size_t i = start; i < otherBlocks.size(); ++i) {
            current.push_back(otherBlocks[i]);
            dfs(i + 1);
            current.pop_back();
        }
    };
    dfs(0);

    // Step 3: 对每种擦除模式，生成解码矩阵，并提取重建 fixedErasedBlock 的那一行
    vector<vector<int>> bigMatrix;

    for (const auto& extraErased : erasePatterns) {
        // 构建 erased 数组
        vector<int> erased(N, 0);
        erased[failedBlock] = 1;
        for (int blk : extraErased) {
            erased[blk] = 1;
        }

        // 调用 Jerasure 生成解码矩阵
        vector<int> decodeMatrix(K * K);
        vector<int> dmIds(K);
        int ret = jerasure_make_decoding_matrix(
            K, M, W,
            codingFlat.data(),
            erased.data(),
            decodeMatrix.data(),
            dmIds.data()
        );

        if (ret != 0) {
            // 解码失败（理论上不应发生，因 Cauchy 矩阵 MDS）
            break;
        }

        vector<int> recoveryCoeffs(N, 0); // 初始化全0
        // 找到哪一行用于重建 fixedErasedBlock
        // 注意：dmIds[j] 表示 decodeMatrix 第 j 行用于重建原始第 j 个数据块（0~K-1）
        // fixedErasedBlock >= K, 修复故障块，需要进行额外的处理
        if (failedBlock >= K) {
            int* decodeMatrixFlat = jerasure_matrix_multiply(codingFlat.data(), decodeMatrix.data(), M, K, K, K, W);
            int targetRow = failedBlock - K;
            for (int j = 0; j < K; ++j) {
                int blockIndex = dmIds[j];
                int coeff = decodeMatrixFlat[targetRow * K + j];
                recoveryCoeffs[blockIndex] = coeff;
            }
            free(decodeMatrixFlat);
        }
        else {          // 修复数据块，按正常逻辑处理
            int targetRow = failedBlock; // 我们要重建的是第 fixedErasedBlock 个数据块
            // decodeMatrix[targetRow * K + j] 是 dmIds[j] 块的系数
            for (int j = 0; j < K; ++j) {
                int blockIndex = dmIds[j];
                int coeff = decodeMatrix[targetRow * K + j];
                recoveryCoeffs[blockIndex] = coeff;
            }
        }
        bigMatrix.push_back(recoveryCoeffs);
    }
    return bigMatrix;

}

vector<int> SimilarityGreedy::computeBinaryMatrixRank(vector<vector<int>>& bitMatrix, int W) {
    int col = bitMatrix[0].size() / W;
    vector<vector<int>> matrix(W, vector<int>(W));
    vector<int> ranks(col);
    for(int k = 0; k < col; k++) {
        for(int i = 0; i < W; i++) {
            for(int j = 0; j < W; j++) {
                matrix[i][j] = bitMatrix[i][k * W + j];
            }
        }
        ranks[k] = ECProject::computeBinaryMatrixRank(matrix);
    }
    return ranks;
}

void SimilarityGreedy::updateXorClosure(set<int>& closure, int val) {
    if (val == 0) return;
    vector<int> snapshot(closure.begin(), closure.end());
    for (int x : snapshot) {
        closure.insert(x ^ val);
    }
    closure.insert(val);
}
void SimilarityGreedy::updateXorClosures(vector<set<int>>& closures, const vector<int>& candidate) {
    for(size_t i = 0; i < closures.size(); i++) {
        updateXorClosure(closures[i], candidate[i]);
    }
}
int SimilarityGreedy::computeSimilarity(const vector<std::set<int>>& closures, const vector<int>& candidate) {
    int sim = 0;
    for (size_t i = 0; i < closures.size(); ++i) {
        int val = candidate[i];
        if (val != 0 && closures[i].find(val) != closures[i].end()) {
            sim++;
        }
    }
    return sim;
}
vector<vector<int>> SimilarityGreedy::generateOptDecodeBitMatrixWithFirstSelect(
    const vector<vector<int>>& bitMatrix, int firstSelect) {
    vector<vector<int>> intMatrix = ECProject::bitMatrixToIntMatrix(bitMatrix, W);
    vector<set<int>> closures(N, set<int>());
    vector<bool> isRecovered(W, false);
    vector<vector<int>> optDecodeMatrix(W, vector<int>(N, 0));
    // cout << firstSelect << " ";
    int leftRecoveredConut = W - 1;
    int firstGroup = firstSelect % W;
    isRecovered[firstGroup] = true;
    updateXorClosures(closures, intMatrix[firstSelect]);
    for(int i = 0; i < N; i++) {
        optDecodeMatrix[firstGroup][i] = intMatrix[firstSelect][i];
    }
    // auto mi = ECProject::intMatrixToBitMatrix(optDecodeMatrix, W);
    // std::cout << mi[firstGroup] << endl;
    while(leftRecoveredConut > 0) {
        int selectIdx = -1;
        int maxSimilar = -1;
        for(size_t i = 0; i < intMatrix.size(); i++) {
            if(isRecovered[i % W]) {
                continue;
            }
            else {
                int similar = computeSimilarity(closures, intMatrix[i]);
                if(similar > maxSimilar) {
                    maxSimilar = similar;
                    selectIdx = i;
                }
            }
        }
        // cout << selectIdx << " ";
        int group = selectIdx % W;
        isRecovered[group] = true;
        leftRecoveredConut--;
        updateXorClosures(closures, intMatrix[selectIdx]);
        for(int i = 0; i < N; i++) {
            optDecodeMatrix[group][i] = intMatrix[selectIdx][i];
        }
    }
    // cout << endl;
    return ECProject::intMatrixToBitMatrix(optDecodeMatrix, W);
}