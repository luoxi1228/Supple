#include "ShuffleBasedSWO.hpp"
#include "../../ORShuffle/RecursiveShuffle.hpp"
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>

void ShuffleBasedSWO(unsigned char *buffer, size_t N, size_t M, size_t K,
                     size_t block_size, unsigned char *result_buffer,
                     enc_ret *ret) {
    if (ret == nullptr) {
        return;
    }
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    if (buffer == nullptr || result_buffer == nullptr || N == 0 || M == 0 ||
        M > N || K == 0 || block_size == 0 || K > SIZE_MAX / M ||
        block_size > SIZE_MAX / (K * M)) {
        return;
    }

#ifdef COUNT_OSWAPS
    const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif
    long begin, end;
    ocall_clock(&begin);
    for (size_t sample = 0; sample < K; ++sample) {
        // Each call draws fresh shuffle randomness. A new uniform permutation
        // also makes this sample independent of every previous sample.
        RecursiveShuffle_M2(buffer, N, block_size);
        memcpy(result_buffer + sample * M * block_size, buffer,
               M * block_size);
    }
    ocall_clock(&end);
    ret->ptime = static_cast<double>(end - begin) / 1000.0;
    ret->apply_perm_time = ret->ptime;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif
}

void DecShuffleBasedSWO(unsigned char *encrypted_buffer, size_t N, size_t M,
                        size_t K, size_t encrypted_block_size,
                        unsigned char *encrypted_result_buffer, enc_ret *ret) {
    if (ret == nullptr) {
        return;
    }
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    if (encrypted_buffer == nullptr || encrypted_result_buffer == nullptr ||
        N == 0 || M == 0 || M > N || K == 0 ||
        encrypted_block_size <= SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE ||
        K > SIZE_MAX / M) {
        return;
    }
    const size_t output_blocks = K * M;
    const size_t block_size = encrypted_block_size -
                              SGX_AESGCM_IV_SIZE - SGX_AESGCM_MAC_SIZE;
    if (block_size > SIZE_MAX / output_blocks) {
        return;
    }

    unsigned char *decrypted_buffer = nullptr;
    const size_t decrypted_block_size = decryptBuffer(
        encrypted_buffer, static_cast<uint64_t>(N), encrypted_block_size,
        &decrypted_buffer);
    if (decrypted_buffer == nullptr || decrypted_block_size == SIZE_MAX) {
        free(decrypted_buffer);
        return;
    }
    unsigned char *plain_result = static_cast<unsigned char *>(
        malloc(output_blocks * decrypted_block_size));
    if (plain_result == nullptr) {
        free(decrypted_buffer);
        return;
    }

    PRB_pool_init(1);
    ShuffleBasedSWO(decrypted_buffer, N, M, K, decrypted_block_size,
                    plain_result, ret);
    encryptBuffer(plain_result, static_cast<uint64_t>(output_blocks),
                  decrypted_block_size, encrypted_result_buffer);
    PRB_pool_shutdown();

    free(plain_result);
    free(decrypted_buffer);
}
