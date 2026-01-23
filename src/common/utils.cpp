#include "utils.h"
#include <fstream>

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

std::string ECProject::append_timestamp_to_filename(const std::string& base_path) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d%H%M%S");
    // 如果需要毫秒精度，可追加： << '.' << std::setfill('0') << std::setw(3) << ms.count();

    // 分离路径和扩展名
    size_t last_dot = base_path.find_last_of('.');
    if (last_dot == std::string::npos) {
        return base_path + "_" + oss.str();
    } else {
        return base_path.substr(0, last_dot) + "_" + oss.str() + base_path.substr(last_dot);
    }
}

// 高斯消元求 0-1 矩阵的秩
int ECProject::computeBinaryMatrixRank(vector<vector<int>> matrix) {
    int rows = matrix.size();
    if (rows == 0)
        return 0;
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
        if (pivot == -1)
            continue;

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


int ECProject::computeBinaryNonZeroCol(vector<vector<int>> bitMatrix) {
    int ans = 0;
    for(size_t j = 0; j < bitMatrix[0].size(); ++j) {
        bool all_zero = true;
        for(size_t i = 0; i < bitMatrix.size(); ++i) {
            if(bitMatrix[i][j] == 1) {
                all_zero = false;
                break;
            }
        }
        if(!all_zero) {
            ans++;
        }
    }
    return ans;
}

/**
 * @brief 将简化位矩阵（每行 R x (C*W)）转换为整数矩阵（R x C）
 *
 * @param bitMatrix: R 行，每行长度为 C * W，元素为 0/1
 * @param W: 每组位数
 * @return std::vector<std::vector<int>>: R x C 的整数矩阵
 */
std::vector<std::vector<int>>
ECProject::bitMatrixToIntMatrix(const std::vector<std::vector<int>> &bitMatrix,
                                int W) {
    if (bitMatrix.empty())
        return {};

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

std::vector<std::vector<int>>
ECProject::intMatrixToBitMatrix(const std::vector<std::vector<int>> &intMatrix,
                                int W) {
    if (intMatrix.empty())
        return {};

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

vector<unsigned int> ECProject::generateUniqueRandom(int N, int K,
                                                     unsigned int seed) {
    if (K > N || K < 0) {
        // throw std::invalid_argument("K must be between 0 and N");
        K = N;
    }
    // 创建 0 到 N-1 的序列
    vector<unsigned int> nums(N);
    std::iota(nums.begin(), nums.end(), 0); // 填充 0,1,2,...,N-1

    // 随机打乱
    std::mt19937 rng(seed);
    std::shuffle(nums.begin(), nums.end(), rng);

    // 取前 K 个
    nums.resize(K);
    return nums;
}

void ECProject::writeToCsv(string outputFileName, vector<vector<double>> data) {
    std::ofstream file(outputFileName);
    if (!file.is_open()) {
        std::cerr << "cannot create file!\n";
        return;
    }

    for (const auto &row : data) {
        for (size_t i = 0; i < row.size(); ++i) {
            file << row[i];
            if (i < row.size() - 1)
                file << ",";
        }
        file << "\n";
    }

    file.close();
    std::cout << "data saved to " + outputFileName << endl;
}

int ECProject::comb(int n, int k) {
    if (k < 0 || k > n)
        return 0;
    if (k == 0 || k == n)
        return 1;
    k = min(k, n - k); // 利用对称性减少计算
    long long res = 1;
    for (long long i = 1; i <= k; ++i) {
        res = res * (n - k + i) / i; // 先乘后除，保证整除
    }
    return res;
}


vector<unsigned int> ECProject::generateAllRangeN(int N) {
    if (N <= 0)
        return {};
    vector<unsigned int> result(N);
    std::iota(result.begin(), result.end(), 0);
    return result;
}


void ECProject::printMatrix(const vector<vector<int>> &matrix, int W) {
    for (size_t i = 0; i < matrix.size(); i++) {
        for (size_t j = 0; j < matrix[0].size(); j++) {
            cout << matrix[i][j] << " ";
            if ((j + 1) % W == 0) {
                cout << " ";
            }
        }
        cout << endl;
    }
}

int ECProject::bytes_to_int(std::vector<unsigned char> &bytes) {
    int integer;
    unsigned char *p = (unsigned char *)(&integer);
    for (int i = 0; i < int(bytes.size()); i++) {
        memcpy(p + i, &bytes[i], 1);
    }
    return integer;
}

std::vector<unsigned char> ECProject::int_to_bytes(int integer) {
    std::vector<unsigned char> bytes(sizeof(int));
    unsigned char *p = (unsigned char *)(&integer);
    for (int i = 0; i < int(bytes.size()); i++) {
        memcpy(&bytes[i], p + i, 1);
    }
    return bytes;
}

double ECProject::bytes_to_double(std::vector<unsigned char> &bytes)
{
  double doubler;
  memcpy(&doubler, bytes.data(), sizeof(double));
  return doubler;
}

std::vector<unsigned char> ECProject::double_to_bytes(double doubler)
{
  std::vector<unsigned char> bytes(sizeof(double));
  memcpy(bytes.data(), &doubler, sizeof(double));
  return bytes;
}


void ECProject::exit_when(bool condition,
                          const std::source_location &location) {
    if (!condition) {
        std::cerr << "Condition failed at " << location.file_name() << ":"
                  << location.line() << " - " << location.function_name()
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
}


// ====== 新增工具函数 ======
std::string ECProject::matrix_to_01_string(const std::vector<std::vector<int>>& mat) {
    if (mat.empty()) return "";
    size_t rows = mat.size(), cols = mat[0].size();
    std::string s;
    s.reserve(rows * cols);
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            s.push_back(mat[i][j] ? '1' : '0');
        }
    }
    return s;
}

std::string ECProject::to_hex_string2(const char *data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2)
           << static_cast<int>(static_cast<unsigned char>(data[i]));
    }
    return ss.str();
}

std::string ECProject::generate_random_string(size_t length) {
    static const char charset[] = "0123456789"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(
        0, sizeof(charset) - 2); // -2: exclude trailing '\0'

    std::string str(length, 0);
    for (size_t i = 0; i < length; ++i) {
        str[i] = charset[dis(gen)];
    }
    return str;
}


uint64_t ECProject::indices_to_bitmask(const std::vector<int>& indices) {
    uint64_t mask = 0;
    for (int idx : indices) {
        // 安全检查（可选，release 可关闭）
        assert(idx >= 0 && idx < 64 && "Index out of 0~63 range");
        mask |= (1ULL << idx);
    }
    return mask;
}

std::vector<std::vector<int>> ECProject::string_to_matrix(std::string_view s, size_t rows, size_t cols) {
    if (s.size() != rows * cols) {
        throw std::runtime_error("Invalid matrix string length");
    }
    std::vector<std::vector<int>> mat(rows, std::vector<int>(cols));
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            mat[i][j] = (s[i * cols + j] == '1');
        }
    }
    return mat;
}

