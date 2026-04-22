#include "SubSampleShuffle.hpp"
#include "../ObliviousPrimitives.hpp"
#include "../RecursiveShuffle/RecursiveShuffle.hpp"
#include <cstring>
#include <cstdlib>

void subSampleShuffle(unsigned char *buffer, size_t N, size_t M, size_t block_size, unsigned char *result_buffer, enc_ret *ret){
    if (M > N) {
        M = N;
    }
    if (M == 0) {
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }
    if (result_buffer == NULL) {
        printf("subSampleShuffle: result_buffer is NULL\n");
        return;
    }

    long t0, t1;
    ocall_clock(&t0);

    // 1. Shuffle the entire buffer
    RecursiveShuffle_M2(buffer, N, block_size);

    // 2. Copy first M blocks after shuffle
    memcpy(result_buffer, buffer, M * block_size);

    ocall_clock(&t1);
    ret->ptime = ((double)(t1 - t0)) / 1000.0;

  #ifdef COUNT_OSWAPS
    ret->OSWAP_count = OSWAP_COUNTER;
  #endif

}

void decryptAndSubSampleShuffle(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret) {
    // Decrypt buffer to decrypted_buffer
    unsigned char *decrypted_buffer = NULL;
    size_t decrypted_block_size = decryptBuffer(encrypted_buffer, (uint64_t) N, encrypted_block_size, &decrypted_buffer);

    if (M > N) {
        M = N;
    }
    if (M == 0) {
        free(decrypted_buffer);
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }

    unsigned char *result_buffer = (unsigned char *)malloc(M * decrypted_block_size);
    if (result_buffer == NULL) {
        printf("Malloc failed in decryptAndSubSampleShuffle for %ld bytes\n", (M * decrypted_block_size));
        free(decrypted_buffer);
        return;
    }

    PRB_pool_init(1);
    subSampleShuffle(decrypted_buffer, N, M, decrypted_block_size, result_buffer, ret);

    // encrytBuffer
    encryptBuffer(result_buffer, (uint64_t) M, decrypted_block_size, encryt_result_buffer);
    PRB_pool_shutdown();

    free(decrypted_buffer);
    free(result_buffer);
}

void subSampleSWO(unsigned char *buffer, size_t N, size_t M, size_t block_size, unsigned char *result_buffer, enc_ret *ret){
    if (M > N) {
        M = N;
    }
    if (M == 0) {
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }
    if (result_buffer == NULL) {
        printf("subSampleSWO: result_buffer is NULL\n");
        return;
    }
    if (N % M != 0) {
        printf("subSampleSWO: N %% M != 0, falling back to single sample\n");
        M = N;
    }
    size_t k = N / M;
    if (k == 0) {
        k = 1;
        M = N;
    }

    const size_t enc_block_size = block_size + SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;
    const size_t id_plain_size = sizeof(size_t);
    const size_t enc_index_size = id_plain_size + SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;
    //const size_t tuple_size = enc_block_size + enc_index_size; // (re-enc(e), enc(i))
    // 计算原始 tuple 大小
    size_t raw_tuple_size = enc_block_size + enc_index_size; 
    
    // 【核心修复】：将 tuple_size 向上对齐到 16 的倍数
    // 如果 raw_tuple_size 是 76，对齐后 tuple_size 将变成 80
    const size_t tuple_size = (raw_tuple_size + 15) & ~15;

    unsigned char *S = (unsigned char *)malloc(N * tuple_size);
    size_t *counts = (size_t *)calloc(k, sizeof(size_t));
    if (S == NULL || counts == NULL) {
        printf("Allocating memory failed in subSampleSWO\n");
        free(S);
        free(counts);
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }

    long t0, t1, begin, end;
    // double time_init = 0.0;
    // double time_sample1 = 0.0;
    // double time_shuffle_d = 0.0;
    // double time_shuffle_s = 0.0;
    // double time_sample2 = 0.0;

    ocall_clock(&begin);

    // 1: D <- oblshuffle(D)
    // ocall_clock(&t0);
    RecursiveShuffle_M2(buffer, N, block_size);
    // ocall_clock(&t1);
    // time_shuffle_d = ((double)(t1 - t0)) / 1000.0;

    // 2: SWO.initialize(n, m)
    SWO swo;
    // ocall_clock(&t0);
    swo.initialize(N, M);
    // ocall_clock(&t1);
    // time_init = ((double)(t1 - t0)) / 1000.0;

    unsigned char *id_buf = (unsigned char *)malloc(id_plain_size);
    if (id_buf == NULL) {
        printf("Allocating memory failed in subSampleSWO (id buffers)\n");
        free(S);
        free(counts);
        free(id_buf);
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }

    // Prepare zero-padded plaintext buffer for enc(i)
    memset(id_buf, 0, id_plain_size);

    // 3-14: Replication scan
    size_t l = 0;
    size_t j = 0;
    unsigned char *e_ptr = buffer;
    unsigned char *e_next_ptr = buffer;
    unsigned char *s_ptr = S;

    // ocall_clock(&t0);
    while (l < N && j < N) {
        for (size_t i = 0; i < k; ++i) {
            bool is_member = swo.samplemember_n(i, j);
            if (is_member) {
                // re-enc(e)
                encryptBuffer(e_ptr, 1, block_size, s_ptr);
                // enc(i)
                size_t sid = i;
                memcpy(id_buf, &sid, id_plain_size);
                encryptBuffer(id_buf, 1, id_plain_size, s_ptr + enc_block_size);
                s_ptr += tuple_size;
                l++;
                if (l < N) {
                    e_next_ptr = buffer + (l * block_size);
                }
            }
        }
        e_ptr = e_next_ptr;
        j++;
    }
    // ocall_clock(&t1);
    // time_sample1 = ((double)(t1 - t0)) / 1000.0;

    // 15: S <- oblshuffle(S)
    // ocall_clock(&t0);
    RecursiveShuffle_M2(S, N, tuple_size);
    // ocall_clock(&t1);
    // time_shuffle_s = ((double)(t1 - t0)) / 1000.0;

    // 16-20: Group by sample id
    unsigned char saved_key[SGX_AESGCM_KEY_SIZE];
    memcpy(saved_key, enclave_decryption_key, SGX_AESGCM_KEY_SIZE);
    memcpy(enclave_decryption_key, enclave_encryption_key, SGX_AESGCM_KEY_SIZE);

    unsigned char *s_group_ptr = S;

    // ocall_clock(&t0);
    for (size_t t = 0; t < N; ++t) {
        size_t sid = 0;
        // dec(ci)
        unsigned char *dec_ptr = id_buf;
        decryptBuffer(s_group_ptr + enc_block_size, 1, enc_index_size, &dec_ptr);
        memcpy(&sid, dec_ptr, id_plain_size);
        if (sid < k && counts[sid] < M) {
            size_t out_index = (sid * M) + counts[sid];
            memcpy(result_buffer + (out_index * enc_block_size), s_group_ptr, enc_block_size);
            counts[sid] += 1;
        }
        s_group_ptr += tuple_size;
    }
    // ocall_clock(&t1);
    // time_sample2 = ((double)(t1 - t0)) / 1000.0;

    memcpy(enclave_decryption_key, saved_key, SGX_AESGCM_KEY_SIZE);
    
    ocall_clock(&end);
    ret->ptime = ((double)(end - begin)) / 1000.0;

    // printf("SWO timing (ms): totalTime=%f, init=%f, sample1=%f, sample2=%f, shuffleD=%f, shuffleS=%f\n", ret->ptime,
    //        time_init, time_sample1, time_sample2, time_shuffle_d, time_shuffle_s);
    
  #ifdef COUNT_OSWAPS
    ret->OSWAP_count = OSWAP_COUNTER;
  #endif

    free(S);
    free(counts);
    free(id_buf);
}

void decryptAndSubSampleSWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret){
    unsigned char *decrypted_buffer = NULL;
    size_t decrypted_block_size = decryptBuffer(encrypted_buffer, (uint64_t) N, encrypted_block_size, &decrypted_buffer);

    if (M > N) {
        M = N;
    }
    if (M == 0) {
        free(decrypted_buffer);
        ret->ptime = 0.0;
  #ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
  #endif
        return;
    }

    PRB_pool_init(1);
    subSampleSWO(decrypted_buffer, N, M, decrypted_block_size, encryt_result_buffer, ret);
    PRB_pool_shutdown();

    free(decrypted_buffer);
}
