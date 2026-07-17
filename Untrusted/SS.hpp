#ifndef __SS_HPP__
#define __SS_HPP__

void decryptAndSubSample(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleMulti(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void decryptAndSubSampleMulti_opt(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void DecPSQF_single(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void DecPSQF_SWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

void DecSuppleSWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encrypt_result_buffer, enc_ret *ret);

void DecSuppleSWO_parallel(unsigned char *encrypted_buffer, size_t N, size_t M, size_t K, size_t encrypted_block_size, unsigned char *encrypt_result_buffer, enc_ret *ret, size_t nthreads);

#endif
