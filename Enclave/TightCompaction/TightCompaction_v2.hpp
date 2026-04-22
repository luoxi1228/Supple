#ifndef __NOP_TIGHTCOMPACTION_V2_HPP__
#define __NOP_TIGHTCOMPACTION_V2_HPP__

#ifndef BEFTS_MODE
  #include <sgx_tcrypto.h>
  #include "../Enclave_globals.h"
  #include "../oasm_lib.h"
  #include "../utils.hpp"
  #include <vector>
  #include "../foav.h"
#endif

template<OSwap_Style oswap_style>
void TightCompact_2power(unsigned char *buf, size_t N, size_t block_size,
       size_t offset, bool *selected);

template<OSwap_Style oswap_style>
void TightCompact_2power_inner(unsigned char *buf, size_t N, 
      size_t block_size, size_t offset, bool *selected, uint32_t *selected_count);

template<OSwap_Style oswap_style>
void TightCompact(unsigned char *buf, size_t N, size_t block_size, bool *selected);

template<OSwap_Style oswap_style>
void TightCompact_inner(unsigned char *buf, size_t N, size_t block_size, bool *selected, uint32_t *selected_count);

template<OSwap_Style oswap_style>
void TightCompact_parallel(unsigned char *buf, size_t N, size_t block_size, bool *selected, size_t nthreads);

template<OSwap_Style oswap_style>
void TightCompact_inner_parallel(unsigned char *buf, size_t N, size_t block_size, bool *selected, uint32_t *selected_count, size_t nthreads);

template<OSwap_Style oswap_style>
void TightCompact_2power_inner_parallel(unsigned char *buf, size_t N,
      size_t block_size, size_t offset, bool *selected, uint32_t *selected_count,
      size_t nthreads);

template <OSwap_Style oswap_style>
void OP_TightCompact_v2(unsigned char *buf, size_t block_size, bool *selected_list);

void compute_LS_distances(uint64_t N, unsigned char *buffer_start, size_t block_size, 
        bool *selected_list, uint64_t *LS_distance);

#include "TightCompaction_v2.tcc"

#endif
