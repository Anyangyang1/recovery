#pragma once
#include "utils.h"
#include "erasure_code.h"
#include <random>
#include <set>
#include <vector>
using namespace std;
namespace ECProject {
class SimilarityGreedy {
  public:
    SimilarityGreedy(int K, int M, int W) : K(K), M(M), W(W), N(K + M) {
        codingMatrix = ErasureCode::cauchy_original_coding_matrix_vector(K, M, W);
    }
    ~SimilarityGreedy() {}

    /**
     * @brief 基于相似度，生成最优解码矩阵
     * @param failedBlock: 故障块号（0~K+M-1）
     * @param mode:
     * 修复模式，mode=0只选取第0行作为起始行，mode=-1(默认)选取所有的行作为起始行,
     * mode=num(num > 0)随机选取num行
     * @param seed 随机种子，设置mode>0才有意义
     * @return: 最优解码矩阵，维度 W x ((K+M)*W)
     */
    vector<vector<int>>
    generateOptDecodeBitMatrix(int failedBlock, int mode = -1,
                               unsigned int seed = std::random_device{}());

    /**
     * @brief 基于相似度，生成所有的最优解码矩阵
     * @param failedBlock: 故障块号（0~K+M-1）
     * @return: 包含所有的最优解码矩阵，每个矩阵的维度 W x ((K+M)*W)
     */
    vector<vector<vector<int>>> generateAllOptDecodeBitMatrix(int failedBlock);

    /**
     * @brief 基于相似度，生成所有故障模式的最优解码矩阵
     * @param mode:
     * 修复模式，mode=0只选取第0行作为起始行，mode=-1(默认)选取所有的行作为起始行,
     * mode=num(num > 0)随机选取num行
     * @param seed 随机种子，设置mode>0才有意义
     * @return:
     * 所有故障模式的最优解码矩阵，第i个二维矩阵表示第i块故障时（包含数据块和校验块）的最优解码矩阵，每个矩阵维度
     * W x ((K+M)*W)
     */
    vector<vector<vector<int>>> generateOptDecodeBitMatrixWithAllMode(
        int mode = -1, unsigned int seed = std::random_device{}());

    /**
     * @brief 基于相似度，生成所有故障模式的所有最优解码矩阵
     * @return: 四维矩阵matrix。matrix[i]表示第i块故障时的所有最优解码矩阵
     */
    vector<vector<vector<vector<int>>>>
    generateAllOptDecodeBitMatrixWithAllMode();

    /**
     * @brief 计算位矩阵的秩，没w为一组
     * @param bitMatrix: W行的位矩阵
     * @param W: 每W列分组，构成一个W*W的位矩阵
     * @return: 每个位矩阵的秩
     */
    static vector<int> computeBinaryMatrixRank(vector<vector<int>> &bitMmatrix,
                                               int W);
    vector<vector<vector<int>>>
    generateAllDecodingMatrices(const vector<int> &failedBlocks);

  private:
    /**
     * @brief 根据故障块号，生成全局候选解码矩阵
     * @param failedBlock: 故障块号
     * @return: 全局候选解码矩阵
     */
    vector<vector<int>> generateAllDecodingMatrix(int failedBlock);

    /**
     * @brief
     * 更新closure集合，将val以及closure中的所有元素与val异或的结果添加到closure中
     * @param closure: 表示已经选取的修复向量以及能由它线性表出的所有向量集合
     */
    void updateXorClosure(set<int> &closure, int val);

    void updateXorClosures(vector<set<int>> &closures,
                           const vector<int> &candidate);

    /**
     * @brief 计算一个候选向量相对于当前closures的相似度
     * @param closures: closures集合，closures[i]表示每个块的closure集合
     * @param candidate：候选向量
     * @return 返回相似度的值
     */
    int computeSimilarity(const vector<std::set<int>> &closures,
                          const vector<int> &candidate);

    /**
     * @brief 设置第一个选取的行，寻找最解码位矩阵
     * @param bitMatrix: 全局候选位矩阵
     * @param firstSelect： 第一个选取的行号
     * @return 最优解码维矩阵
     */
    vector<vector<int>> generateOptDecodeBitMatrixWithFirstSelect(
        const vector<vector<int>> &bitMatrix, int firstSelect);

  private:
    int K;                            // 数据块数量
    int M;                            // 校验块数量
    int W;                            // 有限域W
    int N;                            // 数据块和校验块数量之和
    vector<vector<int>> codingMatrix; // 编码矩阵
};

} // namespace ECProject
