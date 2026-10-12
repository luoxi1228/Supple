#ifndef FFOS_FR_PAR_HPP
#define FFOS_FR_PAR_HPP

#include "../FFOS_FR/FFOS_FR.hpp"

// Parallel generation and application preserve the serial packed tape and
// sample order. nthreads=0 selects the current CPU budget, including the caller.
std::vector<unsigned char> FFOS_FR_ParApply(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, size_t nthreads = 0);

std::vector<unsigned char> FFOS_FR_Par(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size, size_t nthreads = 0);

FFOS_FRControls FFOS_FR_ParControl(
    const std::vector<size_t> &membership, const std::vector<FrontierNode> &frontier,
    size_t n, size_t m, size_t k, enc_ret *profile = nullptr, size_t nthreads = 0);

extern "C" void DecFFOS_FR_Par(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, size_t nthreads, enc_ret *ret);

#endif
