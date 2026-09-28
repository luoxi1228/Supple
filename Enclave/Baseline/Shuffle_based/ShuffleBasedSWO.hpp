#ifndef __BASELINE_SHUFFLE_BASED_SWO_HPP__
#define __BASELINE_SHUFFLE_BASED_SWO_HPP__

#include <cstddef>
#include "../../../Globals.hpp"
#include "../../Enclave_globals.h"

// Draw K independent SWO samples by shuffling all N records K times.
// The output contains K consecutive samples of M plaintext records each.
void ShuffleBasedSWO(unsigned char *buffer, size_t N, size_t M, size_t K,
                     size_t block_size, unsigned char *result_buffer,
                     enc_ret *ret);

// Decrypt the input once, generate the samples, then encrypt the output.
extern "C" void DecShuffleBasedSWO(unsigned char *encrypted_buffer, size_t N,
                                    size_t M, size_t K,
                                    size_t encrypted_block_size,
                                    unsigned char *encrypted_result_buffer,
                                    enc_ret *ret);

#endif
