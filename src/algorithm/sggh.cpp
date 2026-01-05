#include "sggh.h"
#include "utils.h"
#include "erasure_code.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <unordered_set>
using namespace ECProject;
vector<vector<int>>
SimilarityGreedy::generateOptDecodeBitMatrix(int failedBlock, int mode,
                                             unsigned int seed) {
    auto start = std::chrono::high_resolution_clock::now();
    auto bigMatrix = generateAllDecodingMatrix(failedBlock);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration =
    //     std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    // auto time = duration.count() / 1e6;
    // std::cout << "GenerateAllDecodeMatrix Time: " << time << " ms\n";

    // start = std::chrono::high_resolution_clock::now();
    auto bitMatrix = ErasureCode::matrix2Bitmatrix(bigMatrix, W);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto time = duration.count() / 1e6;
    // std::cout << "GenerateAllDecodeMatrix and matrix2Bitmatrix Time: " << time
    //           << " ms\n";

    // cout << bitMatrix << endl;

    vector<unsigned int> firstSelectSet;
    if (mode == 0) {
        firstSelectSet = {0};
    } else if (mode == -1) {
        firstSelectSet = generateAllRangeN(bitMatrix.size());
    } else {
        firstSelectSet = generateUniqueRandom(bitMatrix.size(), mode, seed);
    }

    vector<vector<int>> bestMatrix;
    int bestMatrixRank = INT32_MAX;
    auto start1 = std::chrono::high_resolution_clock::now();
    for (int r : firstSelectSet) {
        auto optMatrix =
            generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, r);
        auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
        int ranks = getSum(optMatrixRank);
        if (ranks < bestMatrixRank) {
            bestMatrix = optMatrix;
            bestMatrixRank = ranks;
        }
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto time1 = duration1.count() / 1e6;
    // std::cout << "SGGH Time: " << time1 << " ms\n";
    return bestMatrix;
}

vector<vector<vector<int>>>
SimilarityGreedy::generateAllOptDecodeBitMatrix(int failedBlock) {
    auto bigMatrix = generateAllDecodingMatrix(failedBlock);
    auto bitMatrix = ErasureCode::matrix2Bitmatrix(bigMatrix, W);
    vector<unsigned int> firstSelectSet = generateAllRangeN(bitMatrix.size());
    vector<vector<vector<int>>> bestMatrices;
    int minRank = INT32_MAX;
    for (int r : firstSelectSet) {
        auto optMatrix =
            generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, r);
        auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
        int ranks = accumulate(optMatrixRank.begin(), optMatrixRank.end(), 0);
        // cout << "ranks: " << ranks << endl;
        if (ranks < minRank) {
            minRank = ranks;
            bestMatrices.clear();
            bestMatrices.push_back(std::move(optMatrix));
        } else if (ranks == minRank) {
            bestMatrices.push_back(std::move(optMatrix));
        }
        // if (ranks <= minRank) {
        //     cout << "----->" << ranks;
        // }
        // cout << endl;
    }
    return bestMatrices;
}

vector<vector<vector<int>>>
SimilarityGreedy::generateOptDecodeBitMatrixWithAllMode(int mode,
                                                        unsigned int seed) {
    vector<vector<vector<int>>> allModeDecodeMatrix(N);
    for (int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateOptDecodeBitMatrix(i, mode, seed);
    }
    return allModeDecodeMatrix;
}

vector<vector<vector<vector<int>>>>
SimilarityGreedy::generateAllOptDecodeBitMatrixWithAllMode() {
    vector<vector<vector<vector<int>>>> allModeDecodeMatrix(N);
    for (int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateAllOptDecodeBitMatrix(i);
    }
    return allModeDecodeMatrix;
}

vector<vector<int>>
SimilarityGreedy::generateAllDecodingMatrix(int failedBlock) {
    assert(failedBlock >= 0 && failedBlock < N);

    // Step 1: 将 codingMatrix 转为 Jerasure 所需的 int* 格式（row-major, M x
    // K）
    vector<int> codingFlat(M * K);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < K; ++j) {
            codingFlat[i * K + j] = codingMatrix[i][j];
        }
    }

    // Step 2: 构建所有擦除模式：固定擦除 fixedErasedBlock，再从其余
    // totalBlocks-1 中选 M-1 个
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

    // Step 3: 对每种擦除模式，生成解码矩阵，并提取重建 fixedErasedBlock
    // 的那一行
    vector<vector<int>> bigMatrix;

    for (const auto &extraErased : erasePatterns) {
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
            K, M, W, codingFlat.data(), erased.data(), decodeMatrix.data(),
            dmIds.data());

        if (ret != 0) {
            // 解码失败（理论上不应发生，因 Cauchy 矩阵 MDS）
            break;
        }

        vector<int> recoveryCoeffs(N, 0); // 初始化全0
        // 找到哪一行用于重建 fixedErasedBlock
        // 注意：dmIds[j] 表示 decodeMatrix 第 j 行用于重建原始第 j
        // 个数据块（0~K-1） fixedErasedBlock >= K,
        // 修复故障块，需要进行额外的处理
        if (failedBlock >= K) {
            int *decodeMatrixFlat = jerasure_matrix_multiply(
                codingFlat.data(), decodeMatrix.data(), M, K, K, K, W);
            int targetRow = failedBlock - K;
            for (int j = 0; j < K; ++j) {
                int blockIndex = dmIds[j];
                int coeff = decodeMatrixFlat[targetRow * K + j];
                recoveryCoeffs[blockIndex] = coeff;
            }
            free(decodeMatrixFlat);
        } else { // 修复数据块，按正常逻辑处理
            int targetRow =
                failedBlock; // 我们要重建的是第 fixedErasedBlock 个数据块
            // decodeMatrix[targetRow * K + j] 是 dmIds[j] 块的系数
            for (int j = 0; j < K; ++j) {
                int blockIndex = dmIds[j];
                int coeff = decodeMatrix[targetRow * K + j];
                recoveryCoeffs[blockIndex] = coeff;
            }
        }
        bigMatrix.push_back(recoveryCoeffs);
    }
    size_t size = bigMatrix.size();
    // cout << size << endl;
    bigMatrix.resize(size);
    return bigMatrix;
}

vector<vector<vector<int>>>
SimilarityGreedy::generateAllDecodingMatrices(const vector<int> &failedBlocks) {

    int F = failedBlocks.size();
    assert(F > 0 && F <= M && F <= N);
    for (int b : failedBlocks) {
        assert(b >= 0 && b < N);
    }

    // Step 1: flatten codingMatrix (M x K)
    vector<int> codingFlat(M * K);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < K; ++j)
            codingFlat[i * K + j] = codingMatrix[i][j];

    // Step 2: candidate blocks = all except failedBlocks
    vector<int> candidates;
    unordered_set<int> failedSet(failedBlocks.begin(), failedBlocks.end());
    for (int i = 0; i < N; ++i)
        if (!failedSet.count(i))
            candidates.push_back(i);

    int need = M - F; // 需额外擦除的块数（凑足 M）
    assert(static_cast<int>(candidates.size()) >= need);

    // Step 3: generate combinations of size `need` from candidates
    vector<vector<int>> extraEraseCombs;
    vector<int> cur;
    function<void(int)> dfs = [&](int start) {
        if (static_cast<int>(cur.size()) == need) {
            extraEraseCombs.push_back(cur);
            return;
        }
        for (int i = start; i < static_cast<int>(candidates.size()); ++i) {
            cur.push_back(candidates[i]);
            dfs(i + 1);
            cur.pop_back();
        }
    };
    dfs(0);

    // Step 4: for each erasure pattern, compute recovery rows for all failed
    // blocks
    vector<vector<vector<int>>> allRecovery; // [pattern][f_idx][block_id]

    for (const auto &extra : extraEraseCombs) {
        // Build erased array (size N)
        vector<int> erased(N, 0);
        for (int b : failedBlocks)
            erased[b] = 1;
        for (int b : extra)
            erased[b] = 1;

        // Jerasure decoding
        vector<int> decodeMatrix(K * K);
        vector<int> dmIds(K);
        int ret = jerasure_make_decoding_matrix(
            K, M, W, codingFlat.data(), erased.data(), decodeMatrix.data(),
            dmIds.data());
        if (ret != 0)
            continue; // skip invalid (shouldn't happen for MDS)

        // Precompute coding × decodeMatrix if any failed block is parity
        int *codingTimesDecode = nullptr;
        bool hasParity = any_of(failedBlocks.begin(), failedBlocks.end(),
                                [this](int b) { return b >= K; });
        if (hasParity) {
            codingTimesDecode = jerasure_matrix_multiply(
                codingFlat.data(), decodeMatrix.data(), M, K, K, K, W);
        }

        // For each failed block, extract its recovery coefficients
        vector<vector<int>> recoveryForThisPattern; // [f_idx][block_id]
        for (int f : failedBlocks) {
            vector<int> coeffs(N, 0); // default 0
            if (f < K) {
                // Data block: row f of decodeMatrix, mapped by dmIds
                for (int j = 0; j < K; ++j) {
                    int srcBlock = dmIds[j];
                    int coeff = decodeMatrix[f * K + j];
                    coeffs[srcBlock] = coeff;
                }
            } else {
                // Parity block: row (f - K) of codingTimesDecode
                int row = f - K;
                for (int j = 0; j < K; ++j) {
                    int srcBlock = dmIds[j];
                    int coeff = codingTimesDecode[row * K + j];
                    coeffs[srcBlock] = coeff;
                }
            }
            recoveryForThisPattern.push_back(move(coeffs));
        }

        allRecovery.push_back(move(recoveryForThisPattern));
        if (codingTimesDecode)
            free(codingTimesDecode);
    }

    return allRecovery;
}

vector<int>
SimilarityGreedy::computeBinaryMatrixRank(vector<vector<int>> &bitMatrix,
                                          int W) {
    int col = bitMatrix[0].size() / W;
    vector<vector<int>> matrix(W, vector<int>(W));
    vector<int> ranks(col);
    for (int k = 0; k < col; k++) {
        for (int i = 0; i < W; i++) {
            for (int j = 0; j < W; j++) {
                matrix[i][j] = bitMatrix[i][k * W + j];
            }
        }
        ranks[k] = ECProject::computeBinaryMatrixRank(matrix);
    }
    return ranks;
}

void SimilarityGreedy::updateXorClosure(set<int> &closure, int val) {
    if (val == 0)
        return;
    vector<int> snapshot(closure.begin(), closure.end());
    for (int x : snapshot) {
        closure.insert(x ^ val);
    }
    closure.insert(val);
}
void SimilarityGreedy::updateXorClosures(vector<set<int>> &closures,
                                         const vector<int> &candidate) {
    for (size_t i = 0; i < closures.size(); i++) {
        updateXorClosure(closures[i], candidate[i]);
    }
}
int SimilarityGreedy::computeSimilarity(const vector<std::set<int>> &closures,
                                        const vector<int> &candidate) {
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
    const vector<vector<int>> &bitMatrix, int firstSelect) {
    vector<vector<int>> intMatrix =
        ECProject::bitMatrixToIntMatrix(bitMatrix, W);
    vector<set<int>> closures(N, set<int>());
    vector<bool> isRecovered(W, false);
    vector<vector<int>> optDecodeMatrix(W, vector<int>(N, 0));
    // cout << firstSelect << " ";
    int leftRecoveredConut = W - 1;
    int firstGroup = firstSelect % W;
    isRecovered[firstGroup] = true;
    updateXorClosures(closures, intMatrix[firstSelect]);
    for (int i = 0; i < N; i++) {
        optDecodeMatrix[firstGroup][i] = intMatrix[firstSelect][i];
    }
    // cout << firstSelect << " ";
    // auto mi = ECProject::intMatrixToBitMatrix(optDecodeMatrix, W);
    // std::cout << mi[firstGroup] << endl;
    while (leftRecoveredConut > 0) {
        int selectIdx = -1;
        int maxSimilar = -1;
        for (size_t i = 0; i < intMatrix.size(); i++) {
            if (isRecovered[i % W]) {
                continue;
            } else {
                int similar = computeSimilarity(closures, intMatrix[i]);
                if (similar > maxSimilar) {
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
        for (int i = 0; i < N; i++) {
            optDecodeMatrix[group][i] = intMatrix[selectIdx][i];
        }
    }
    // cout << endl;
    return ECProject::intMatrixToBitMatrix(optDecodeMatrix, W);
}