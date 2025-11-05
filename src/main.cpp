#include <iostream>
#include <vector>
#include <set>
#include <bitset>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cmath>
#include <numeric>
#include <chrono>
#include <random>
#include <fstream>

// C 接口（假设 jerasure 和 cauchy 已正确链接）
extern "C" {
#include "../Jerasure-1.2A/jerasure.h"
#include "../Jerasure-1.2A/cauchy.h"
}

using namespace std;

// ======================
// 工具函数
// ======================

// 高斯消元求 0-1 矩阵的秩
int computeBinaryMatrixRank(vector<vector<int>> matrix) {
    int rows = matrix.size();
    if (rows == 0) return 0;
    int cols = matrix[0].size();
    int rank = 0;

    for (int col = 0; col < cols && rank < rows; ++col) {
        // 找主元
        int pivot = -1;
        for (int r = rank; r < rows; ++r) {
            if (matrix[r][col] == 1) {
                pivot = r;
                break;
            }
        }
        if (pivot == -1) continue;

        // 交换行
        swap(matrix[rank], matrix[pivot]);

        // 消元
        for (int r = 0; r < rows; ++r) {
            if (r != rank && matrix[r][col] == 1) {
                for (int c = 0; c < cols; ++c) {
                    matrix[r][c] ^= matrix[rank][c];
                }
            }
        }
        ++rank;
    }
    return rank;
}

template<class T>
ostream& operator<<(ostream& os, vector<T>& vc) {
    for(auto c: vc) {
        cout << c << " ";
    }
    return os;
}

template<class T>
ostream& operator<<(ostream& os, vector<vector<T>>& vc) {
    for(auto row: vc) {
        for(auto r: row) {
            cout << r << " ";
        }
        cout << endl;
    }
    return os;
}


std::vector<std::vector<int>> generateAllDecodingMatrix(
    int K, int M, int W,
    const std::vector<std::vector<int>>& codingMatrix,
    int fixedErasedBlock
) {
    const int totalBlocks = K + M;
    assert(fixedErasedBlock >= 0 && fixedErasedBlock < totalBlocks);

    // Step 1: 将 codingMatrix 转为 Jerasure 所需的 int* 格式（row-major, M x K）
    std::vector<int> codingFlat(M * K);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < K; ++j) {
            codingFlat[i * K + j] = codingMatrix[i][j];
        }
    }

    // Step 2: 构建所有擦除模式：固定擦除 fixedErasedBlock，再从其余 totalBlocks-1 中选 M-1 个
    std::vector<int> otherBlocks;
    for (int i = 0; i < totalBlocks; ++i) {
        if (i != fixedErasedBlock) {
            otherBlocks.push_back(i);
        }
    }

    // 生成组合（简单递归 or 迭代）
    std::vector<std::vector<int>> erasePatterns;
    std::vector<int> current;
    int patternCount = 0;

    std::function<void(int)> dfs = [&](int start) {
        if (current.size() == static_cast<size_t>(M - 1)) {
            erasePatterns.push_back(current);
            patternCount++;
            return;
        }
        for (int i = start; i < static_cast<int>(otherBlocks.size()); ++i) {
            current.push_back(otherBlocks[i]);
            dfs(i + 1);
            current.pop_back();
        }
    };
    dfs(0);

    // Step 3: 对每种擦除模式，生成解码矩阵，并提取重建 fixedErasedBlock 的那一行
    std::vector<std::vector<int>> bigMatrix;

    for (const auto& extraErased : erasePatterns) {
        // 构建 erased 数组
        std::vector<int> erased(totalBlocks, 0);
        erased[fixedErasedBlock] = 1;
        for (int blk : extraErased) {
            erased[blk] = 1;
        }

        // 调用 Jerasure 生成解码矩阵
        std::vector<int> decodeMatrix(K * K);
        std::vector<int> dmIds(K);
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

        std::vector<int> recoveryCoeffs(totalBlocks, 0); // 初始化全0
        // 找到哪一行用于重建 fixedErasedBlock
        // 注意：dmIds[j] 表示 decodeMatrix 第 j 行用于重建原始第 j 个数据块（0~K-1）
        // fixedErasedBlock >= K, 修复故障块，需要进行额外的处理
        if (fixedErasedBlock >= K) {
            int* decodeMatrixFlat = jerasure_matrix_multiply(codingFlat.data(), decodeMatrix.data(), M, K, K, K, W);
            int targetRow = fixedErasedBlock - K;
            for (int j = 0; j < K; ++j) {
                int blockIndex = dmIds[j];
                int coeff = decodeMatrixFlat[targetRow * K + j];
                recoveryCoeffs[blockIndex] = coeff;
            }
            free(decodeMatrixFlat);
        }
        else {          // 修复数据块，按正常逻辑处理
            int targetRow = fixedErasedBlock; // 我们要重建的是第 fixedErasedBlock 个数据块
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

/**
 * @brief 使用 Jerasure 将整数矩阵转为标准位矩阵（W×W per element）
 *
 * @param mat: R x C 的整数矩阵
 * @param W: 字长
 * @return: 位矩阵，维度 (R*W) x (C*W)
 */
std::vector<std::vector<int>> jerasureStyleBitMatrix(
    const std::vector<std::vector<int>>& mat,
    int W
) {
    size_t R = mat.size();
    if (R == 0) return {};
    size_t C = mat[0].size();

    // 转为 flat int array (row-major)
    std::vector<int> flat(R * C);
    for (size_t i = 0; i < R; ++i) {
        for (size_t j = 0; j < C; ++j) {
            flat[i * C + j] = mat[i][j];
        }
    }

    // 调用 Jerasure
    int* bitPtr = jerasure_matrix_to_bitmatrix(C, R, W, flat.data());
    if (!bitPtr) {
        throw std::runtime_error("jerasure_matrix_to_bitmatrix failed");
    }

    // 转为 vector<vector<int>>: (R*W) x (C*W)
    size_t outRows = R * W;
    size_t outCols = C * W;
    std::vector<std::vector<int>> result(outRows, std::vector<int>(outCols));

    for (size_t i = 0; i < outRows; ++i) {
        for (size_t j = 0; j < outCols; ++j) {
            result[i][j] = bitPtr[i * outCols + j];
        }
    }

    free(bitPtr); // Jerasure 使用 malloc
    return result;
}


/**
 * @brief 将简化位矩阵（每行 R x (C*W)）转换为整数矩阵（R x C）
 *
 * @param bitMatrix: R 行，每行长度为 C * W，元素为 0/1
 * @param W: 每组位数
 * @return std::vector<std::vector<int>>: R x C 的整数矩阵
 */
std::vector<std::vector<int>> bitMatrixToIntMatrix(
    const std::vector<std::vector<int>>& bitMatrix,
    int W
) {
    if (bitMatrix.empty()) return {};

    size_t R = bitMatrix.size();
    size_t totalBits = bitMatrix[0].size();
    assert(totalBits % W == 0);
    size_t C = totalBits / W;

    std::vector<std::vector<int>> intMatrix(R, std::vector<int>(C, 0));

    for (size_t i = 0; i < R; ++i) {
        for (size_t j = 0; j < C; ++j) {
            int val = 0;
            // 从高位到低位（bitMatrix[i][j*W] 是最高位）
            for (int b = 0; b < W; ++b) {
                val = (val << 1) | bitMatrix[i][j * W + b];
            }
            intMatrix[i][j] = val;
        }
    }
    return intMatrix;
}

std::vector<std::vector<int>> intMatrixToBitMatrix(
    const std::vector<std::vector<int>>& intMatrix,
    int W
) {
    if (intMatrix.empty()) return {};

    size_t R = intMatrix.size();
    size_t C = intMatrix[0].size();
    std::vector<std::vector<int>> bitMatrix(R, std::vector<int>(C * W, 0));

    for (size_t i = 0; i < R; ++i) {
        for (size_t j = 0; j < C; ++j) {
            std::vector<int> bits(W, 0);
            // 从最高位开始填充
            for (int b = 0; b < W; ++b) {
                // 检查第 (W - 1 - i) 位是否为 1
                bits[b] = (intMatrix[i][j] >> (W - 1 - b)) & 1;
            }
            for (int b = 0; b < W; ++b) {
                bitMatrix[i][j * W + b] = bits[b];
            }
        }
    }
    return bitMatrix;
}


void updateXorClosure(std::set<int>& closure, int val) {
    if (val == 0) return;
    std::vector<int> snapshot(closure.begin(), closure.end());
    for (int x : snapshot) {
        closure.insert(x ^ val);
    }
    closure.insert(val);
}

void updateXorClosures(vector<set<int>>& closures, vector<int>& candidate) {
    for(int i = 0; i < closures.size(); i++) {
        updateXorClosure(closures[i], candidate[i]);
    }
}



int computeSimilarity(const std::vector<int>& candidate,
                      const std::vector<std::set<int>>& closures) {
    int N = closures.size();
    int sim = 0;
    for (int i = 0; i < N; ++i) {
        int val = candidate[i];
        if (val != 0 && closures[i].find(val) != closures[i].end()) {
            sim++;
        }
    }
    return sim;
}


// std::vector<std::vector<int>> generateOptDecodeBitMatrix(vector<vector<int>>& bitMatrix, int K, int M, int W) {
//     const int N = K + M;
//     vector<vector<int>> intMatrix = bitMatrixToIntMatrix(bitMatrix, W);
//     vector<set<int>> closures(N, set<int>());
//     vector<bool> isRecovered(W, false);
//     vector<vector<int>> optDecodeMatrix(W, vector<int>(N, 0));
//     int leftRecoveredConut = W;
//     while(leftRecoveredConut > 0) {
//         int selectIdx = -1;
//         int maxSimilar = -1;
//         for(int i = 0; i < intMatrix.size(); i++) {
//             if(isRecovered[i % W]) {
//                 continue;
//             }
//             else {
//                 int similar = computeSimilarity(intMatrix[i], closures);
//                 if(similar > maxSimilar) {
//                     maxSimilar = similar;
//                     selectIdx = i;
//                 }
//             }
//         }
//         int group = selectIdx % W;
//         isRecovered[group] = true;
//         leftRecoveredConut--;
//         updateXorClosures(closures, intMatrix[selectIdx]);
//         for(int i = 0; i < N; i++) {
//             optDecodeMatrix[group][i] = intMatrix[selectIdx][i];
//         }
//     }
//     return intMatrixToBitMatrix(optDecodeMatrix, W);
// }


std::vector<std::vector<int>> generateOptDecodeBitMatrixWithFirstSelect(vector<vector<int>>& bitMatrix, int K, int M, int W, int firstSelect) {
    const int N = K + M;
    vector<vector<int>> intMatrix = bitMatrixToIntMatrix(bitMatrix, W);
    vector<set<int>> closures(N, set<int>());
    vector<bool> isRecovered(W, false);
    vector<vector<int>> optDecodeMatrix(W, vector<int>(N, 0));
    
    int leftRecoveredConut = W - 1;
    int firstGroup = firstSelect % W;
    isRecovered[firstGroup] = true;
    updateXorClosures(closures, intMatrix[firstSelect]);
    for(int i = 0; i < N; i++) {
        optDecodeMatrix[firstGroup][i] = intMatrix[firstSelect][i];
    }
    auto mi = intMatrixToBitMatrix(optDecodeMatrix, W);
    // std::cout << mi[firstGroup] << endl;

    while(leftRecoveredConut > 0) {
        int selectIdx = -1;
        int maxSimilar = -1;
        for(int i = 0; i < intMatrix.size(); i++) {
            if(isRecovered[i % W]) {
                continue;
            }
            else {
                int similar = computeSimilarity(intMatrix[i], closures);
                if(similar > maxSimilar) {
                    maxSimilar = similar;
                    selectIdx = i;
                }
            }
        }
        int group = selectIdx % W;
        isRecovered[group] = true;
        leftRecoveredConut--;
        updateXorClosures(closures, intMatrix[selectIdx]);
        for(int i = 0; i < N; i++) {
            optDecodeMatrix[group][i] = intMatrix[selectIdx][i];
        }
    }
    return intMatrixToBitMatrix(optDecodeMatrix, W);
}




vector<int> computeBinaryMatrixRank(vector<vector<int>>& bitMatrix, int W) {
    int col = bitMatrix[0].size() / W;
    vector<vector<int>> matrix(W, vector<int>(W));
    vector<int> ranks(col);
    for(int k = 0; k < col; k++) {
        for(int i = 0; i < W; i++) {
            for(int j = 0; j < W; j++) {
                matrix[i][j] = bitMatrix[i][k * W + j];
            }
        }
        ranks[k] = computeBinaryMatrixRank(matrix);
    }
    return ranks;
}

// /**
//  * @brief 生成最优解码位矩阵
//  *
//  * @param codingMatrix: M x K 的编码矩阵
//  * @param W: 有限域
//  * @param failedBlock: 故障块号
//  * @return std::vector<std::vector<int>>: W x ((K + M)*W) 的解码位矩阵 
//  */
// std::vector<std::vector<int>> generateOptDecodeBitMatrixOnlyOne(std::vector<std::vector<int>> codingMatrix, int W, int failedBlock) {
//     const int K = codingMatrix[0].size();
//     const int M = codingMatrix.size();
//     // 故障块如果是校验块，则直接返回对应的编码矩阵
//     if(failedBlock >= K) {
//         int rowId = failedBlock - K;
//         vector<vector<int>> matrix = vector<vector<int>>(1, vector<int>(K + M));
//         for(int i = 0; i < K; i++) {
//             matrix[0][i] = codingMatrix[rowId][i];
//         }
//         return jerasureStyleBitMatrix(matrix, W);
//     }

//     auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);

//     // 假设 bigMatrix 是 generateAllDecodingMatrix 的输出
//     auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);
//     return generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, K, M, W, 0);
// }


// std::vector<std::vector<int>> generateOptDecodeBitMatrixWithAll(std::vector<std::vector<int>> codingMatrix, int W, int failedBlock) {
//     const int K = codingMatrix[0].size();
//     const int M = codingMatrix.size();
//     // 故障块如果是校验块，则直接返回对应的编码矩阵
//     if(failedBlock >= K) {
//         int rowId = failedBlock - K;
//         vector<vector<int>> matrix = vector<vector<int>>(1, vector<int>(K + M));
//         for(int i = 0; i < K; i++) {
//             matrix[0][i] = codingMatrix[rowId][i];
//         }
//         return jerasureStyleBitMatrix(matrix, W);
//     }

//     auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);

//     // 假设 bigMatrix 是 generateAllDecodingMatrix 的输出
//     auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);
//     vector<vector<int>> bestMatrix;
//     int bestMatrixRank = INT32_MAX;
//     for(int i = 0; i < bitMatrix.size(); i++) {
//         auto optMatrix = generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, K, M, W, i);
//         auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
//         int ranks = accumulate(optMatrixRank.begin(), optMatrixRank.end(), 0);
//         cout << "ranks: " << ranks << endl;
//         if(ranks < bestMatrixRank) {
//             bestMatrix = optMatrix;
//             bestMatrixRank = ranks;
//         }
//     }
//     return bestMatrix;
// }

std::vector<int> generateUniqueRandom(int N, int K, unsigned int seed = std::random_device{}()) {
    if (K > N || K < 0) {
        // throw std::invalid_argument("K must be between 0 and N");
        K = N;
    }

    // 创建 0 到 N-1 的序列
    std::vector<int> nums(N);
    std::iota(nums.begin(), nums.end(), 0); // 填充 0,1,2,...,N-1

    // 随机打乱
    std::mt19937 rng(seed);
    std::shuffle(nums.begin(), nums.end(), rng);

    // 取前 K 个
    nums.resize(K);
    return nums;
}

std::vector<int> generateAllRangeN(int N) {
    if (N <= 0) return {};
    std::vector<int> result(N);
    std::iota(result.begin(), result.end(), 0);
    return result;
}

std::vector<std::vector<int>> generateOptDecodeBitMatrix(std::vector<std::vector<int>> codingMatrix,
                            int W, int failedBlock, int mode = -1, unsigned int seed = std::random_device{}()) {
    const int K = codingMatrix[0].size();
    const int M = codingMatrix.size();
    auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);
    auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);
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
        auto optMatrix = generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, K, M, W, r);
        auto optMatrixRank = computeBinaryMatrixRank(optMatrix, W);
        int ranks = accumulate(optMatrixRank.begin(), optMatrixRank.end(), 0);
        // cout << "ranks: " << ranks << endl;
        if(ranks < bestMatrixRank) {
            bestMatrix = optMatrix;
            bestMatrixRank = ranks;
        }
    }
    return bestMatrix;
}


std::vector<std::vector<std::vector<int>>> generateAllOptDecodeBitMatrix(std::vector<std::vector<int>> codingMatrix,
                            int W, int failedBlock) {
    const int K = codingMatrix[0].size();
    const int M = codingMatrix.size();
    auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);
    auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);
    vector<int> firstSelectSet = generateAllRangeN(bitMatrix.size());
    vector<vector<vector<int>>> bestMatrices;
    int minRank = INT32_MAX;
    for(int r: firstSelectSet) {
        auto optMatrix = generateOptDecodeBitMatrixWithFirstSelect(bitMatrix, K, M, W, r);
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
    }
    return bestMatrices;
}


vector<vector<vector<int>>> generateOptDecodeBitMatrixWithAllMode(vector<vector<int>> codingMatrix, int W, 
                            int mode = -1, unsigned int seed = std::random_device{}()) {
    const int K = codingMatrix[0].size();
    const int M = codingMatrix.size();
    const int N = K + M;
    vector<vector<vector<int>>> allModeDecodeMatrix(N);
    for(int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateOptDecodeBitMatrix(codingMatrix, W, i, mode, seed);
    }
    return allModeDecodeMatrix;
}


vector<vector<vector<vector<int>>>> generateAllOptDecodeBitMatrixWithAllMode(vector<vector<int>> codingMatrix, int W) {
    const int K = codingMatrix[0].size();
    const int M = codingMatrix.size();
    const int N = K + M;
    vector<vector<vector<vector<int>>>> allModeDecodeMatrix(N);
    for(int i = 0; i < N; i++) {
        allModeDecodeMatrix[i] = generateAllOptDecodeBitMatrix(codingMatrix, W, i);
    }
    return allModeDecodeMatrix;
}



/**
 * @brief 将 S 个条带、每个条带 (K+M) 个块，随机分配到 N 个节点
 *
 * @param S: 条带数量
 * @param K: 数据块数 per stripe
 * @param M: 校验块数 per stripe
 * @param N: 节点数量 (必须 N >= 1)
 * @param seed: 随机种子（可选，用于可复现结果）
 * @return std::vector<std::vector<int>>: assignment[s][b] = node_id
 */
std::vector<std::vector<int>> assignStripesToNodes(
    int S, int K, int M, int N, 
    unsigned int seed = std::random_device{}()
) {
    assert(S > 0 && K > 0 && M >= 0 && N > 0);
    const int blocksPerStripe = K + M;

    // 初始化随机数生成器
    std::mt19937 rng(seed);

    // 结果：S x (K+M)
    std::vector<std::vector<int>> assignment(S, std::vector<int>(blocksPerStripe));

    // 对每个条带独立分配
    for (int s = 0; s < S; ++s) {
        // 方法 1：简单随机（允许节点负载不均）
        // for (int b = 0; b < blocksPerStripe; ++b) {
        //     assignment[s][b] = rng() % N;
        // }

        // 方法 2：更均衡的随机（推荐）—— 对每个条带，打乱节点顺序后循环分配
        std::vector<int> nodes(N);
        std::iota(nodes.begin(), nodes.end(), 0); // [0, 1, ..., N-1]
        std::shuffle(nodes.begin(), nodes.end(), rng);

        for (int b = 0; b < blocksPerStripe; ++b) {
            assignment[s][b] = nodes[b % N];
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
 * @return std::vector<std::vector<std::pair<int, int>>: nodeBlocks[i]表示节点i存储的所有块数集合，是一个数组
 *          数组中的元素是个<stripe_id, block_id>元素对
 */
std::vector<std::vector<std::pair<int, int>>> getNodeToBlocks(
    const std::vector<std::vector<int>>& assignment,
    int blocksPerStripe, int N
) {
    std::vector<std::vector<std::pair<int, int>>> nodeBlocks(N);
    for (int s = 0; s < assignment.size(); ++s) {
        for (int b = 0; b < blocksPerStripe; ++b) {
            int node = assignment[s][b];
            nodeBlocks[node].emplace_back(s, b); // (stripe_id, block_id)
        }
    }
    return nodeBlocks;
}


/**
 * @brief 根据条带的分布情况，以及节点的故障情况，计算每个节点参与修复时的负载
 *
 * @param assigment: 条带分布情况
 * @param codingMatrix: M x W 的编码矩阵
 * @param W: 有限域 W
 * @param N: 节点数量 (必须 N >= 1)
 * @param seed: 随机种子（可选，用于可复现结果）
 * @return std::vector<std::vector<int>>: assignment[s][b] = node_id
 */

vector<int> getNodeReload(vector<vector<int>> assigment, vector<vector<int>> codingMatrix, int W, int N, int failedNodeId) {
    const int K = codingMatrix[0].size();
    const int M = codingMatrix.size();
    auto nodeBlocks = getNodeToBlocks(assigment, K + M, N);
    vector<int> nodeReload(N, 0);
    // 故障节点上的条带分布
    auto failedNodeStripes = nodeBlocks[failedNodeId];
    auto decodeMatrixes = generateOptDecodeBitMatrixWithAllMode(codingMatrix, W, 0);
    cout << "failedNodeStripes.size(): " <<  failedNodeStripes.size() << endl;
    for(auto failedNodeStripe: failedNodeStripes) {
        int stripeId = failedNodeStripe.first;
        int failedBlockId = failedNodeStripe.second;
        // printf("recover strip: %d, block: %d  ", stripeId, failedBlockId);
        auto decodeMatrix = decodeMatrixes[failedBlockId];
        auto decodeRanks = computeBinaryMatrixRank(decodeMatrix, W);
        auto stripeDistribution = assigment[stripeId];
        for(int i = 0; i < stripeDistribution.size(); i++) {
            nodeReload[stripeDistribution[i]] += decodeRanks[i];
        }
        // cout << "nodeReload: " << nodeReload << endl;
    }
    return nodeReload;
}

vector<vector<int>> cauchy_original_coding_matrix_vector(int K, int M, int W) {
    int* cauchy_mat = cauchy_original_coding_matrix(K, M, W);
    std::vector<std::vector<int>> codingMatrix(M, std::vector<int>(K));
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < K; ++j)
            codingMatrix[i][j] = cauchy_mat[i * K + j];

    free(cauchy_mat); // 注意：cauchy_* 返回 malloc 内存
    return codingMatrix;
}

double computeLoadBalance(const std::vector<int>& loads) {
    if (loads.empty()) return 0.0;

    double sum = std::accumulate(loads.begin(), loads.end(), 0.0);
    double mean = sum / (loads.size() - 1);
    double maxLoad = *std::max_element(loads.begin(), loads.end());

    if (mean == 0) return 0.0;
    return maxLoad / mean;
}


vector<double> reducePacketsTest(int K, int M, int W, int failedBlock) {
    // 生成 Cauchy 编码矩阵
    std::vector<std::vector<int>> codingMatrix = cauchy_original_coding_matrix_vector(K, M, W);
    auto bigMatrix = generateAllDecodingMatrix(K, M, W, codingMatrix, failedBlock);
    auto bitMatrix = jerasureStyleBitMatrix(bigMatrix, W);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix1 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto ranks1 = computeBinaryMatrixRank(decodeBitMatrix1, W);
    auto time1 = duration1.count() / 1e6;
    auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
    cout << "***********K,M,W,FailedBlock(Only One)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks1 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum1 << endl;
    std::cout << "duration: " << time1 << " ms\n";

    auto start2 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix2 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock, 0);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
    auto ranks2 = computeBinaryMatrixRank(decodeBitMatrix2, W);
    auto time2 = duration2.count() / 1e6;
    auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
    cout << "***********K,M,W,FailedBlock(ALL)***********" << K << "," << M << "," << W << "," << failedBlock << endl; 
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks2 << endl;
    cout << "origin packets: " << K * W << endl;
    cout << "need packets: " << rankSum2 << endl;
    std::cout << "duration: " << time2 << " ms\n";

    auto start3 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix3 = generateOptDecodeBitMatrix(bitMatrix, W, failedBlock, 100, 42);
    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 = std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3);
    auto ranks3 = computeBinaryMatrixRank(decodeBitMatrix3, W);
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
template<class T>
void writeToCsv(string outputFileName, vector<vector<T>> data) {
    std::ofstream file(outputFileName);
    if (!file.is_open()) {
        std::cerr << "cannot create file!\n";
        return;
    }

    for (const auto& row : data) {
        for (size_t i = 0; i < row.size(); ++i) {
            file << row[i];
            if (i < row.size() - 1) file << ",";
        }
        file << "\n";
    }

    file.close();
    std::cout << "data saved to " + outputFileName << endl;
}

template<class T>
int getSum(vector<T> nums) {
    return accumulate(nums.begin(), nums.end(), 0);
}

void loadBalanceTest(int K, int M, int W, int N, int S, int failedNodeId) {
    auto assignment = assignStripesToNodes(S, K, M, N, 45); // 固定种子便于调试
    cout << "stripe assigment:" << endl << assignment << endl;
    auto matrix = cauchy_original_coding_matrix_vector(K, M, W);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto nodeReload = getNodeReload(assignment, matrix, W, N, failedNodeId);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto time1 = duration1.count() / 1e6;

    cout << "nodeReload: " << nodeReload << endl;
    cout << "sum: " << getSum(nodeReload) << endl;
    cout << "loadBanlance: " << computeLoadBalance(nodeReload) << endl;
    std::cout << "duration: " << time1 << " ms\n";
}

int comb(int n, int k) {
    if (k < 0 || k > n) return 0;
    if (k == 0 || k == n) return 1;
    k = min(k, n - k); // 利用对称性减少计算
    long long res = 1;
    for (long long i = 1; i <= k; ++i) {
        res = res * (n - k + i) / i; // 先乘后除，保证整除
    }
    return res;
}

// ======================
// 主函数
// ======================

int main() {
    int K = 6, M = 4, W = 8, failedBlock = 0, N = 20, S = 100;
    loadBalanceTest(K, M, W, N, S, 0);
    // std::vector<std::vector<int>> codingMatrix = cauchy_original_coding_matrix_vector(K, M, W);
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
    return 0;
}