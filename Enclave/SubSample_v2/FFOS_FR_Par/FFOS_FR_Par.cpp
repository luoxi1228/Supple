#include "FFOS_FR_Par.hpp"

std::vector<unsigned char> FFOS_FR_ParApply(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, size_t nthreads)
{
  return ffos_fr_detail::ParallelApply(data, controls, frontier, n, m, k, block_size, nthreads);
}

std::vector<unsigned char> FFOS_FR_Par(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size, size_t nthreads)
{
  return ffos_fr_detail::ParallelSample(data, n, m, k, block_size, nthreads);
}

extern "C" void DecFFOS_FR_Par(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, size_t nthreads, enc_ret *ret)
{
  ffos_fr_detail::DecWithBackend(encrypted_buffer, N, M, K,
      encrypted_block_size, encrypt_result_buffer, ret,
      FFOS_FRBackend::FourGateSSE2, nthreads, true);
}

FFOS_FRControls FFOS_FR_ParControl(const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile, size_t nthreads) {
  return ffos_fr_detail::ParallelControl(membership, frontier, n, m, k, profile, nthreads);
}
