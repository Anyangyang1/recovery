#include "jerasure_wrapper.h"
#include "loadbalance.h"
#include "metadata.h"
#include "sggh.h"
#include "utils.h"
#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
// ISA-L
#include <isa-l.h>
using namespace std;
using namespace ECProject;

void test_SGGR_force_time() {
    // int param[][3] {
    //     {6, 3, 4}, {8, 3, 4}, {6, 3, 5}, {8, 3, 5}, {6, 3, 8}, {8, 3, 8},
    //         {10, 4, 8}, {12, 4, 8}
    // };
    int param[][3]{{6,3,4},{8, 3, 4}, {6, 3, 5}, {8, 3, 5}, {6, 3, 8}};
    for (auto p : param) {
        SimilarityGreedy sg = SimilarityGreedy(p[0], p[1], p[2]);
        auto matrix = sg.generateOptDecodeBitMatrixBruteForce(0);
        cout << matrix << endl;
       
        auto ranks = SimilarityGreedy::computeBinaryMatrixRank(matrix, p[2]);
        cout << "ranks: " << ranks << endl;
        cout << getSum(ranks) << endl;
    }
}

void test2() {
    const int k = 4, m = 3, w = 8;
    SimilarityGreedy sg = SimilarityGreedy(k, m, w);
    auto matrixs = sg.generateOptDecodeBitMatrixWithAllMode(0);
    for (auto matrix : matrixs) {
        auto ranks = SimilarityGreedy::computeBinaryMatrixRank(matrix, w);
        cout << "ranks: " << ranks << endl;
        cout << getSum(ranks) << endl;
    }
}

std::vector<std::vector<int>> element_to_bitmatrix(int elt, int w) {
    std::vector<std::vector<int>> bitmatrix(w, std::vector<int>(w, 0));

    for (int x = 0; x < w; ++x) {
        for (int l = 0; l < w; ++l) {
            bitmatrix[x][l] = (elt >> l) & 1;
        }
        elt = galois_single_multiply(elt, 2, w);
    }

    return bitmatrix;
}

// 测试2^w的位矩阵是否都可逆
bool test_reverse(int w) {
    int num = (1 << w);
    // cout << "w: " << w << " ,num: " << num << "*************" <<  endl;
    for (int i = 1; i < num; ++i) {
        auto bitmatrix = element_to_bitmatrix(i, w);
        // cout << i << endl;
        // cout << bitmatrix << endl;
        if (computeBinaryMatrixRank(bitmatrix) != w) {
            return false;
        }
    }
    return true;
}

GF2BasisResult
compute_basis_gf2_indices(const std::vector<std::vector<int>> &A) {
    int w = static_cast<int>(A.size());
    if (w == 0 || A[0].size() != static_cast<size_t>(w)) {
        return {};
    }

    // Step 1: 高斯消元求 RREF 和主元映射
    auto R = A;
    std::vector<int> pivot_row_for_col(
        w, -1); // col c → 哪一行是其主元行（在 R 中的行号）
    int rank = 0;

    for (int c = 0; c < w && rank < w; ++c) {
        // 找主元行
        int pivot = -1;
        for (int r = rank; r < w; ++r) {
            if (R[r][c] == 1) {
                pivot = r;
                break;
            }
        }
        if (pivot == -1)
            continue;
        std::swap(R[rank], R[pivot]);
        pivot_row_for_col[c] = rank;

        // 消去其他所有行的第 c 列
        for (int r = 0; r < w; ++r) {
            if (r != rank && R[r][c] == 1) {
                for (int j = 0; j < w; ++j) {
                    R[r][j] ^= R[rank][j];
                }
            }
        }
        ++rank;
    }

    // 提取基（前 rank 行）
    std::vector<std::vector<int>> basis(R.begin(), R.begin() + rank);

    // Step 2: 对每个原始行 A[i]，求其由哪些基向量异或而成
    std::vector<std::vector<int>> reps(w);

    for (int i = 0; i < w; ++i) {
        auto v = A[i]; // 当前待表出行

        // 遍历所有主元列（按列递增顺序 = 基向量顺序）
        for (int c = 0; c < w; ++c) {
            if (v[c] == 1 && pivot_row_for_col[c] != -1) {
                int basis_idx = pivot_row_for_col[c]; // 该主元对应的基索引（0
                                                      // ~ rank-1）
                reps[i].push_back(basis_idx);

                // v ^= basis[basis_idx]
                const auto &b = basis[basis_idx];
                for (int j = 0; j < w; ++j) {
                    v[j] ^= b[j];
                }
            }
        }
        // 理论上 v 应全 0；可加 assert 检查
    }

    return {basis, reps};
}

GF2BasisResult compute_basis_selective(const std::vector<std::vector<int>> &A) {
    int w = static_cast<int>(A.size());
    if (w == 0)
        return {{}, {}};
    assert(A[0].size() == static_cast<size_t>(w));

    std::vector<std::vector<int>> basis;
    std::vector<std::vector<int>> reps(w);
    std::vector<int> pivot_col(w, -1); // col -> index in basis

    for (int i = 0; i < w; ++i) {
        const auto &row = A[i];

        // Check zero row
        bool is_zero = true;
        for (int x : row)
            if (x) {
                is_zero = false;
                break;
            }
        if (is_zero) {
            reps[i] = {};
            continue;
        }

        std::vector<int> v = row;
        std::vector<int> coeff;

        // Gaussian elimination: left to right
        for (int c = 0; c < w; ++c) {
            if (v[c] == 1) {
                int k = pivot_col[c];
                if (k != -1) {
                    // Eliminate using existing basis[k]
                    const auto &b = basis[k];
                    for (int j = 0; j < w; ++j)
                        v[j] ^= b[j];
                    coeff.push_back(k);
                } else {
                    // New pivot: add original row to basis
                    pivot_col[c] = static_cast<int>(basis.size());
                    basis.push_back(row); // store original row
                    coeff.push_back(static_cast<int>(basis.size()) - 1);
                    break; // stop after adding to basis
                }
            }
        }
        reps[i] = coeff;
    }

    return {basis, reps};
}
std::vector<std::vector<int>>
get_submatrix(const std::vector<std::vector<int>> &decode_matrix, int i) {
    if (decode_matrix.empty())
        return {};

    size_t w = decode_matrix.size();
    if (w == 0)
        return {};

    // 检查列对齐
    size_t total_cols = decode_matrix[0].size();
    if (total_cols % w != 0 || i < 0)
        return {};

    size_t block_idx = static_cast<size_t>(i);
    size_t blocks = total_cols / w;
    if (block_idx >= blocks)
        return {};

    // 计算第 i 块的列范围
    size_t start_col = block_idx * w;

    // 先检查是否全零（提前退出优化）
    bool all_zero = true;
    for (size_t r = 0; r < w && all_zero; ++r) {
        for (size_t c = 0; c < w && all_zero; ++c) {
            if (decode_matrix[r][start_col + c] != 0) {
                all_zero = false;
            }
        }
    }

    if (all_zero) {
        return {}; // 返回空矩阵
    }

    // 否则拷贝子矩阵
    std::vector<std::vector<int>> result(w, std::vector<int>(w));
    for (size_t r = 0; r < w; ++r) {
        std::copy_n(decode_matrix[r].begin() + start_col, w, result[r].begin());
    }
    return result;
}

void generate_opt_decode_matrix_test(const int k, const int m, const int w) {
    cout << "k, m, w " << k << "," << m << "," << w << "**********************"
         << endl;
    SimilarityGreedy sg = SimilarityGreedy(k, m, w);
    auto matrix = sg.generateOptDecodeBitMatrix(0);
    // cout << matrix << endl;

    auto ranks = SimilarityGreedy::computeBinaryMatrixRank(matrix, w);
    cout << "net every packets: " << ranks << endl;
    cout << "net sum packets: " << getSum(ranks) << endl;

    auto non_zeros = SimilarityGreedy::computeBinaryNonZeroCol(matrix, w);
    cout << "disk every packets: " << non_zeros << endl;
    cout << "disk sum packets: " << getSum(non_zeros) << endl;
}

void xor_gen_test(const int k, const size_t block_size) {
    std::vector<char *> data(k);
    char *parity_isal = (char *)aligned_malloc(block_size);
    for (int i = 0; i < k; ++i) {
        data[i] = (char *)aligned_malloc(block_size);

        std::string str = generate_random_string(block_size);
        std::memcpy(data[i], str.data(), block_size);
    }
    void *srcs[k + 1];
    for (int i = 0; i < k; i++) {
        srcs[i] = data[i];
    }
    void *dests = parity_isal;
    srcs[k] = dests;
    xor_gen(k + 1, block_size, srcs);
}

int64_t test_xor_gen_split_or_all(const size_t block_size, const int data_num,
                                  const int group_size) {
    assert(data_num % group_size == 0);
    int64_t us = 0;
    std::vector<char *> data(data_num);
    std::vector<char *> parity(group_size);
    int loop_size = data_num / group_size;
    char *parity_isal = (char *)aligned_malloc(block_size);
    for (int i = 0; i < data_num; ++i) {
        data[i] = (char *)aligned_malloc(block_size);
        std::string str = generate_random_string(block_size);
        std::memcpy(data[i], str.data(), block_size);
    }
    for (int i = 0; i < group_size; i++) {
        parity[i] = (char *)aligned_malloc(block_size);
        void *loops[loop_size + 1];
        for (int j = 0; j < loop_size; j++) {
            loops[j] = data[i * loop_size + j];
        }
        loops[loop_size] = parity[i];
        auto start = std::chrono::high_resolution_clock::now();
        xor_gen(loop_size + 1, block_size, loops);
        auto end = std::chrono::high_resolution_clock::now();

        us += std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                  .count();
    }

    if (group_size > 1) {
        void *srcs[group_size + 1];
        for (int i = 0; i < group_size; i++) {
            srcs[i] = parity[i];
        }
        srcs[group_size] = parity_isal;
        auto start = std::chrono::high_resolution_clock::now();
        xor_gen(group_size + 1, block_size, srcs);
        auto end = std::chrono::high_resolution_clock::now();

        us += std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                  .count();
    }
    return us;
}

int64_t xor_gen_and_cpy(const int k, const size_t block_size) {
    std::vector<char *> data(k);
    char *parity_isal = (char *)aligned_malloc(block_size);
    for (int i = 0; i < k; ++i) {
        data[i] = (char *)aligned_malloc(block_size);

        std::string str = generate_random_string(block_size);
        std::memcpy(data[i], str.data(), block_size);
    }
    void *srcs[k + 1];
    for (int i = 0; i < k; i++) {
        srcs[i] = data[i];
    }
    void *dests = parity_isal;
    srcs[k] = dests;

    auto start = std::chrono::high_resolution_clock::now();
    xor_gen(k + 1, block_size, srcs);
    auto end = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                  .count();
    return us;
}

void isa_jerasure_test(const int k, const int m, const size_t block_size) {
    // === 1. 分配内存 ===
    std::vector<uint8_t *> data(k);
    uint8_t *parity_isal = (uint8_t *)aligned_malloc(block_size);
    uint8_t *parity_jerasure = (uint8_t *)aligned_malloc(block_size);

    for (int i = 0; i < k; ++i) {
        data[i] = (uint8_t *)aligned_malloc(block_size);
        // 填充：data[0]=1, data[1]=2, data[2]=3
        std::memset(data[i], i + 1, block_size);
    }

    // === 2. ISA-L XOR ===
    {
        void *srcs[k + 1];
        for (int i = 0; i < k; i++) {
            srcs[i] = data[i];
        }
        srcs[k] = parity_isal;

        auto start = std::chrono::high_resolution_clock::now();
        xor_gen(k + 1, block_size, srcs); //  4 个参数
        auto end = std::chrono::high_resolution_clock::now();

        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();
        double mbps = (k * block_size) / 1024.0 / 1024.0 / (us / 1e6);
        std::cout << "[ISA-L] XOR time: " << us << " us, bandwidth: " << mbps
                  << " MB/s\n";

        // 验证：1 ^ 2 ^ 3 = 0
        uint8_t expected = 0; // = 0
        for (int i = 1; i <= k; i++) {
            expected ^= i;
        }
        if (parity_isal[0] == expected) {
            std::cout << "[ISA-L]  Verification passed (first byte = "
                      << (int)parity_isal[0] << ")\n";
        } else {
            std::cout << "[ISA-L]  Verification failed\n";
        }
    }

    // === 3. Jerasure XOR (RS(3,1) with identity matrix) ===
    {
        // Jerasure RS 编码需矩阵：对于 XOR，可用 [1, 1, 1] 系数
        int *matrix = (int *)malloc(k * m * sizeof(int)); // GF(2^8)
        // 设 m=1 行为 [1, 1, 1] → P = D0 + D1 + D2 = D0 ⊕ D1 ⊕ D2

        for (int i = 0; i < k * m; ++i) {
            matrix[i] = 1; // 整数 1，不是字节 0x01
        }

        auto start = std::chrono::high_resolution_clock::now();
        jerasure_matrix_encode(k, m, 8, matrix, (char **)data.data(),
                               (char **)&parity_jerasure, block_size);
        auto end = std::chrono::high_resolution_clock::now();

        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();
        double mbps = (k * block_size) / 1024.0 / 1024.0 / (us / 1e6);
        std::cout << "[Jerasure] XOR time: " << us << " us, bandwidth: " << mbps
                  << " MB/s\n";

        uint8_t expected = 0; // = 0
        for (int i = 1; i <= k; i++) {
            expected ^= i;
        }
        if (parity_jerasure[0] == expected) {
            std::cout << "[Jerasure]  Verification passed (first byte = "
                      << (int)parity_jerasure[0] << ")\n";
        } else {
            std::cout << "[Jerasure]  Verification failed\n";
        }

        free(matrix);
    }

    // === 4. 清理 ===
    for (auto p : data)
        aligned_free(p);
    aligned_free(parity_isal);
    aligned_free(parity_jerasure);
}

void reducePacketsTest() {
    vector<int> KK{12};
    vector<int> MM{3, 4};
    vector<int> NUM{1};
    const int w = 8, failedBlock = 0;
    for (int num : NUM) {
        for (int m : MM) {
            for (int k : KK) {
                SimilarityGreedy sg = SimilarityGreedy(k, m, w);
                auto start1 = std::chrono::high_resolution_clock::now();
                // auto decodeBitMatrix1 =
                // sg.generateOptDecodeBitMatrix(failedBlock, num, 0);
                auto decodeBitMatrix1 =
                    sg.generateOptDecodeBitMatrix(failedBlock, 0);

                auto end1 = std::chrono::high_resolution_clock::now();
                auto duration1 =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end1 - start1);
                auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(
                    decodeBitMatrix1, w);
                auto time1 = duration1.count() / 1e6;
                auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
                // cout << decodeBitMatrix << endl;
                cout << "ranks: " << ranks1 << endl;
                cout << "origin packets: " << k * w << endl;
                cout << "need packets: " << rankSum1 << endl;
                std::cout << "duration: " << time1 << " ms\n";
                cout << "^^^^^^^^^^^^^^k,m,w,FailedBlock,num^^^^^^^^^^^^^^" << k
                     << "," << m << "," << w << "," << num << endl;
            }
        }
    }
}

vector<double> reducePacketsTest(int k, int m, int w, int failedBlock) {
    // 生成 Cauchy 编码矩阵
    SimilarityGreedy sg = SimilarityGreedy(k, m, w);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix1 = sg.generateOptDecodeBitMatrix(failedBlock);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto ranks1 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix1, w);
    auto time1 = duration1.count() / 1e6;
    auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
    cout << "***********k,m,w,FailedBlock(Only One)***********" << k << "," << m
         << "," << w << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks1 << endl;
    cout << "origin packets: " << k * w << endl;
    cout << "need packets: " << rankSum1 << endl;
    std::cout << "duration: " << time1 << " ms\n";

    auto start2 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix2 = sg.generateOptDecodeBitMatrix(failedBlock, 0);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
    auto ranks2 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix2, w);
    auto time2 = duration2.count() / 1e6;
    auto rankSum2 = accumulate(ranks2.begin(), ranks2.end(), 0);
    cout << "***********k,m,w,FailedBlock(ALL)***********" << k << "," << m
         << "," << w << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks2 << endl;
    cout << "origin packets: " << k * w << endl;
    cout << "need packets: " << rankSum2 << endl;
    std::cout << "duration: " << time2 << " ms\n";

    auto start3 = std::chrono::high_resolution_clock::now();
    auto decodeBitMatrix3 = sg.generateOptDecodeBitMatrix(failedBlock, 100, 42);
    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3);
    auto ranks3 =
        SimilarityGreedy::computeBinaryMatrixRank(decodeBitMatrix3, w);
    auto time3 = duration3.count() / 1e6;
    auto rankSum3 = accumulate(ranks3.begin(), ranks3.end(), 0);
    cout << "***********k,m,w,FailedBlock(Random)***********" << k << "," << m
         << "," << w << "," << failedBlock << endl;
    // cout << decodeBitMatrix << endl;
    cout << "ranks: " << ranks3 << endl;
    cout << "origin packets: " << k * w << endl;
    cout << "need packets: " << rankSum3 << endl;
    std::cout << "duration: " << time3 << " ms\n";

    vector<double> data{
        (double)k * w, (double)rankSum1, time1, (double)rankSum2,
        time2,         (double)rankSum3, time3};
    return data;
}

void loadBalanceTest(int k, int m, int w, int N, int S, int failedNodeId) {
    MultiStripeRecovery msr = MultiStripeRecovery(k, m, w, N, S);

    auto start1 = std::chrono::high_resolution_clock::now();
    auto nodeReload = msr.getNodeLoad(failedNodeId);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    auto time1 = duration1.count() / 1e6;

    cout << "nodeReload: " << nodeReload << endl;
    cout << "sum: " << getSum(nodeReload) << endl;
    cout << "loadBanlance: "
         << MultiStripeRecovery::computeLoadBalance(nodeReload) << endl;
    std::cout << "duration: " << time1 << " ms\n";
}

void xorTimeTest(unsigned int value_size, bool use_jerasure) {

    // auto aligned_alloc_char = [](size_t n) {
    //     void *p = aligned_alloc(32, n); // C11，POSIX 保证
    //     if (!p)
    //         throw std::bad_alloc();
    //     return std::unique_ptr<char[], decltype(&free)>(static_cast<char
    //     *>(p),
    //                                                     free);
    // };

    // auto value1 = aligned_alloc_char(value_size);
    // std::fill_n(value1.get(), value_size, 'a');

    // auto value2 = aligned_alloc_char(value_size);
    // std::fill_n(value2.get(), value_size, 'b');

    // auto value3 = aligned_alloc_char(value_size);
    std::vector<char> value1(value_size, 'a');
    std::vector<char> value2(value_size, 'b');
    std::vector<char> value3(value_size, 0);
    // printf("v1 align: %ld, v2 align: %ld, v3 align: %ld\n",
    // (uintptr_t)value1.data() % 32,
    // (uintptr_t)value2.data() % 32,
    // (uintptr_t)value3.data() % 32);
    {

        SCOPED_TIMER("test");
        if (use_jerasure) {
            galois_region_xor(value1.data(), value2.data(), value3.data(),
                              value_size);
        } else {
            for (size_t i = 0; i < value_size; i++) {
                value3[i] = value1[i] ^ value2[i];
            }
        }
    }
}

int add(int a, int b) {
    cout << a + b << endl;
    return a + b;
}

void test1() {
    vector<int> KK{24, 48, 72, 96};
    vector<int> MM{3, 4};
    vector<int> NUM{1};
    const int w = 8, failedBlock = 0;
    for (int num : NUM) {
        for (int m : MM) {
            for (int k : KK) {
                SimilarityGreedy sg = SimilarityGreedy(k, m, w);
                auto start1 = std::chrono::high_resolution_clock::now();
                auto decodeBitMatrix1 =
                    sg.generateOptDecodeBitMatrix(failedBlock, num, 0);
                // auto decodeBitMatrix1 =
                // sg.generateOptDecodeBitMatrix(failedBlock, 0);

                auto end1 = std::chrono::high_resolution_clock::now();
                auto duration1 =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end1 - start1);
                auto ranks1 = SimilarityGreedy::computeBinaryMatrixRank(
                    decodeBitMatrix1, w);
                auto time1 = duration1.count() / 1e6;
                auto rankSum1 = accumulate(ranks1.begin(), ranks1.end(), 0);
                cout << "***********k,m,w,NUM,FailedBlock(Only One)***********"
                     << k << "," << m << "," << w << "," << num << endl;
                // cout << decodeBitMatrix << endl;
                cout << "ranks: " << ranks1 << endl;
                cout << "origin packets: " << k * w << endl;
                cout << "need packets: " << rankSum1 << endl;
                std::cout << "duration: " << time1 << " ms\n";
            }
        }
    }
}