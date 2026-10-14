#ifndef FFOS_C_OPT_HPP
#define FFOS_C_OPT_HPP

#include "../FFOS_C/FFOS_C.hpp"

// The selection tape is byte-for-byte FFOS-C. Public shape metadata permits
// sibling edges to execute before their descendants without rewriting it.
struct FFOS_C_OptControls {
  struct Node {
    size_t input_blocks, sample_start, sample_count, control_offset;
    size_t left, right;
  };
  std::vector<uint8_t> bits;
  std::vector<Node> nodes;
  std::vector<size_t> roots;
  size_t n = 0, m = 0, k = 0, bit_count = 0;
};

FFOS_C_OptControls FFOS_C_OptControl(const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile = nullptr);

// Inputs and output must be disjoint. Validation precedes every output write.
ControlReadResult FFOS_C_OptControlRead(const unsigned char *data,
    const FFOS_C_OptControls &controls, size_t n, size_t m, size_t k,
    size_t block_size, unsigned char *samples, size_t out_capacity_blocks,
    enc_ret *profile = nullptr);

std::vector<unsigned char> FFOS_C_OptApply(const unsigned char *data,
    const FFOS_C_OptControls &controls, size_t n, size_t m, size_t k,
    size_t block_size);

std::vector<unsigned char> FFOS_C_Opt(const unsigned char *data,
    size_t n, size_t m, size_t k, size_t block_size);

extern "C" void DecFFOS_C_Opt(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret);

#endif
