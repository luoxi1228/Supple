#ifndef __SUBSAMPLE_V2_SUPPLE_SWO_PARALLEL_HPP__
#define __SUBSAMPLE_V2_SUPPLE_SWO_PARALLEL_HPP__

#ifndef BEFTS_MODE
  #include <cstddef>
  #include <cstdint>
  #include <vector>
  #include "../../Globals.hpp"
#endif

#include "helper.hpp"

namespace swo_parallel {
using detail::FrontierNode;
using detail::ControlReadResult;

std::vector<uint8_t> CONTROLBITS_PARALLEL(const std::vector<size_t> &M,
                                          const std::vector<FrontierNode> &F,
                                          size_t n,
                                          size_t m,
                                          size_t k,
                                          size_t nthreads);

size_t CONTROLWRITE_PARALLEL(const std::vector<size_t> &M,
                             std::vector<uint8_t> &C,
                             const std::vector<FrontierNode> &F,
                             size_t m,
                             size_t k,
                             size_t p,
                             size_t nthreads);

ControlReadResult CONTROLREAD_PARALLEL(unsigned char *D,
                                       const std::vector<uint8_t> &C,
                                       const std::vector<FrontierNode> &F,
                                       size_t n,
                                       size_t m,
                                       size_t k,
                                       size_t block_size,
                                       unsigned char *S,
                                       size_t out_capacity_blocks,
                                       size_t p,
                                       size_t nthreads);

std::vector<unsigned char> RECSAMPLE_PARALLEL(const unsigned char *D,
                                             const std::vector<uint8_t> &C,
                                             const std::vector<FrontierNode> &F,
                                             size_t n,
                                             size_t m,
                                             size_t k,
                                             size_t block_size,
                                             size_t nthreads);

std::vector<unsigned char> OMBSUBSAMPLE_PARALLEL(const unsigned char *D,
                                                size_t n,
                                                size_t m,
                                                size_t k,
                                                size_t block_size,
                                                size_t nthreads);

} // namespace swo_parallel

extern "C" void DecSuppleSWO_parallel(unsigned char *encrypted_buffer,
                                      size_t N,
                                      size_t M,
                                      size_t K,
                                      size_t encrypted_block_size,
                                      unsigned char *encrypt_result_buffer,
                                      enc_ret *ret,
                                      size_t nthreads);

#endif
