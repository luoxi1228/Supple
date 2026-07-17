#ifndef __SUBSAMPLE_V2_SUPPLE_SWO_HPP__
#define __SUBSAMPLE_V2_SUPPLE_SWO_HPP__

#ifndef BEFTS_MODE
  #include <cstddef>
  #include <cstdint>
  #include <vector>

  #include "helper.hpp"
  #include "../../ObliviousPrimitives.hpp"
  #include "../../RecursiveShuffle/RecursiveShuffle.hpp"
  #include "../../utils.hpp"
  #include "../../Enclave_globals.h"
  #include "../../../Globals.hpp"
#endif

struct FrontierNode
{
  size_t start;
  size_t count;
};

struct ControlReadResult
{
  size_t written_blocks;
  size_t next_pos;
};

std::vector<size_t> MARKMATRIX(size_t n, size_t m, size_t k);

std::vector<FrontierNode> FRONTIER(size_t s, size_t l, size_t n, size_t m);

std::vector<FrontierNode> NEXTNODES(const std::vector<FrontierNode> &F,
                                    size_t k);

size_t CONTROLNUM(const std::vector<FrontierNode> &F,
                  size_t n,
                  size_t m,
                  size_t k);

std::vector<uint8_t> CONTROLBITS(const std::vector<size_t> &M,
                                 const std::vector<FrontierNode> &F,
                                 size_t n,
                                 size_t m,
                                 size_t k);

size_t CONTROLWRITE(const std::vector<size_t> &M,
                    std::vector<uint8_t> &C,
                    const std::vector<FrontierNode> &F,
                    size_t m,
                    size_t k,
                    size_t p);

size_t CONTROLWRITE_WORKSPACE(const size_t *M,
                              size_t n,
                              size_t mark_words,
                              std::vector<uint8_t> &C,
                              const std::vector<FrontierNode> &F,
                              size_t m,
                              size_t k,
                              size_t p,
                              std::vector<SwoMarkWorkspace> &workspaces,
                              size_t depth);

std::vector<unsigned char> RECSAMPLE(const unsigned char *D,
                                     const std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t n,
                                     size_t m,
                                     size_t k,
                                     size_t block_size);

ControlReadResult CONTROLREAD(unsigned char *D,
                              const std::vector<uint8_t> &C,
                              const std::vector<FrontierNode> &F,
                              size_t n,
                              size_t m,
                              size_t k,
                              size_t block_size,
                              unsigned char *S,
                              size_t out_capacity_blocks,
                              size_t p);

ControlReadResult CONTROLREAD_WORKSPACE(unsigned char *D,
                                        const std::vector<uint8_t> &C,
                                        const std::vector<FrontierNode> &F,
                                        size_t n,
                                        size_t m,
                                        size_t k,
                                        size_t block_size,
                                        unsigned char *S,
                                        size_t out_capacity_blocks,
                                        size_t p,
                                        std::vector<SwoDataWorkspace> &workspaces,
                                        size_t depth);

std::vector<unsigned char> FILTERDATA(const unsigned char *D,
                                      const std::vector<uint8_t> &Cv,
                                      size_t n,
                                      size_t t,
                                      size_t block_size);

std::vector<size_t> FILTERMARK(const std::vector<size_t> &M,
                               const std::vector<uint8_t> &Cv,
                               size_t t,
                               size_t s,
                               size_t l,
                               size_t k);

std::vector<unsigned char> OMBSUBSAMPLE(const unsigned char *D,
                                        size_t n,
                                        size_t m,
                                        size_t k,
                                        size_t block_size);

void DecSuppleSWO(unsigned char *encrypted_buffer,
                  size_t N,
                  size_t M,
                  size_t K,
                  size_t encrypted_block_size,
                  unsigned char *encrypt_result_buffer,
                  enc_ret *ret);

#endif
