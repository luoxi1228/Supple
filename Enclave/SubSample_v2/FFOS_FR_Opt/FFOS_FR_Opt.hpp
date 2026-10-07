#ifndef FFOS_FR_OPT_HPP
#define FFOS_FR_OPT_HPP

#include "../FFOS_FR/FFOS_FR.hpp"

// Both FR variants use FFOS_FRControl and the identical packed control tape.
std::vector<unsigned char> FFOS_FR_OptApply(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size);

std::vector<unsigned char> FFOS_FR_Opt(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size);

extern "C" void DecFFOS_FR_Opt(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret);

#endif
