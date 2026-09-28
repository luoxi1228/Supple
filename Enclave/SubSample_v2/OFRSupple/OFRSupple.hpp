#ifndef SUPPLE_OFR_SUPPLE_HPP
#define SUPPLE_OFR_SUPPLE_HPP

#include "../SWO/helper.hpp"
#include "../OFR/OFR.hpp"
#include "../../../Globals.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Selection controls are packed into bytes; their positions count bits.
// OFR controls occupy one byte per two-bit word; their positions count words.
struct OFRSuppleControlCounts
{
  size_t swo_bits;
  size_t ofr_words;
};

struct OFRSuppleControlPositions
{
  size_t swo_bits;
  size_t ofr_words;
};

struct OFRSuppleControls
{
  std::vector<uint8_t> swo;
  std::vector<uint8_t> ofr;
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
  // A populated nodes array means each span of ofr has been converted in
  // place to the layout selected by that node. An empty array keeps ofr in
  // the raw DFS layout used by OFRSuppleControlWrite.
  std::vector<Shape> shapes;
  std::vector<Node> nodes;
};

struct OFRSuppleReadResult
{
  size_t written_blocks;
  OFRSuppleControlPositions next;
};

OFRSuppleControlCounts OFRSuppleControlCount(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k);

OFRSuppleControlPositions OFRSuppleControlWrite(
    const std::vector<size_t> &membership, OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position, enc_ret *profile = nullptr);

OFRSuppleControls OFRSuppleControl(
    const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile = nullptr);

OFRSuppleReadResult OFRSuppleControlRead(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, unsigned char *samples, size_t out_capacity_blocks,
    OFRSuppleControlPositions position, enc_ret *profile = nullptr);

std::vector<unsigned char> OFRSuppleApply(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size);

std::vector<unsigned char> OFRSupple(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size);

extern "C" void DecOFRSupple(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret);

#endif
