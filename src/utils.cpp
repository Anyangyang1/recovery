#include <fstream>
#include "../include/utils.h"

// 高斯消元求 0-1 矩阵的秩
int ECProject::computeBinaryMatrixRank(vector<vector<int>> matrix) {
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


/**
 * @brief 将简化位矩阵（每行 R x (C*W)）转换为整数矩阵（R x C）
 *
 * @param bitMatrix: R 行，每行长度为 C * W，元素为 0/1
 * @param W: 每组位数
 * @return std::vector<std::vector<int>>: R x C 的整数矩阵
 */
std::vector<std::vector<int>> ECProject::bitMatrixToIntMatrix(
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


std::vector<std::vector<int>> ECProject::intMatrixToBitMatrix(
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


vector<int> ECProject::generateUniqueRandom(int N, int K, unsigned int seed) {
    if (K > N || K < 0) {
        // throw std::invalid_argument("K must be between 0 and N");
        K = N;
    }
    // 创建 0 到 N-1 的序列
    vector<int> nums(N);
    std::iota(nums.begin(), nums.end(), 0); // 填充 0,1,2,...,N-1

    // 随机打乱
    std::mt19937 rng(seed);
    std::shuffle(nums.begin(), nums.end(), rng);

    // 取前 K 个
    nums.resize(K);
    return nums;
}

template<class T>
void ECProject::writeToCsv(string outputFileName, vector<vector<T>> data) {
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

int ECProject::comb(int n, int k) {
    if (k < 0 || k > n) return 0;
    if (k == 0 || k == n) return 1;
    k = min(k, n - k); // 利用对称性减少计算
    long long res = 1;
    for (long long i = 1; i <= k; ++i) {
        res = res * (n - k + i) / i; // 先乘后除，保证整除
    }
    return res;
}

/**
 * @brief 使用 Jerasure 将整数矩阵转为标准位矩阵（W×W per element）
 *
 * @param mat: R x C 的整数矩阵
 * @param W: 字长
 * @return: 位矩阵，维度 (R*W) x (C*W)
 */
vector<vector<int>> ECProject::matrix2Bitmatrix(
    const vector<vector<int>>& mat, int W) {
    size_t R = mat.size();
    if (R == 0) return {};
    size_t C = mat[0].size();

    // 转为 flat int array (row-major)
    vector<int> flat(R * C);
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
    vector<vector<int>> result(outRows, vector<int>(outCols));

    for (size_t i = 0; i < outRows; ++i) {
        for (size_t j = 0; j < outCols; ++j) {
            result[i][j] = bitPtr[i * outCols + j];
        }
    }

    free(bitPtr); // Jerasure 使用 malloc
    return result;
}

vector<int> ECProject::generateAllRangeN(int N) {
    if (N <= 0) return {};
    vector<int> result(N);
    std::iota(result.begin(), result.end(), 0);
    return result;
}

vector<vector<int>> ECProject::cauchy_original_coding_matrix_vector(int K, int M, int W) {
    int* cauchy_mat = cauchy_original_coding_matrix(K, M, W);
    vector<vector<int>> codingMatrix(M, vector<int>(K));
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < K; ++j)
            codingMatrix[i][j] = cauchy_mat[i * K + j];

    free(cauchy_mat); // 注意：cauchy_* 返回 malloc 内存
    return codingMatrix;
}