#include "FFOS_FR_Opt.hpp"

std::vector<unsigned char> FFOS_FR_OptApply(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size)
{
  return FFOS_FRApply(data, controls, frontier, n, m, k, block_size,
                      FFOS_FRBackend::FourGateSSE2);
}

std::vector<unsigned char> FFOS_FR_Opt(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size)
{
  return FFOS_FR(data, n, m, k, block_size, FFOS_FRBackend::FourGateSSE2);
}

extern "C" void DecFFOS_FR_Opt(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret)
{
  ffos_fr_detail::DecWithBackend(encrypted_buffer, N, M, K,
      encrypted_block_size, encrypt_result_buffer, ret,
      FFOS_FRBackend::FourGateSSE2);
}
