#ifndef __SS_HPP__
#define __SS_HPP__

void decryptAndSubSample(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleMulti(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleMulti_opt(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleShuffle(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleSWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);


#endif

