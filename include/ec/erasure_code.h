#pragma once

#include "../jerasure_wrapper.h"
#include "utils.h"

namespace ECProject {
enum ECTYPE { RS, XOR };
class ErasureCode {
  public:
    int k;     /* number of data blocks */
    int m;     /* number of parity blocks */
    int w = 8; /* word size for encoding */

    ErasureCode() = default;
    ErasureCode(int k, int m) : k(k), m(m) {}
    ErasureCode(int k, int m, int w) : k(k), m(m), w(w) {}
    virtual ~ErasureCode() {}

    virtual void encode(char **data_ptrs, char **coding_ptrs,
                        int block_size) = 0;
    virtual void decode(char **data_ptrs, char **coding_ptrs, int blocksize,
                        int *erasures, int failed_num) = 0;
    virtual std::unique_ptr<ErasureCode> clone() const = 0;

    virtual void make_encoding_matrix(int *final_matrix) {}

    void print_matrix(int *matrix, int rows, int cols, std::string msg);
    void get_identity_matrix(int *matrix, int rows, int kk);
    virtual void make_full_matrix(int *matrix, int kk);
    void make_submatrix_by_rows(int cols, int *matrix, int *new_matrix,
                                std::vector<int> block_idxs);
    void make_submatrix_by_cols(int cols, int rows, int *matrix,
                                int *new_matrix, std::vector<int> blocks_idxs);
    void perform_addition(char **data_ptrs, char **coding_ptrs, int block_size,
                          const std::vector<int> &data_idxs,
                          const std::vector<int> &parity_idxs);
    void encode_partial_blocks_for_parities_(
        int k_, int *full_matrix, char **data_ptrs, char **coding_ptrs,
        int block_size, const std::vector<int> &data_idxs,
        const std::vector<int> &parity_idxs);
    void decode_with_partial_blocks_(int k_, int *full_matrix, char **data_ptrs,
                                     char **coding_ptrs, int block_size,
                                     const std::vector<int> &failure_idxs,
                                     const std::vector<int> &parity_idxs);
    void encode_partial_blocks_for_failures_(
        int k_, int *full_matrix, char **data_ptrs, char **coding_ptrs,
        int block_size, const std::vector<int> &data_idxs,
        const std::vector<int> &parity_idxs,
        const std::vector<int> &failure_idxs);
    void encode_partial_blocks_for_failures_v2_(
        int k_, int *full_matrix, char **data_ptrs, char **coding_ptrs,
        int block_size, const std::vector<int> &data_idxs,
        const std::vector<int> &failure_idxs,
        const std::vector<int> &live_idxs);
};
} // namespace ECProject