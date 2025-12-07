#pragma once
#include "../jerasure_wrapper.h"
#include "erasure_code.h"

namespace ECProject {
class XORCode : public ErasureCode {
  public:
    XORCode() {}
    XORCode(int k, int m) : ErasureCode(k, m) {}
    XORCode(int k, int m, int w) : ErasureCode(k, m, w) {}
    ~XORCode() override {}

    void encode(char **data_ptrs, char **coding_ptrs, int block_size) override;
    void decode(char **data_ptrs, char **coding_ptrs, int block_size,
                int *erasures, int failed_num) override;
    void make_encoding_matrix(int *final_matrix) override;

    void matrix_to_bitmatrix(int *matrix, int *bitmatrix);
    std::unique_ptr<ErasureCode> clone() const override;
};
} // namespace ECProject
