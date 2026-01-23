#pragma once

#include "scoped_timer.hpp"
#include "ylt/easylog.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <source_location>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>

#define my_assert(condition)                                                   \
    exit_when((condition), std::source_location::current())
using namespace std;

namespace ECProject {

template <class T> ostream &operator<<(ostream &os, std::vector<T> &vc) {
    for (auto c : vc) {
        cout << c << " ";
    }
    return os;
}

template <class T>
ostream &operator<<(ostream &os, std::vector<std::vector<T>> &vc) {
    for (auto row : vc) {
        for (auto r : row) {
            cout << r << " ";
        }
        cout << endl;
    }
    return os;
}

std::vector<std::vector<int>>
bitMatrixToIntMatrix(const std::vector<std::vector<int>> &bitMatrix, int W);

std::vector<std::vector<int>>
intMatrixToBitMatrix(const std::vector<std::vector<int>> &intMatrix, int W);

// 高斯消元求 0-1 矩阵的秩
int computeBinaryMatrixRank(vector<vector<int>> bitMatrix);

int computeBinaryNonZeroCol(vector<vector<int>> bitMatrix);

vector<unsigned int>
generateUniqueRandom(int N, int K, unsigned int seed = std::random_device{}());

void writeToCsv(string outputFileName, vector<vector<double>> data);

template <class T> int getSum(const vector<T> &nums) {
    return accumulate(nums.begin(), nums.end(), 0);
}

int comb(int n, int k);
vector<unsigned int> generateAllRangeN(int N);

void printMatrix(const vector<vector<int>> &matrix, int W);

int bytes_to_int(std::vector<unsigned char> &bytes);

std::vector<unsigned char> int_to_bytes(int integer);

double bytes_to_double(std::vector<unsigned char> &bytes);

std::vector<unsigned char> double_to_bytes(double doubler);

void exit_when(bool condition, const std::source_location &location);

std::string append_timestamp_to_filename(const std::string& base_path);

template <typename T> std::string vecToString(const std::vector<T> &vec) {
    if (vec.empty())
        return "";
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0)
            oss << ' ';
        oss << vec[i];
    }
    return oss.str();
}

template <typename K, typename V>
std::string mapToString(const std::unordered_map<K, V> &mp) {
    if (mp.empty())
        return "";
    std::ostringstream oss;
    bool first = true;
    for (const auto &[k, v] : mp) {
        if (!first)
            oss << ' ';
        oss << '(' << k << ',' << v << ')';
        first = false;
    }
    return oss.str();
}

// 01 矩阵工具函数（声明）
std::string matrix_to_01_string(const std::vector<std::vector<int>> &mat);
std::vector<std::vector<int>> string_to_matrix(std::string_view s, size_t rows,
                                               size_t cols);

uint64_t indices_to_bitmask(const std::vector<int> &indices);
std::string generate_random_string(size_t length);

// 内存对齐分配（避免 SIGBUS）
inline void *aligned_malloc(size_t size, size_t alignment = 32) {
    void *ptr = nullptr;
#ifdef _WIN32
    ptr = _aligned_malloc(size, alignment);
#else
    posix_memalign(&ptr, alignment, size);
#endif
    return ptr;
}

inline void aligned_free(void *ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

template <typename T>
bool writeVectorToCSV(const std::vector<T>& vec, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    for (size_t i = 0; i < vec.size(); ++i) {
        file << vec[i];
        if (i != vec.size() - 1) file << '\n';  // 最后一行不加换行（可选）
    }
    // 或统一加换行（更常见）：
    // for (const auto& x : vec) file << x << '\n';

    return file.good();
}

std::string to_hex_string2(const char *data, size_t len);

} // namespace ECProject
