#pragma once
#include "erasure_code.h"
#include "../jerasure_wrapper.h"


namespace ECProject {
class RSCode : public ErasureCode {
  public:
    RSCode() {}
    RSCode(int k, int m) : ErasureCode(k, m) {}
    ~RSCode() override {}

    void encode(char **data_ptrs, char **coding_ptrs, int block_size) override;
    void decode(char **data_ptrs, char **coding_ptrs, int block_size,
                int *erasures, int failed_num) override;
    void make_encoding_matrix(int *final_matrix) override;
    
};
} // namespace ECProject
