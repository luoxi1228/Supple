#ifndef FFOS_FR_HPP
#define FFOS_FR_HPP

#include "../FFOS_C/helper.hpp"
#include "../OFR/OFR.hpp"
#include "../../../Globals.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Public implementation choice; controls, sampling and memory ownership are
// shared. Mode 5 uses scalar OFork, mode 6 enables four-gate SSE2 at B=8/16.
enum class FFOS_FRBackend { Scalar, FourGateSSE2 };

// Selection controls are packed into bytes; their positions count bits.
// OFR controls occupy two bits per word; their positions count logical words.
struct FFOS_FRControlCounts
{
  size_t swo_bits;
  size_t ofr_words;
};

struct FFOS_FRControlPositions
{
  size_t swo_bits;
  size_t ofr_words;
};

struct FFOS_FRControls
{
  std::vector<uint8_t> swo;
  ofr::PackedControls ofr;
  struct Shape
  {
    size_t n;
    size_t n_left;
    size_t n_right;
  };
  struct Node
  {
    size_t offset;
    size_t shape_index;
    bool postordered;
    size_t count;
  };
  // A populated nodes array means each span of ofr was written directly in
  // the layout selected by that node. An empty array keeps ofr in
  // the raw DFS layout used by FFOS_FRControlWrite.
  std::vector<Shape> shapes;
  std::vector<Node> nodes;
};

struct FFOS_FRReadResult
{
  size_t written_blocks;
  FFOS_FRControlPositions next;
};

FFOS_FRControlCounts FFOS_FRControlCount(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k);

FFOS_FRControlPositions FFOS_FRControlWrite(
    const std::vector<size_t> &membership, FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    FFOS_FRControlPositions position, enc_ret *profile = nullptr);

FFOS_FRControls FFOS_FRControl(
    const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile = nullptr);

FFOS_FRReadResult FFOS_FRControlRead(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, unsigned char *samples, size_t out_capacity_blocks,
    FFOS_FRControlPositions position, enc_ret *profile = nullptr,
    FFOS_FRBackend backend = FFOS_FRBackend::Scalar);

std::vector<unsigned char> FFOS_FRApply(
    const unsigned char *data, const FFOS_FRControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, FFOS_FRBackend backend = FFOS_FRBackend::Scalar);

std::vector<unsigned char> FFOS_FR(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size,
    FFOS_FRBackend backend = FFOS_FRBackend::Scalar);

namespace ffos_fr_detail {
void DecWithBackend(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret, FFOS_FRBackend backend);
}

extern "C" void DecFFOS_FR(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret);

#endif
