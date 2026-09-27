#ifndef SUPPLE_OFR_HPP
#define SUPPLE_OFR_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ofr
{

// Routing tags: bit 1 is left responsibility; bit 0 is right responsibility.
enum OFRRoutingTag : uint8_t
{
  OFR_TAG_ZERO = 0,
  OFR_TAG_RIGHT = 1,
  OFR_TAG_LEFT = 2,
  OFR_TAG_BOTH = 3
};

// Control words use the same physical bit positions but different semantics:
// 00 -> (x,x), 01 -> (x,y), 10 -> (y,x), 11 -> (y,y).
enum OFRControlWord : uint8_t
{
  OFR_COPY_FIRST = 0,
  OFR_STRAIGHT = 1,
  OFR_SWAP = 2,
  OFR_COPY_SECOND = 3
};

struct OFRBalanceResult
{
  std::vector<uint8_t> controls;
  std::vector<uint8_t> top_tags;
  std::vector<uint8_t> bottom_tags;
};

struct OFRDataResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
};

struct OFRControlReadResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
  size_t next_pos;
};

struct OFROutputSwap
{
  size_t first;
  size_t second;
};

std::array<uint8_t, 10> OFRPairMask(uint8_t x, uint8_t y);

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t top_tag,
                     uint8_t bottom_tag);

std::vector<uint8_t> OFRNormalize(const std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right);

OFRBalanceResult OFRBalance(const std::vector<uint8_t> &tags,
                            size_t n,
                            size_t n_left,
                            size_t n_right);

size_t OFRControlCount(size_t n, size_t n_left, size_t n_right);

size_t OFRControlWrite(const std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position);

std::vector<uint8_t> OFRControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right);

// The output order is public: prepare this swap plan before online execution.
std::vector<OFROutputSwap> OFROutputSwaps(size_t n,
                                           size_t n_left,
                                           size_t n_right);

// Reorder a balanced power-of-two control tape for the original level-order
// OFork network. This conversion depends only on public dimensions.
std::vector<uint8_t> OFRLevelOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Offline conversion for balanced power-of-two dimensions. The resulting
// tape finishes each contiguous half before the gates between the halves,
// matching ORCompact's execution order.
std::vector<uint8_t> OFRPostOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Apply prepared controls directly to a writable n-record buffer. This path
// performs no dynamic allocations and preserves OFRControlRead's output order.
void OFRApplyInPlace(unsigned char *data,
                     const std::vector<uint8_t> &controls,
                     const std::vector<OFROutputSwap> &output_swaps,
                     size_t n,
                     size_t n_left,
                     size_t n_right,
                     size_t block_size,
                     bool apply_output_swaps = true);

void OFRApplyLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<OFROutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// Apply a tape returned by OFRPostOrderControls. Balanced power-of-two only.
void OFRApplyPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<OFROutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// The following entry points skip the recursive control-count check. Call
// them only with controls already validated during the offline prepare phase.
void OFRApplyPreparedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &controls,
    const std::vector<OFROutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

void OFRApplyPreparedLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<OFROutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

void OFRApplyPreparedPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<OFROutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// Complete the public output permutation after a network-only apply.
void OFRApplyOutputSwapsInPlace(
    unsigned char *data,
    size_t n,
    size_t block_size,
    const std::vector<OFROutputSwap> &output_swaps);

OFRControlReadResult OFRControlRead(const unsigned char *data,
                                    const std::vector<uint8_t> &controls,
                                    size_t n,
                                    size_t n_left,
                                    size_t n_right,
                                    size_t block_size,
                                    size_t position);

OFRDataResult OFRApply(const unsigned char *data,
                       const std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size);

OFRDataResult OFRCompact(const unsigned char *data,
                         const std::vector<uint8_t> &tags,
                         size_t n,
                         size_t n_left,
                         size_t n_right,
                         size_t block_size);

} // namespace ofr

#endif
