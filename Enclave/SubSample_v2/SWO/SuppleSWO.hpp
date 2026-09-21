#ifndef SUPPLE_SWO_HPP
#define SUPPLE_SWO_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include "helper.hpp"
#include "../../../Globals.hpp"

// The eight algorithms in Supple_Latex/algorithms/SWO. An empty F is the root
// sentinel; recursive functions receive explicit node lists. Controls are
// packed into bytes, but their counts and read/write positions are in bits.
// Data records occupy block_size bytes; samples are concatenated in sample order.

std::vector<unsigned char> SWOSample(const unsigned char *D,
    size_t n, size_t m, size_t k,
    size_t block_size);

std::vector<size_t> SWOMark(size_t n, size_t m, size_t k);

std::vector<FrontierNode> SWOFrontier(size_t n, size_t m, size_t k);

std::vector<uint8_t> SWOControl(const std::vector<size_t> &M,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k);

std::vector<unsigned char> SWOApply(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k,
    size_t block_size);

size_t SWOControlCount(const std::vector<FrontierNode> &nodes, size_t n, size_t m);

size_t SWOControlWrite(const std::vector<size_t> &M, std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p);

ControlReadResult SWOControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p);

// ECALL adapter; keeps the generated EDL contract unchanged.
extern "C" void DecSuppleSWO(unsigned char *encrypted_buffer,
    size_t N,
    size_t M,
    size_t K,
    size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer,
    enc_ret *ret);

#endif
