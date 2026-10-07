#ifndef SUPPLE_OFR_HPP
#define SUPPLE_OFR_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "PackedControls.hpp"

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
  PackedControls controls;
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

std::array<uint8_t, 10> OFRPairMask(uint8_t x, uint8_t y);

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t top_tag,
                     uint8_t bottom_tag);

std::vector<uint8_t> OFRNormalize(const std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right);

void OFRNormalizeInPlace(std::vector<uint8_t> &tags,
                         size_t n, size_t n_left, size_t n_right);

namespace detail
{
// Internal owned-label path: the caller has generated only tags 0..3 and
// accumulated their weights. Public normalization retains complete checking.
void OFRNormalizeOwnedInPlace(std::vector<uint8_t> &tags,
                              size_t n, size_t n_left, size_t n_right,
                              size_t left_weight, size_t right_weight);
size_t OFRControlWriteAndRouteNormalized(
    std::vector<uint8_t> &tags, PackedControls &controls,
    size_t *rows, size_t words_per_row, size_t n,
    size_t n_left, size_t n_right, size_t position, size_t control_count,
    std::vector<uint8_t> *feature_scratch = NULL);
size_t OFRControlWritePostOrderAndRouteNormalized(
    std::vector<uint8_t> &tags, PackedControls &controls,
    size_t *rows, size_t words_per_row, size_t n,
    size_t n_left, size_t n_right, size_t position, size_t control_count);
} // namespace detail

// Convenience wrapper that preserves tags and materializes both child arrays.
OFRBalanceResult OFRBalance(const std::vector<uint8_t> &tags,
                            size_t n,
                            size_t n_left,
                            size_t n_right);

// Mutate one strided tag view and write its layer controls at position.
size_t OFRBalanceInPlace(std::vector<uint8_t> &tags,
                         PackedControls &controls,
                         size_t base, size_t stride, size_t n,
                         size_t n_left, size_t n_right, size_t position);

size_t OFRControlCount(size_t n, size_t n_left, size_t n_right);

// Compatibility wrapper: copies tags before generating controls.
size_t OFRControlWrite(const std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position);

// Preferred path: the root view is validated once; recursive views use the
// capacities established by Balance and do not allocate child tag arrays.
size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t base, size_t stride, size_t n,
                       size_t n_left, size_t n_right, size_t position);

size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t n, size_t n_left, size_t n_right,
                       size_t position);

// Generate the same DFS tape while applying each gate to machine-word
// membership rows. Tags must already have exact output responsibilities.
size_t OFRControlWriteAndRoute(std::vector<uint8_t> &tags,
                               PackedControls &controls,
                               size_t *rows, size_t words_per_row,
                               size_t n, size_t n_left, size_t n_right,
                               size_t position);

// Balanced power-of-two only: generate the final contiguous-block postorder
// tape directly with public-size residue tiles, and route membership rows.
size_t OFRControlWritePostOrderAndRoute(std::vector<uint8_t> &tags,
                                        PackedControls &controls,
                                        size_t *rows, size_t words_per_row,
                                        size_t n, size_t n_left, size_t n_right,
                                        size_t position);

PackedControls OFRControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right);

// Generate the prepared contiguous-block postorder tape for a balanced
// power-of-two network using tiles selected from public dimensions.
PackedControls OFRControlPostOrder(const std::vector<uint8_t> &tags,
                                         size_t n,
                                         size_t n_left,
                                         size_t n_right);

// Reorder a balanced power-of-two control tape for the original level-order
// OFork network. This conversion depends only on public dimensions.
PackedControls OFRLevelOrderControls(
    const PackedControls &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Offline conversion for balanced power-of-two dimensions. The resulting
// tape finishes each contiguous half before the gates between the halves,
// matching ORCompact's execution order.
PackedControls OFRPostOrderControls(
    const PackedControls &controls,
    size_t n,
    size_t n_left,
    size_t n_right);

// Convert one contiguous DFS control span without copying its source first.
PackedControls OFRPostOrderControls(
    PackedControlView controls,
    size_t n, size_t n_left, size_t n_right);

// Apply controls in place. The first n_left records are the left output;
// the remaining n_right records are the right output.
void OFRApplyInPlace(unsigned char *data,
                     const PackedControls &controls,
                     size_t n,
                     size_t n_left,
                     size_t n_right,
                     size_t block_size);

void OFRApplyLevelOrderedInPlace(
    unsigned char *data,
    const PackedControls &level_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size);

// Apply a tape returned by OFRPostOrderControls. Balanced power-of-two only.
void OFRApplyPostOrderInPlace(
    unsigned char *data,
    const PackedControls &postorder_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size);

// Prepared entry points validate the complete public control span before
// applying it, then consume packed controls without per-gate bounds checks.
void OFRApplyPreparedInPlace(
    unsigned char *data,
    const PackedControls &controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size);

void OFRApplyPreparedInPlace(
    unsigned char *data, PackedControlView controls,
    size_t n, size_t n_left, size_t n_right, size_t block_size);

void OFRApplyPreparedLevelOrderedInPlace(
    unsigned char *data,
    const PackedControls &level_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size);

// The public backend choice changes gate execution only, never tape layout.
void OFRApplyPreparedPostOrderInPlace(
    unsigned char *data,
    const PackedControls &postorder_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size, bool use_four_gate_sse2 = true);

void OFRApplyPreparedPostOrderInPlace(
    unsigned char *data, PackedControlView postorder_controls, size_t n, size_t n_left, size_t n_right,
    size_t block_size, bool use_four_gate_sse2 = true);

// Replay a DFS control span on machine-word membership rows in place.
void OFRApplyWordsInPlace(
    size_t *rows, size_t words_per_row,
    const PackedControls &controls, size_t position,
    size_t n, size_t n_left, size_t n_right);

OFRControlReadResult OFRControlRead(const unsigned char *data,
                                    const PackedControls &controls,
                                    size_t n,
                                    size_t n_left,
                                    size_t n_right,
                                    size_t block_size,
                                    size_t position);

OFRDataResult OFRApply(const unsigned char *data,
                       const PackedControls &controls,
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
