#ifndef __RECURSIVESHUFFLE_HPP__
#define __RECURSIVESHUFFLE_HPP__

#ifndef BEFTS_MODE
  #include "../foav.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RS_RB_BUFFER_LIMIT 819200
#define RS_MARKHALF_MAX_COINS 2048


void MarkHalf(uint64_t N, bool *selected_list);

void RecursiveShuffle_M1(unsigned char *buf, uint64_t N, size_t block_size);
void RecursiveShuffle_M2(unsigned char *buf, uint64_t N, size_t block_size);
void RecursiveShuffle_M2_parallel(unsigned char *buf, uint64_t N, size_t block_size, size_t nthreads);

void RecursiveShuffle_M1_inner_16x(unsigned char *buf, uint64_t N, size_t block_size);

double DecryptAndShuffleM1(unsigned char *encrypted_buffer, size_t N, size_t encrypted_block_size, unsigned char *result_buffer, enc_ret *ret);
double DecryptAndShuffleM2(unsigned char *encrypted_buffer, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char *result_buffer, enc_ret *ret);

#ifdef __cplusplus
}
#endif

#include "RecursiveShuffle.tcc"

#endif
