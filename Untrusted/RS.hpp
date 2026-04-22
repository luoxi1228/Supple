#ifndef __RS_HPP__
#define __RS_HPP__

void RecursiveShuffle_M1(unsigned char *buf, uint64_t N, size_t len);
double RecursiveShuffle_M2(unsigned char *buf, uint64_t N, size_t len);


double DecryptAndShuffleM1(unsigned char *buf, size_t N, size_t encrypted_block_size, unsigned char *result_buf, enc_ret *ret);
double DecryptAndShuffleM2(unsigned char *buf, size_t N, size_t encrypted_block_size, size_t nthreads, unsigned char *result_buf, enc_ret *ret);


#endif
