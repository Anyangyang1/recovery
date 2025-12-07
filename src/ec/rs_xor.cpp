#include "rs_xor.h"

using namespace ECProject;

void XORCode::make_encoding_matrix(int *final_matrix) {
    int *matrix = cauchy_original_coding_matrix(k, m, w);

    bzero(final_matrix, sizeof(int) * k * m);

    for (int i = 0; i < m; i++) {
        for (int j = 0; j < k; j++) {
            final_matrix[i * k + j] = matrix[i * k + j];
        }
    }

    free(matrix);
}

void XORCode::matrix_to_bitmatrix(int *matrix, int *final_bitmatrix) {
    int *bitmatrix = jerasure_matrix_to_bitmatrix(k, m, w, matrix);
    bzero(final_bitmatrix, sizeof(int) * k * w * m * w);
    for (int i = 0; i < m * w; i++) {
        for (int j = 0; j < k * w; j++) {
            final_bitmatrix[i * (k * w) + j] = bitmatrix[i * (k * w) + j];
        }
    }
    free(bitmatrix);
}

void XORCode::encode(char **data_ptrs, char **coding_ptrs, int block_size) {
    int *matrix = cauchy_original_coding_matrix(k, m, w);
    int *bitmatrix = jerasure_matrix_to_bitmatrix(k, m, w, matrix);

    jerasure_bitmatrix_encode(k, m, w, bitmatrix, data_ptrs, coding_ptrs,
                              block_size, block_size / w);
    free(matrix);
    free(bitmatrix);
}

void XORCode::decode(char **data_ptrs, char **coding_ptrs, int block_size,
                     int *erasures, int failed_num) {
    if (failed_num > m) {
        ELOG(ERROR) << "[Decode] Undecodable!";
        return;
    }
    int *matrix = cauchy_original_coding_matrix(k, m, w);
    int *bitmatrix = jerasure_matrix_to_bitmatrix(k, m, w, matrix);
    int ret = 0;
    ret = jerasure_bitmatrix_decode(k, m, w, bitmatrix, failed_num, erasures,
                                    data_ptrs, coding_ptrs, block_size,
                                    block_size / w);
    free(matrix);
    free(bitmatrix);
    if (ret == -1) {
        ELOG(ERROR) << "[Decode] Failed!";
        return;
    }
}

std::unique_ptr<ErasureCode> XORCode::clone() const {
    return std::make_unique<XORCode>(*this);
}