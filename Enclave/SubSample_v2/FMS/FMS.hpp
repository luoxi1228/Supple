#ifndef SUPPLE_FMS_HPP
#define SUPPLE_FMS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fms
{

// Routing tags: bit 1 is left responsibility; bit 0 is right responsibility.
enum FMSRoutingTag : uint8_t
{
  FMS_TAG_ZERO = 0,
  FMS_TAG_RIGHT = 1,
  FMS_TAG_LEFT = 2,
  FMS_TAG_BOTH = 3
};

// Control words use the same physical bit positions but different semantics:
// 00 -> (x,x), 01 -> (x,y), 10 -> (y,x), 11 -> (y,y).
enum FMSControlWord : uint8_t
{
  FMS_COPY_FIRST = 0,
  FMS_STRAIGHT = 1,
  FMS_SWAP = 2,
  FMS_COPY_SECOND = 3
};

struct FMSBalanceResult
{
  std::vector<uint8_t> controls;
  std::vector<uint8_t> top_tags;
  std::vector<uint8_t> bottom_tags;
};

struct FMSDataResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
};

struct FMSControlReadResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
  size_t next_pos;
};

struct FMSOutputSwap
{
  size_t first;
  size_t second;
};

std::array<uint8_t, 10> FMSPairMask(uint8_t x, uint8_t y);

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t top_tag,
                     uint8_t bottom_tag);

std::vector<uint8_t> FMSNormalize(const std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right);

FMSBalanceResult FMSBalance(const std::vector<uint8_t> &tags,
                            size_t n,
                            size_t n_left,
                            size_t n_right);

size_t FMSControlCount(size_t n, size_t n_left, size_t n_right);

size_t FMSControlWrite(const std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position);

std::vector<uint8_t> FMSControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right);

// The output order is public: prepare this swap plan before online execution.
std::vector<FMSOutputSwap> FMSOutputSwaps(size_t n,
                                           size_t n_left,
                                           size_t n_right);

// Reorder a balanced power-of-two control tape for the original level-order
// OFork network. This conversion depends only on public dimensions.
std::vector<uint8_t> FMSLevelOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Offline conversion for balanced power-of-two dimensions. The resulting
// tape finishes each contiguous half before the gates between the halves,
// matching ORCompact's execution order.
std::vector<uint8_t> FMSPostOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Apply prepared controls directly to a writable n-record buffer. This path
// performs no dynamic allocations and preserves FMSControlRead's output order.
void FMSApplyInPlace(unsigned char *data,
                     const std::vector<uint8_t> &controls,
                     const std::vector<FMSOutputSwap> &output_swaps,
                     size_t n,
                     size_t n_left,
                     size_t n_right,
                     size_t block_size,
                     bool apply_output_swaps = true);

void FMSApplyLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// Apply a tape returned by FMSPostOrderControls. Balanced power-of-two only.
void FMSApplyPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// The following entry points skip the recursive control-count check. Call
// them only with controls already validated during the offline prepare phase.
void FMSApplyPreparedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

void FMSApplyPreparedLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

void FMSApplyPreparedPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps = true);

// Complete the public output permutation after a network-only apply.
void FMSApplyOutputSwapsInPlace(
    unsigned char *data,
    size_t n,
    size_t block_size,
    const std::vector<FMSOutputSwap> &output_swaps);

FMSControlReadResult FMSControlRead(const unsigned char *data,
                                    const std::vector<uint8_t> &controls,
                                    size_t n,
                                    size_t n_left,
                                    size_t n_right,
                                    size_t block_size,
                                    size_t position);

FMSDataResult FMSApply(const unsigned char *data,
                       const std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size);

FMSDataResult FMSCompact(const unsigned char *data,
                         const std::vector<uint8_t> &tags,
                         size_t n,
                         size_t n_left,
                         size_t n_right,
                         size_t block_size);

} // namespace fms

#endif
