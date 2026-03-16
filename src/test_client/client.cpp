#include "scoped_timer.hpp"
#include "utils.h"
#include <iostream>
#include <vector>
#include "metadata.h"
#include "sggh.h"
#include "loadbalance.h"
using namespace std;
using namespace ECProject;
void isa_jerasure_test(const int K, const int M, const size_t BLOCK_SIZE);
void generate_opt_decode_matrix_test(const int K, const int M, const int W);
void xor_gen_test(const int k, const size_t block_size);
int64_t xor_gen_and_cpy(const int k, const size_t block_size);
bool access_data(const std::string &key, char *value_buf, size_t value_size);
bool access_data(const std::string &key, char *value_buf,
                 const vector<int> &idxs);
bool store_data(const std::string &key, const char *value, size_t value_size);
bool test_reverse(int w);
int64_t test_xor_gen_split_or_all(const size_t block_size, const int data_num,
                                  const int group_size);
void test_SGGR_force_time();
int main(int argc, char* argv[]) {
    int k = atoi(argv[1]);
    int m = atoi(argv[2]);
    int w = atoi(argv[3]);
    int S = atoi(argv[4]);
    int N = atoi(argv[5]);
    int F = atoi(argv[6]);
    MultiStripeRecovery mr = MultiStripeRecovery(k, m, w, N, S);
    auto nodeLoad = mr.getNodeLoad(F);
    cout << nodeLoad << endl;
    cout << "loadbalance: " << MultiStripeRecovery::computeLoadBalance(nodeLoad) << endl;
    cout << "sum packet: " << ECProject::getSum(nodeLoad) << endl;

    auto nodeLoadPlan = mr.getBalancedRecoveryPlan(F);
    auto nodeLoadBalance = mr.applyBalancedRecovery(F, nodeLoadPlan);
    cout << nodeLoadBalance << endl;
    cout << "loadbalance2: " << MultiStripeRecovery::computeLoadBalance(nodeLoadBalance) << endl;
    cout << "sum packet2: " << ECProject::getSum(nodeLoadBalance) << endl;

    // int k = atoi(argv[1]);
    // int m = atoi(argv[2]);
    // int w = atoi(argv[3]);
    // SimilarityGreedy sg = SimilarityGreedy(k, m, w);
    // auto bestMatrix = sg.generateAllOptDecodeBitMatrix(0);
    // for(auto &matrix: bestMatrix) {
    //     cout << matrix << endl;
    //     auto ranks = SimilarityGreedy::computeBinaryMatrixRank(matrix, w);
    //     cout << ranks << endl;
    // }
    return 0;
}