#include "rs.h"

using namespace ECProject;

void RSCode::make_encoding_matrix(int *final_matrix) {
    int *matrix = reed_sol_vandermonde_coding_matrix(k, m, w);

    bzero(final_matrix, sizeof(int) * k * m);

    for (int i = 0; i < m; i++) {
        for (int j = 0; j < k; j++) {
            final_matrix[i * k + j] = matrix[i * k + j];
        }
    }

    free(matrix);
}

void RSCode::encode(char **data_ptrs, char **coding_ptrs, int block_size) {
    std::vector<int> rs_matrix(k * m, 0);
    make_encoding_matrix(rs_matrix.data());
    jerasure_matrix_encode(k, m, w, rs_matrix.data(), data_ptrs, coding_ptrs,
                           block_size);
}

void RSCode::decode(char **data_ptrs, char **coding_ptrs, int block_size,
                    int *erasures, int failed_num) {
    if (failed_num > m) {
        std::cout << "[Decode] Undecodable!" << std::endl;
        return;
    }
    int *rs_matrix = reed_sol_vandermonde_coding_matrix(k, m, w);
    int ret = 0;
    ret = jerasure_matrix_decode(k, m, w, rs_matrix, failed_num, erasures,
                                 data_ptrs, coding_ptrs, block_size);
    if (ret == -1) {
        std::cout << "[Decode] Failed!" << std::endl;
        return;
    }
}

std::unique_ptr<ErasureCode> RSCode::clone() const{
    return std::make_unique<RSCode>(*this);
}