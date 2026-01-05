#include "rs_xor.h"
#include "isa-l.h"
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

    bitmatrix_encode_with_isa(data_ptrs, coding_ptrs, bitmatrix, block_size);
    free(matrix);
    free(bitmatrix);
}

void XORCode::bitmatrix_encode_with_isa(char **data_ptrs, char **coding_ptrs,
                                        int *bitmatrix, int block_size) {
    int packet_size = block_size / w;
    std::vector<void *> srcs;
    srcs.reserve(k * w + 1);

    for (int j = 0; j < m; ++j) {     // m: number of parity blocks
        for (int r = 0; r < w; ++r) { // r: target packet index in parity j
            void *out = coding_ptrs[j] + r * packet_size;
            srcs.clear();
            for (int i = 0; i < k; ++i) {
                for (int y = 0; y < w; ++y) {
                    // bitmatrix layout: [j][i][y][r] → linear index
                    // int idx = ((j * k + i) * w + y) * w + r;
                    int idx = (j * k * w * w) + (r * k * w) + (i * w) + y; 
                    if (bitmatrix[idx]) {
                        srcs.push_back(data_ptrs[i] + y * packet_size);
                    }
                }
            }
            srcs.push_back(out); // xor_gen 要求 dst 在末尾（见 ISA-L）
            if (srcs.size() == 2) {
                memcpy(out, srcs[0], packet_size);
            } else {
                xor_gen(static_cast<int>(srcs.size()), packet_size,
                        srcs.data());
            }
        }
    }
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