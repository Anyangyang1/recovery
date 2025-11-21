#pragma once

#include <iostream>
#include <vector>
#include <cassert>
#include <random>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <memory>
#include <cstring>

extern "C" {
#include "../../Jerasure-1.2A/jerasure.h"
#include "../../Jerasure-1.2A/cauchy.h"
}
using namespace std;

namespace ECProject {

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

    std::vector<std::vector<int>> bitMatrixToIntMatrix(
        const std::vector<std::vector<int>>& bitMatrix, int W);

    std::vector<std::vector<int>> intMatrixToBitMatrix(
        const std::vector<std::vector<int>>& intMatrix, int W);

    // 高斯消元求 0-1 矩阵的秩
    int computeBinaryMatrixRank(vector<vector<int>> bitMatrix);

    
    vector<int> generateUniqueRandom(
        int N, int K, unsigned int seed = std::random_device{}());

    void writeToCsv(string outputFileName, vector<vector<double>> data);

    template<class T>
    int getSum(const vector<T>& nums) {
        return accumulate(nums.begin(), nums.end(), 0);
    }

    int comb(int n, int k);

    vector<vector<int>> matrix2Bitmatrix(const vector<vector<int>>& mat, int W);

    vector<int> generateAllRangeN(int N);

    vector<vector<int>> cauchy_original_coding_matrix_vector(int K, int M, int W);

    void printMatrix(const vector<vector<int>>& matrix, int W);


    int bytes_to_int(std::vector<unsigned char> &bytes);

    std::vector<unsigned char> int_to_bytes(int integer);

    double bytes_to_double(std::vector<unsigned char> &bytes);

    std::vector<unsigned char> double_to_bytes(double doubler);

} // namespace name
