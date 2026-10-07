#ifndef FFOS_C_HPP
#define FFOS_C_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include "helper.hpp"
#include "../../../Globals.hpp"

// FFOS-C sampling and its control/apply stages. An empty F is the root
// sentinel; recursive functions receive explicit node lists. Controls are
// packed into bytes, but their counts and read/write positions are in bits.
// Data records occupy block_size bytes; samples are concatenated in sample order.

std::vector<unsigned char> FFOS_C(const unsigned char *D,
    size_t n, size_t m, size_t k,
    size_t block_size);

std::vector<size_t> FFOS_CMark(size_t n, size_t m, size_t k);

std::vector<FrontierNode> FFOS_CFrontier(size_t n, size_t m, size_t k);

std::vector<uint8_t> FFOS_CControl(const std::vector<size_t> &M,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k);

std::vector<unsigned char> FFOS_CApply(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k,
    size_t block_size);

size_t FFOS_CControlCount(const std::vector<FrontierNode> &nodes, size_t n, size_t m);

size_t FFOS_CControlWrite(const std::vector<size_t> &M, std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p);

ControlReadResult FFOS_CControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p, enc_ret *profile = nullptr);

// ECALL adapter; parameter layout matches the sampling interface.
extern "C" void DecFFOS_C(unsigned char *encrypted_buffer,
    size_t N,
    size_t M,
    size_t K,
    size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer,
    enc_ret *ret);

#endif
