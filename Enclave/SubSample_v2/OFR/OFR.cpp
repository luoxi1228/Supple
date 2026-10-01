#include "OFR.hpp"
#include "helper.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifndef BEFTS_MODE
#include "../../oasm_lib.h"
#endif
#include "FourGateSSE2.hpp"

namespace ofr
{

using detail::AddOverflowSize;
using detail::CtEqualU8;
using detail::CtLessSize;
using detail::CtMaxI64;
using detail::CtMinI64;
using detail::CtSelectU8;
using detail::LeftBit;
using detail::MulOverflowSize;
using detail::RightBit;
using detail::ValidateCapacities;
using detail::ValidateTags;

namespace
{

struct CapacitySplit
{
  size_t n_top;
  size_t n_bottom;
  size_t top_left;
  size_t top_right;
  size_t bottom_left;
  size_t bottom_right;
};

CapacitySplit SplitCapacities(size_t n, size_t n_left, size_t n_right)
{
  const size_t n_top = n / 2 + n % 2;
  const size_t top_left = n_left / 2 + n_left % 2;
  const size_t top_right = n_top - top_left;
  CapacitySplit split = {
      n_top,
      n / 2,
      top_left,
      top_right,
      n_left - top_left,
      n_right - top_right};
  return split;
}

size_t OFRControlCountImpl(size_t n, size_t n_left, size_t n_right)
{
  if (n_left == 0 || n_right == 0)
    return 0;
  if (n >= 2 && (n & (n - 1)) == 0 &&
      n_left == n / 2 && n_right == n / 2)
  {
    size_t depth = 0;
    for (size_t length = n; length > 1; length >>= 1U)
      ++depth;
    size_t count = 0;
    if (MulOverflowSize(n / 2, depth, &count))
      throw std::length_error("OFR control count overflow");
    return count;
  }
  if (n == 2)
    return 1;

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  const size_t top_count = OFRControlCountImpl(
      split.n_top, split.top_left, split.top_right);
  const size_t bottom_count = OFRControlCountImpl(
      split.n_bottom, split.bottom_left, split.bottom_right);

  size_t result = 0;
  if (AddOverflowSize(n / 2, top_count, &result) ||
      AddOverflowSize(result, bottom_count, &result))
    throw std::length_error("OFR control count overflow");
  return result;
}

size_t OFRBalanceInPlaceImpl(uint8_t *tags, PackedControls &controls, uint8_t *scratch,
                             size_t base, size_t stride, size_t n,
                             size_t n_left, size_t n_right, size_t position);

size_t OFRControlWriteImpl(uint8_t *tags, PackedControls &controls, uint8_t *scratch,
                           size_t base, size_t stride, size_t n,
                           size_t n_left, size_t n_right, size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;

  if (n == 2)
  {
    // Exact (1,1) responsibilities make the second tag the two-bit complement
    // of the first: 00/11 -> copy second, 01/10 -> swap, and vice versa.
    const uint8_t x = tags[base];
    controls[position] = static_cast<uint8_t>(OFR_TAG_BOTH ^ x);
    tags[base] = OFR_TAG_LEFT;
    tags[base + stride] = OFR_TAG_RIGHT;
    return position + 1;
  }

  position = OFRBalanceInPlaceImpl(
      tags, controls, scratch, base, stride, n, n_left, n_right, position);

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = OFRControlWriteImpl(tags, controls, scratch, base, stride * 2,
                                 split.n_top,
                                 split.top_left,
                                 split.top_right,
                                 position);
  return OFRControlWriteImpl(tags, controls, scratch, base + stride, stride * 2,
                             split.n_bottom,
                             split.bottom_left,
                             split.bottom_right,
                             position);
}

// The two recursive children occupy the even and odd record lanes. Recurse
// with a doubled stride instead of materializing either child in a vector.
size_t OFRApplyStrided(unsigned char *data,
                       PackedControlView controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size,
                       size_t stride,
                       size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;

  const size_t byte_stride = stride * block_size;
  PackedControlCursor cursor(controls, position);
  if (n == 2)
  {
    detail::ApplyOFork(data, data + byte_stride, block_size, cursor.nextUnchecked());
    return position + 1;
  }

  const size_t gate_count = n / 2;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    unsigned char *first = data + (2 * gate) * byte_stride;
    detail::ApplyOFork(first,
                       first + byte_stride,
                       block_size,
                       cursor.nextUnchecked());
  }
  position += gate_count;

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = OFRApplyStrided(data,
                             controls,
                             split.n_top,
                             split.top_left,
                             split.top_right,
                             block_size,
                             stride * 2,
                             position);
  return OFRApplyStrided(data + byte_stride,
                          controls,
                          split.n_bottom,
                          split.bottom_left,
                          split.bottom_right,
                          block_size,
                          stride * 2,
                          position);
}

size_t OFRApplyWordsStrided(size_t *rows, size_t words_per_row,
                            PackedControlView controls, size_t n,
                            size_t n_left, size_t n_right,
                            size_t stride, size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;

  const size_t word_stride = stride * words_per_row;
  PackedControlCursor cursor(controls, position);
  const size_t gate_count = n / 2;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    size_t *first = rows + (2 * gate) * word_stride;
    size_t *second = first + word_stride;
    const uint8_t control = cursor.nextUnchecked();
    const size_t top_mask = size_t(0) - size_t((control >> 1U) & 1U);
    const size_t bottom_mask = size_t(0) - size_t(control & 1U);
    for (size_t word = 0; word < words_per_row; ++word)
    {
      const size_t x = first[word];
      const size_t y = second[word];
      first[word] = (x & ~top_mask) | (y & top_mask);
      second[word] = (x & ~bottom_mask) | (y & bottom_mask);
    }
  }
  position += gate_count;
  if (n == 2)
    return position;

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = OFRApplyWordsStrided(
      rows, words_per_row, controls, split.n_top, split.top_left,
      split.top_right, stride * 2, position);
  return OFRApplyWordsStrided(
      rows + word_stride, words_per_row, controls, split.n_bottom,
      split.bottom_left, split.bottom_right, stride * 2, position);
}

bool IsBalancedPowerOfTwo(size_t n, size_t n_left, size_t n_right)
{
  return n >= 2 && (n & (n - 1)) == 0 &&
         n_left == n / 2 && n_right == n / 2;
}

size_t WriteLevelOrderControls(const PackedControls &source,
                               PackedControls *destination,
                               size_t n,
                               size_t root_half,
                               size_t depth,
                               size_t stride,
                               size_t residue,
                               size_t position)
{
  const size_t gate_count = n / 2;
  const size_t stage_start = depth * root_half;
  for (size_t gate = 0; gate < gate_count; ++gate)
    (*destination)[stage_start + gate * stride + residue] =
        source[position + gate];
  position += gate_count;
  if (n == 2)
    return position;

  position = WriteLevelOrderControls(source, destination, n / 2,
                                     root_half, depth + 1, stride * 2,
                                     residue, position);
  return WriteLevelOrderControls(source, destination, n / 2,
                                 root_half, depth + 1, stride * 2,
                                 residue + stride, position);
}

// A DFS control node is identified by its public stride and residue. Record
// its first gate once; a gate at block k is then at start[stride + residue] + k.
size_t BuildDfsControlStarts(size_t length,
                             size_t stride,
                             size_t residue,
                             size_t position,
                             std::vector<size_t> *starts)
{
  (*starts)[stride + residue] = position;
  position += length / 2;
  if (length > 2)
  {
    position = BuildDfsControlStarts(length / 2, stride * 2, residue,
                                     position, starts);
    position = BuildDfsControlStarts(length / 2, stride * 2,
                                     residue + stride, position, starts);
  }
  return position;
}

// Emit the contiguous-block postorder directly from the DFS tape. The public
// gate geometry determines every source position, so no level-order tape is
// materialized.
size_t WritePostOrderFromDfs(PackedControlView source,
                             const std::vector<size_t> &starts,
                             PackedControls *destination,
                             size_t base,
                             size_t length,
                             size_t position)
{
  const size_t half = length / 2;
  if (length > 2)
  {
    position = WritePostOrderFromDfs(source, starts, destination, base,
                                     half, position);
    position = WritePostOrderFromDfs(source, starts, destination, base + half,
                                     half, position);
  }
  const size_t block = base / length;
  for (size_t offset = 0; offset < half; ++offset)
    (*destination)[position + offset] = source[starts[half + offset] + block];
  return position + half;
}

struct GenericGate
{
  void operator()(unsigned char *first,
                  unsigned char *second,
                  size_t block_size,
                  uint8_t control) const
  {
    detail::ApplyOFork(first, second, block_size, control);
  }
};

template <typename Gate>
__attribute__((always_inline)) inline
void ApplyFourIndependent(unsigned char *first, unsigned char *second,
                           size_t block_size, uint8_t controls,
                           const Gate &apply_gate)
{
  apply_gate(first, second, block_size, static_cast<uint8_t>(controls & 3U));
  apply_gate(first + block_size, second + block_size, block_size,
               static_cast<uint8_t>((controls >> 2U) & 3U));
  apply_gate(first + 2 * block_size, second + 2 * block_size, block_size,
               static_cast<uint8_t>((controls >> 4U) & 3U));
  apply_gate(first + 3 * block_size, second + 3 * block_size, block_size,
               static_cast<uint8_t>(controls >> 6U));
}

#ifndef BEFTS_MODE
template <OFork_Style style> struct StyledGate;
#if defined(__SSE2__) && defined(__x86_64__) && !defined(__ILP32__)
template <>
__attribute__((always_inline)) inline
void ApplyFourIndependent<StyledGate<OFORK_8> >(
    unsigned char *first, unsigned char *second, size_t block_size,
    uint8_t controls, const StyledGate<OFORK_8> &apply_gate);
template <>
__attribute__((always_inline)) inline
void ApplyFourIndependent<StyledGate<OFORK_16> >(
    unsigned char *first, unsigned char *second, size_t block_size,
    uint8_t controls, const StyledGate<OFORK_16> &apply_gate);
#endif
#endif

template <typename Gate>
void ApplyContiguousGateRun(unsigned char *first, unsigned char *second,
                            size_t block_size, size_t count,
                            PackedControlCursor &cursor, const Gate &apply_gate)
{
  size_t gate = 0;
  for (; count - gate >= 4; gate += 4)
  {
    const uint8_t controls = cursor.nextFourUnchecked();
    ApplyFourIndependent(first, second, block_size, controls, apply_gate);
    first += 4 * block_size;
    second += 4 * block_size;
  }
  for (; gate < count; ++gate)
  {
    apply_gate(first, second, block_size, cursor.nextUnchecked());
    first += block_size;
    second += block_size;
  }
}

template <typename Gate>
void ApplyLevelOrderedGates(unsigned char *data,
                            PackedControlView controls,
                            size_t n,
                            size_t block_size,
                            const Gate &apply_gate)
{
  PackedControlCursor cursor(controls);
  for (size_t stride = 1; stride < n; stride *= 2)
  {
    for (size_t base = 0; base < n; base += stride * 2)
    {
      unsigned char *first = data + base * block_size;
      unsigned char *second = first + stride * block_size;
      ApplyContiguousGateRun(first, second, block_size, stride, cursor, apply_gate);
    }
  }
}

template <typename Gate>
__attribute__((always_inline)) inline
void ApplyPostOrderFourFixed(unsigned char *data, size_t block_size,
                            PackedControlCursor &cursor,
                            const Gate &apply_gate)
{
  const uint8_t controls = cursor.nextFourUnchecked();
  apply_gate(data, data + block_size, block_size,
             static_cast<uint8_t>(controls & 3U));
  apply_gate(data + 2 * block_size, data + 3 * block_size, block_size,
             static_cast<uint8_t>((controls >> 2U) & 3U));
  apply_gate(data, data + 2 * block_size, block_size,
             static_cast<uint8_t>((controls >> 4U) & 3U));
  apply_gate(data + block_size, data + 3 * block_size, block_size,
             static_cast<uint8_t>(controls >> 6U));
}

template <typename Gate>
__attribute__((always_inline)) inline
void ApplyPostOrderEightFixed(unsigned char *data, size_t block_size,
                             PackedControlCursor &cursor,
                             const Gate &apply_gate)
{
  ApplyPostOrderFourFixed(data, block_size, cursor, apply_gate);
  ApplyPostOrderFourFixed(data + 4 * block_size, block_size, cursor, apply_gate);
  const uint8_t controls = cursor.nextFourUnchecked();
  ApplyFourIndependent(data, data + 4 * block_size, block_size, controls, apply_gate);
}

template <typename Gate>
size_t ApplyPostOrderGates(unsigned char *data,
                           PackedControlCursor &cursor,
                           size_t n,
                           size_t block_size,
                           size_t position,
                           const Gate &apply_gate)
{
  if (n == 8)
  {
    ApplyPostOrderEightFixed(data, block_size, cursor, apply_gate);
    return cursor.position();
  }
  if (n == 4)
  {
    ApplyPostOrderFourFixed(data, block_size, cursor, apply_gate);
    return cursor.position();
  }
  if (n == 2)
  {
    apply_gate(data, data + block_size, block_size, cursor.nextUnchecked());
    return cursor.position();
  }
  const size_t half = n / 2;
  if (n > 2)
  {
    position = ApplyPostOrderGates(data, cursor, half, block_size,
                                   position, apply_gate);
    position = ApplyPostOrderGates(data + half * block_size, cursor,
                                   half, block_size, position, apply_gate);
  }
  ApplyContiguousGateRun(data, data + half * block_size,
                         block_size, half, cursor, apply_gate);
  return cursor.position();
}

#ifndef BEFTS_MODE
template <OFork_Style style>
struct StyledGate
{
  void operator()(unsigned char *first,
                  unsigned char *second,
                  size_t block_size,
                  uint8_t control) const
  {
    ofork_buffer<style>(first, second, static_cast<uint32_t>(block_size),
                        static_cast<uint8_t>((control >> 1U) & 1U),
                        static_cast<uint8_t>(control & 1U));
  }
};

#if defined(__SSE2__) && defined(__x86_64__) && !defined(__ILP32__)
template <>
__attribute__((always_inline)) inline
void ApplyFourIndependent<StyledGate<OFORK_8> >(
    unsigned char *first, unsigned char *second, size_t block_size,
    uint8_t controls, const StyledGate<OFORK_8> &apply_gate)
{
  (void)block_size;
  (void)apply_gate;
  detail::fourgate_sse2::ApplyFour8(first, second, controls,
#ifdef COUNT_OSWAPS
                                 &OSWAP_COUNTER
#else
                                 NULL
#endif
  );
}

template <>
__attribute__((always_inline)) inline
void ApplyFourIndependent<StyledGate<OFORK_16> >(
    unsigned char *first, unsigned char *second, size_t block_size,
    uint8_t controls, const StyledGate<OFORK_16> &apply_gate)
{
  (void)block_size;
  (void)apply_gate;
  detail::fourgate_sse2::ApplyFour16(first, second, controls,
#ifdef COUNT_OSWAPS
                                  &OSWAP_COUNTER
#else
                                  NULL
#endif
  );
}
#endif

// Keep the hot loop equivalent to the original OFork implementation: choose
// the assembly specialization once and decode the packed tape sequentially.
template <OFork_Style style>
void ApplyLevelOrderedWithStyle(unsigned char *data,
                                PackedControlView controls,
                                size_t n,
                                size_t block_size)
{
  ApplyLevelOrderedGates(data, controls, n, block_size, StyledGate<style>());
}
#endif

} // namespace

std::array<uint8_t, 10> OFRPairMask(uint8_t x, uint8_t y)
{
  const uint8_t x_zero = CtEqualU8(x, OFR_TAG_ZERO);
  const uint8_t x_left = CtEqualU8(x, OFR_TAG_LEFT);
  const uint8_t x_right = CtEqualU8(x, OFR_TAG_RIGHT);
  const uint8_t x_both = CtEqualU8(x, OFR_TAG_BOTH);
  const uint8_t y_zero = CtEqualU8(y, OFR_TAG_ZERO);
  const uint8_t y_left = CtEqualU8(y, OFR_TAG_LEFT);
  const uint8_t y_right = CtEqualU8(y, OFR_TAG_RIGHT);
  const uint8_t y_both = CtEqualU8(y, OFR_TAG_BOTH);

  std::array<uint8_t, 10> pair = {{
      static_cast<uint8_t>(x_zero & y_zero),
      static_cast<uint8_t>((x_zero & y_left) | (x_left & y_zero)),
      static_cast<uint8_t>((x_zero & y_right) | (x_right & y_zero)),
      static_cast<uint8_t>((x_zero & y_both) | (x_both & y_zero)),
      static_cast<uint8_t>(x_left & y_left),
      static_cast<uint8_t>((x_left & y_right) | (x_right & y_left)),
      static_cast<uint8_t>((x_left & y_both) | (x_both & y_left)),
      static_cast<uint8_t>(x_right & y_right),
      static_cast<uint8_t>((x_right & y_both) | (x_both & y_right)),
      static_cast<uint8_t>(x_both & y_both)}};
  return pair;
}

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t top_tag,
                     uint8_t bottom_tag)
{
  if (((x | y | top_tag | bottom_tag) >> 2U) != 0)
    throw std::invalid_argument("Invalid OFork routing tag");

  const uint8_t straight = static_cast<uint8_t>(
      CtEqualU8(top_tag, x) & CtEqualU8(bottom_tag, y));
  const uint8_t cross = static_cast<uint8_t>(
      CtEqualU8(top_tag, y) & CtEqualU8(bottom_tag, x));
  const uint8_t copy_x = static_cast<uint8_t>(
      CtEqualU8(x, OFR_TAG_BOTH) & CtEqualU8(y, OFR_TAG_ZERO));
  const uint8_t copy_y = static_cast<uint8_t>(
      CtEqualU8(x, OFR_TAG_ZERO) & CtEqualU8(y, OFR_TAG_BOTH));
  const uint8_t copy_pair = static_cast<uint8_t>(copy_x | copy_y);
  const uint8_t left_right = static_cast<uint8_t>(
      CtEqualU8(top_tag, OFR_TAG_LEFT) &
      CtEqualU8(bottom_tag, OFR_TAG_RIGHT));
  const uint8_t right_left = static_cast<uint8_t>(
      CtEqualU8(top_tag, OFR_TAG_RIGHT) &
      CtEqualU8(bottom_tag, OFR_TAG_LEFT));
  const uint8_t fork = static_cast<uint8_t>(
      copy_pair & (left_right | right_left));

  const uint8_t use_cross = static_cast<uint8_t>(cross & (straight ^ 1U));
  const uint8_t use_fork = static_cast<uint8_t>(
      fork & (straight ^ 1U) & (cross ^ 1U));
  const uint8_t fork_control = static_cast<uint8_t>((copy_y << 1U) | copy_y);

  uint8_t control = OFR_STRAIGHT;
  control = CtSelectU8(control, OFR_SWAP, use_cross);
  control = CtSelectU8(control, fork_control, use_fork);
  return control;
}

void OFRNormalizeInPlace(std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (tags.size() != n)
    throw std::invalid_argument("OFR tag array length mismatch");

  size_t left_weight = 0;
  size_t right_weight = 0;
  uint8_t invalid = 0;
  for (size_t i = 0; i < n; ++i)
  {
    invalid = static_cast<uint8_t>(invalid | (tags[i] >> 2U));
    left_weight += LeftBit(tags[i]);
    right_weight += RightBit(tags[i]);
  }
  if (invalid != 0 || left_weight > n_left || right_weight > n_right)
    throw std::invalid_argument("OFR routing tags do not match output capacities");

  detail::OFRNormalizeOwnedInPlace(tags, n, n_left, n_right, left_weight, right_weight);
}

void detail::OFRNormalizeOwnedInPlace(std::vector<uint8_t> &tags,
                                      size_t n, size_t n_left, size_t n_right,
                                      size_t left_weight, size_t right_weight)
{
  ValidateCapacities(n, n_left, n_right);
  if (tags.size() != n || left_weight > n_left || right_weight > n_right)
    throw std::invalid_argument("OFR owned labels do not match capacities");
  const size_t assign_left = n_left - left_weight;
  const size_t assign_right = n_right - right_weight;
  size_t zero_rank = 0;
  for (size_t i = 0; i < n; ++i)
  {
    // Owned labels are in 0..3. The arithmetic zero test and disjoint
    // responsibility additions avoid redundant conditional selections.
    const uint8_t is_zero = static_cast<uint8_t>(((tags[i] + 3U) >> 2U) ^ 1U);
    const uint8_t before_left = CtLessSize(zero_rank, assign_left);
    const uint8_t use_left = static_cast<uint8_t>(
        is_zero & before_left);
    const uint8_t after_left = static_cast<uint8_t>(
        before_left ^ 1U);
    const uint8_t before_end = CtLessSize(
        zero_rank, assign_left + assign_right);
    const uint8_t use_right = static_cast<uint8_t>(
        is_zero & after_left & before_end);

    tags[i] = static_cast<uint8_t>(tags[i] | (use_left << 1U) | use_right);
    zero_rank += is_zero;
  }

}

std::vector<uint8_t> OFRNormalize(const std::vector<uint8_t> &tags,
                                  size_t n, size_t n_left, size_t n_right)
{
  std::vector<uint8_t> normalized(tags);
  OFRNormalizeInPlace(normalized, n, n_left, n_right);
  return normalized;
}

namespace
{

// Bits 0..1 are the fixed right/left responsibilities, bits 2..3 the
// variable right/left responsibilities, and bit 4 the diagonal (01,10).
uint8_t ClassifyPairBits(uint8_t x, uint8_t y)
{
  const uint8_t different = static_cast<uint8_t>(x ^ y);
  const uint8_t opposite = static_cast<uint8_t>(
      LeftBit(different) & RightBit(different));
  const uint8_t x_singleton = static_cast<uint8_t>(LeftBit(x) ^ RightBit(x));
  const uint8_t diagonal = static_cast<uint8_t>(opposite & x_singleton);
  const uint8_t variable = static_cast<uint8_t>(
      different & static_cast<uint8_t>(0U - (diagonal ^ 1U)));
  return static_cast<uint8_t>(
      (x & y) | (variable << 2U) | (diagonal << 4U));
}

uint8_t GateControlForBalancedPair(uint8_t x, uint8_t y,
                                   uint8_t top_tag)
{
  // The total responsibility bits are conserved, so once the top tag matches
  // one input, the bottom tag must match the other. The (00,11) pair can also
  // split into (10,01), in which case both lanes copy the 11 input.
  // Testing straight first preserves the canonical encoding for x == y.
  const uint8_t straight = CtEqualU8(top_tag, x);
  const uint8_t cross = static_cast<uint8_t>(
      CtEqualU8(top_tag, y) & (straight ^ 1U));
  // A top tag distinct from both inputs is possible only when (00,11)
  // splits into (10,01). The 11 input is y exactly when its left bit is set.
  const uint8_t fork_y = static_cast<uint8_t>(
      LeftBit(y) & (straight ^ 1U) & (cross ^ 1U));
  const uint8_t top_from_y = static_cast<uint8_t>(cross | fork_y);
  const uint8_t bottom_from_y = static_cast<uint8_t>(straight | fork_y);
  return static_cast<uint8_t>((top_from_y << 1U) | bottom_from_y);
}

inline void RouteMembershipGate(size_t *first, size_t *second,
                                 size_t words_per_row, uint8_t control)
{
  const size_t top_mask = size_t(0) - size_t((control >> 1U) & 1U);
  const size_t bottom_mask = size_t(0) - size_t(control & 1U);
  for (size_t word = 0; word < words_per_row; ++word)
  {
    const size_t x = first[word];
    const size_t y = second[word];
    first[word] = (x & ~top_mask) | (y & top_mask);
    second[word] = (x & ~bottom_mask) | (y & bottom_mask);
  }
}

template <bool PostOrder, bool RouteWords = false>
size_t OFRBalanceLayer(uint8_t *tags, PackedControls &controls,
                       uint8_t *scratch, const size_t *postorder_starts,
                       size_t root_n, size_t base, size_t stride, size_t n,
                       size_t n_left, size_t n_right, size_t position,
                       size_t *rows = NULL, size_t words_per_row = 0)
{
  const size_t gate_count = n / 2;
  uint8_t *features = scratch;
  const size_t node_base = PostOrder ? root_n / (2 * stride) : 0;

  size_t diagonal_count = 0, fixed_left_count = 0;
  size_t fixed_right_count = 0;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t first = base + (2 * gate) * stride;
    const uint8_t pair = ClassifyPairBits(
        tags[first], tags[first + stride]);
    diagonal_count += (pair >> 4U) & 1U;
    fixed_left_count += (pair >> 1U) & 1U;
    fixed_right_count += pair & 1U;
    // Five-bit classification features are scratch, separate from the
    // two-bit output tape; all recursive layers reuse this root allocation.
    features[gate] = pair;
  }

  const int64_t diagonal = static_cast<int64_t>(diagonal_count);
  const int64_t fixed_left = static_cast<int64_t>(fixed_left_count);
  const int64_t fixed_right = static_cast<int64_t>(fixed_right_count);
  int64_t quota_diagonal = 0;
  int64_t quota_left = 0;
  int64_t quota_right = 0;
  if ((n & 3U) == 0 && n_left == gate_count &&
      n_right == gate_count)
  {
    // With balanced capacities, each side has n/2 total bits. Consequently
    // 2*fixed_side + diagonal <= n/2, so floor(diagonal/2) satisfies both
    // quota bounds. This public network shape needs no min/max clipping.
    const int64_t target = static_cast<int64_t>(n / 4);
    quota_diagonal = diagonal / 2;
    quota_left = target - fixed_left - quota_diagonal;
    quota_right = target - fixed_right - diagonal + quota_diagonal;
  }
  else
  {
    const CapacitySplit split = SplitCapacities(n, n_left, n_right);
    const size_t last = base + (n - 1) * stride;
    const int64_t e_left = (n & 1U) != 0 ? LeftBit(tags[last]) : 0;
    const int64_t e_right = (n & 1U) != 0 ? RightBit(tags[last]) : 0;
    const int64_t target_left = static_cast<int64_t>(split.top_left) - e_left;
    const int64_t target_right = static_cast<int64_t>(split.top_right) - e_right;
    // Each fixed pair contributes two responsibility bits and each diagonal
    // or variable pair contributes one. The exact capacities supply totals.
    const int64_t variable_left = static_cast<int64_t>(n_left) - e_left -
                                  2 * fixed_left - diagonal;
    const int64_t variable_right = static_cast<int64_t>(n_right) - e_right -
                                   2 * fixed_right - diagonal;

    int64_t quota_diagonal_min = CtMaxI64(
        0, target_left - fixed_left - variable_left);
    quota_diagonal_min = CtMaxI64(
        quota_diagonal_min, fixed_right + diagonal - target_right);
    int64_t quota_diagonal_max = CtMinI64(
        diagonal, target_left - fixed_left);
    quota_diagonal_max = CtMinI64(
        quota_diagonal_max,
        fixed_right + diagonal + variable_right - target_right);
    quota_diagonal = CtMinI64(
        quota_diagonal_max,
        CtMaxI64(quota_diagonal_min, diagonal / 2));
    quota_left = target_left - fixed_left - quota_diagonal;
    quota_right = target_right - fixed_right - diagonal + quota_diagonal;
  }

  size_t rank_diagonal = 0;
  size_t rank_left = 0;
  size_t rank_right = 0;

  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t first = base + (2 * gate) * stride;
    const uint8_t x = tags[first];
    const uint8_t y = tags[first + stride];
    const uint8_t pair = features[gate];
    const uint8_t diagonal_type = static_cast<uint8_t>((pair >> 4U) & 1U);
    const uint8_t left_variable_type = static_cast<uint8_t>((pair >> 3U) & 1U);
    const uint8_t right_variable_type = static_cast<uint8_t>((pair >> 2U) & 1U);

    const uint8_t use_diagonal = static_cast<uint8_t>(
        diagonal_type & CtLessSize(
            rank_diagonal, static_cast<size_t>(quota_diagonal)));
    const uint8_t use_left = static_cast<uint8_t>(
        left_variable_type & CtLessSize(
            rank_left, static_cast<size_t>(quota_left)));
    const uint8_t use_right = static_cast<uint8_t>(
        right_variable_type & CtLessSize(
            rank_right, static_cast<size_t>(quota_right)));
    const uint8_t top_left_fixed = static_cast<uint8_t>((pair >> 1U) & 1U);
    const uint8_t top_right_fixed = static_cast<uint8_t>(pair & 1U);
    const uint8_t top_left = static_cast<uint8_t>(
        top_left_fixed | use_diagonal | use_left);
    const uint8_t top_right = static_cast<uint8_t>(
        top_right_fixed |
        (diagonal_type & (use_diagonal ^ 1U)) |
        use_right);
    const uint8_t top_tag = static_cast<uint8_t>(
        (top_left << 1U) | top_right);
    // For each responsibility bit, bottom = x + y - top equals x ^ y ^ top
    // because the conserved sum is 0, 1, or 2 and top is one bit.
    const uint8_t bottom_tag = static_cast<uint8_t>(x ^ y ^ top_tag);

    const uint8_t control = GateControlForBalancedPair(x, y, top_tag);
    if (PostOrder)
      controls[postorder_starts[node_base + gate] + base] = control;
    else
      controls[position + gate] = control;
    if (RouteWords)
      RouteMembershipGate(rows + first * words_per_row,
                           rows + (first + stride) * words_per_row,
                           words_per_row, control);
    tags[first] = top_tag;
    tags[first + stride] = bottom_tag;
    rank_diagonal += diagonal_type;
    rank_left += left_variable_type;
    rank_right += right_variable_type;
  }

  return position + gate_count;
}

size_t OFRBalanceInPlaceImpl(uint8_t *tags, PackedControls &controls, uint8_t *scratch,
                             size_t base, size_t stride, size_t n,
                             size_t n_left, size_t n_right, size_t position)
{
  return OFRBalanceLayer<false>(tags, controls, scratch, NULL, 0,
                                base, stride, n, n_left, n_right, position);
}

size_t OFRControlWriteAndRouteImpl(
    uint8_t *tags, PackedControls &controls, uint8_t *scratch,
    size_t *rows, size_t words_per_row,
    size_t base, size_t stride, size_t n,
    size_t n_left, size_t n_right, size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;
  if (n == 2)
  {
    const uint8_t control = static_cast<uint8_t>(OFR_TAG_BOTH ^ tags[base]);
    controls[position] = control;
    RouteMembershipGate(rows + base * words_per_row,
                         rows + (base + stride) * words_per_row,
                         words_per_row, control);
    tags[base] = OFR_TAG_LEFT;
    tags[base + stride] = OFR_TAG_RIGHT;
    return position + 1;
  }

  position = OFRBalanceLayer<false, true>(
      tags, controls, scratch, NULL, 0, base, stride, n,
      n_left, n_right, position, rows, words_per_row);
  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = OFRControlWriteAndRouteImpl(
      tags, controls, scratch, rows, words_per_row, base, stride * 2,
      split.n_top, split.top_left, split.top_right, position);
  return OFRControlWriteAndRouteImpl(
      tags, controls, scratch, rows, words_per_row, base + stride, stride * 2,
      split.n_bottom, split.bottom_left, split.bottom_right, position);
}

struct ResidueBalanceState
{
  size_t diagonal, fixed_left, fixed_right;
  size_t quota_diagonal, quota_left, quota_right;
  size_t rank_diagonal, rank_left, rank_right;
};

// Private SSE2 types avoid host intrinsic headers: SGX builds use -nostdinc.
typedef int OfRVec32 __attribute__((vector_size(16)));
typedef short OfRVec16 __attribute__((vector_size(16)));
typedef char OfRVec8 __attribute__((vector_size(16)));
typedef long long OfRVec64 __attribute__((vector_size(16)));
typedef unsigned short OfRUVec16 __attribute__((vector_size(16)));
typedef unsigned char OfRUVec8 __attribute__((vector_size(16)));

inline OfRVec32 LoadVec32(const void *source)
{
  OfRVec32 result;
  std::memcpy(&result, source, sizeof(result));
  return result;
}

inline void StoreVec32(void *destination, OfRVec32 value)
{
  std::memcpy(destination, &value, sizeof(value));
}

inline OfRVec32 TagLanes4(const uint8_t *tags)
{
  const OfRVec8 zero8 = {};
  const OfRVec16 zero16 = {};
  OfRVec8 bytes = {};
  std::memcpy(&bytes, tags, 4);
  const OfRVec16 words = (OfRVec16)__builtin_ia32_punpcklbw128(bytes, zero8);
  return (OfRVec32)__builtin_ia32_punpcklwd128(words, zero16);
}

inline uint32_t PackTagLanes4(OfRVec32 lanes)
{
  const OfRVec32 zero32 = {};
  const OfRVec16 zero16 = {};
  const OfRVec16 words = __builtin_ia32_packssdw128(lanes, zero32);
  const OfRVec8 bytes = __builtin_ia32_packuswb128(words, zero16);
  uint32_t result;
  std::memcpy(&result, &bytes, sizeof(result));
  return result;
}

inline OfRVec32 PairLanes4(OfRVec32 x, OfRVec32 y)
{
  const OfRVec32 one = {1, 1, 1, 1};
  const OfRVec32 different = x ^ y;
  const OfRVec32 diagonal = ((different >> 1) & different) &
                           ((x >> 1) ^ x) & one;
  const OfRVec32 variable = different & (diagonal - one);
  return (x & y) | (variable << 2) | (diagonal << 4);
}

struct alignas(16) ResidueBalance32
{
  uint32_t diagonal[256], fixed_left[256], fixed_right[256];
  uint32_t quota_diagonal[256], quota_left[256], quota_right[256];
  uint32_t rank_diagonal[256], rank_left[256], rank_right[256];
};

template <bool RouteWords, bool OneWord>
void OFRPostOrderTile32(uint8_t *tags, PackedControlMutableView controls,
                       size_t *rows, size_t words_per_row, size_t stride,
                       size_t depth, size_t length, size_t residue, size_t tile)
{
  const OfRVec32 zero = {};
  const OfRVec32 one = {1, 1, 1, 1};
  const OfRVec32 two = {2, 2, 2, 2};
  const OfRVec32 three = {3, 3, 3, 3};
  ResidueBalance32 states;
  const size_t gate_count = length / 2;
  if (length > 2)
  {
    std::fill(states.diagonal, states.diagonal + tile, 0);
    std::fill(states.fixed_left, states.fixed_left + tile, 0);
    std::fill(states.fixed_right, states.fixed_right + tile, 0);
    std::fill(states.rank_diagonal, states.rank_diagonal + tile, 0);
    std::fill(states.rank_left, states.rank_left + tile, 0);
    std::fill(states.rank_right, states.rank_right + tile, 0);
    for (size_t gate = 0; gate < gate_count; ++gate)
    {
      const size_t first = 2 * gate * stride + residue;
      for (size_t r = 0; r < tile; r += 4)
      {
        const OfRVec32 x = TagLanes4(tags + first + r);
        const OfRVec32 y = TagLanes4(tags + first + stride + r);
        const OfRVec32 fixed = x & y;
        const OfRVec32 different = x ^ y;
        const OfRVec32 diagonal = ((different >> 1) & different) & ((x >> 1) ^ x) & one;
        StoreVec32(states.diagonal + r,
                    LoadVec32(states.diagonal + r) + diagonal);
        StoreVec32(states.fixed_left + r,
                    LoadVec32(states.fixed_left + r) + ((fixed >> 1) & one));
        StoreVec32(states.fixed_right + r,
                    LoadVec32(states.fixed_right + r) + (fixed & one));
      }
    }
    const int target = static_cast<int>(length / 4);
    const OfRVec32 targets = {target, target, target, target};
    for (size_t r = 0; r < tile; r += 4)
    {
      const OfRVec32 diagonal = LoadVec32(states.diagonal + r);
      const OfRVec32 quota_diagonal = diagonal >> 1;
      StoreVec32(states.quota_diagonal + r, quota_diagonal);
      StoreVec32(states.quota_left + r,
                  targets - LoadVec32(states.fixed_left + r) - quota_diagonal);
      StoreVec32(states.quota_right + r,
                  targets - LoadVec32(states.fixed_right + r) -
                  (diagonal - quota_diagonal));
    }
  }

  size_t layer_start = stride * depth;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t first = 2 * gate * stride + residue;
    for (size_t r = 0; r < tile; r += 4)
    {
      const OfRVec32 x = TagLanes4(tags + first + r);
      const OfRVec32 y = TagLanes4(tags + first + stride + r);
      OfRVec32 top_tag = two;
      OfRVec32 bottom_tag = one;
      OfRVec32 control;
      if (length == 2)
        control = three ^ x;
      else
      {
        const OfRVec32 fixed = x & y;
        const OfRVec32 different = x ^ y;
        const OfRVec32 diagonal = ((different >> 1) & different) &
                                  ((x >> 1) ^ x) & one;
        const OfRVec32 variable = different & (diagonal - one);
        const OfRVec32 left_type = (variable >> 1) & one;
        const OfRVec32 right_type = variable & one;
        const OfRVec32 rank_diagonal = LoadVec32(states.rank_diagonal + r);
        const OfRVec32 rank_left = LoadVec32(states.rank_left + r);
        const OfRVec32 rank_right = LoadVec32(states.rank_right + r);
        const OfRVec32 use_diagonal = diagonal &
            (LoadVec32(states.quota_diagonal + r) > rank_diagonal);
        const OfRVec32 use_left = left_type &
            (LoadVec32(states.quota_left + r) > rank_left);
        const OfRVec32 use_right = right_type &
            (LoadVec32(states.quota_right + r) > rank_right);
        const OfRVec32 top_left = ((fixed >> 1) & one) | use_diagonal | use_left;
        const OfRVec32 top_right = (fixed & one) |
                                   (diagonal & (use_diagonal ^ one)) | use_right;
        top_tag = (top_left << 1) | top_right;
        bottom_tag = x ^ y ^ top_tag;
        const OfRVec32 straight = top_tag == x;
        const OfRVec32 cross = (top_tag == y) & ~straight;
        const OfRVec32 fork_y = (y >> 1) & one & ~(straight | cross);
        control = (((cross & one) | fork_y) << 1) | (straight & one) | fork_y;
        StoreVec32(states.rank_diagonal + r, rank_diagonal + diagonal);
        StoreVec32(states.rank_left + r, rank_left + left_type);
        StoreVec32(states.rank_right + r, rank_right + right_type);
      }

      const uint32_t gate_bytes = PackTagLanes4(control);
      const uint8_t encoded = static_cast<uint8_t>(
          gate_bytes | (gate_bytes >> 6U) | (gate_bytes >> 12U) | (gate_bytes >> 18U));
      controls.set_four_encoded(layer_start + residue + r, encoded);
      if (RouteWords)
      {
        if (OneWord)
        {
          const OfRVec32 top_masks = zero - ((control >> 1) & one);
          const OfRVec32 bottom_masks = zero - (control & one);
          for (size_t half = 0; half < 2; ++half)
          {
            const OfRVec32 top_mask = half == 0
                ? __builtin_ia32_punpckldq128(top_masks, top_masks)
                : __builtin_ia32_punpckhdq128(top_masks, top_masks);
            const OfRVec32 bottom_mask = half == 0
                ? __builtin_ia32_punpckldq128(bottom_masks, bottom_masks)
                : __builtin_ia32_punpckhdq128(bottom_masks, bottom_masks);
            size_t *a = rows + first + r + 2 * half;
            size_t *b = a + stride;
            const OfRVec32 row_x = LoadVec32(a);
            const OfRVec32 row_y = LoadVec32(b);
            const OfRVec32 different = row_x ^ row_y;
            StoreVec32(a, row_x ^ (different & top_mask));
            StoreVec32(b, row_x ^ (different & bottom_mask));
          }
        }
        else
        {
          alignas(16) uint32_t gate_controls[4];
          StoreVec32(gate_controls, control);
          for (size_t lane = 0; lane < 4; ++lane)
            RouteMembershipGate(rows + (first + r + lane) * words_per_row,
                                 rows + (first + stride + r + lane) * words_per_row,
                                 words_per_row, static_cast<uint8_t>(gate_controls[lane]));
        }
      }
      const uint32_t top_bytes = PackTagLanes4(top_tag);
      const uint32_t bottom_bytes = PackTagLanes4(bottom_tag);
      std::memcpy(tags + first + r, &top_bytes, 4);
      std::memcpy(tags + first + stride + r, &bottom_bytes, 4);
    }
    if (gate + 1 < gate_count)
    {
      const size_t carry = static_cast<size_t>(__builtin_ctzll(
          static_cast<unsigned long long>(gate + 1)));
      layer_start += stride * (depth + 1 + (size_t(2) << carry) - 2);
    }
  }
}


inline OfRVec16 LoadVec16(const void *source)
{
  OfRVec16 result;
  std::memcpy(&result, source, sizeof(result));
  return result;
}

inline void StoreVec16(void *destination, OfRVec16 value)
{
  std::memcpy(destination, &value, sizeof(value));
}

inline OfRVec16 TagLanes8(const uint8_t *tags)
{
  const OfRVec8 zero = {};
  OfRVec8 bytes = {};
  std::memcpy(&bytes, tags, 8);
  return (OfRVec16)__builtin_ia32_punpcklbw128(bytes, zero);
}

inline uint64_t PackTagLanes8(OfRVec16 lanes)
{
  const OfRVec16 zero = {};
  const OfRVec8 bytes = __builtin_ia32_packuswb128(lanes, zero);
  uint64_t result;
  std::memcpy(&result, &bytes, sizeof(result));
  return result;
}

inline uint16_t PackControlLanes8(OfRVec16 controls)
{
  const OfRVec16 weights = {1, 4, 16, 64, 1, 4, 16, 64};
  const OfRVec32 pairs = __builtin_ia32_pmaddwd128(controls, weights);
  const OfRVec32 combined = pairs + __builtin_ia32_pshufd(pairs, 0xb1);
  const uint32_t bytes = PackTagLanes4(combined);
  return static_cast<uint16_t>((bytes & 255U) | ((bytes >> 8U) & 65280U));
}

inline OfRVec16 PairLanes8(OfRVec16 x, OfRVec16 y)
{
  const OfRVec16 one = {1, 1, 1, 1, 1, 1, 1, 1};
  const OfRVec16 different = x ^ y;
  const OfRVec16 diagonal = ((different >> 1) & different) &
                           ((x >> 1) ^ x) & one;
  const OfRVec16 variable = different & (diagonal - one);
  return (x & y) | (variable << 2) | (diagonal << 4);
}

struct alignas(16) ResidueBalance16
{
  uint16_t diagonal[256], fixed_left[256], fixed_right[256];
  uint16_t quota_diagonal[256], quota_left[256], quota_right[256];
  uint16_t rank_diagonal[256], rank_left[256], rank_right[256];
};

template <bool RouteWords, bool OneWord>
void OFRPostOrderTile16(uint8_t *tags, PackedControlMutableView controls,
                       size_t *rows, size_t words_per_row, size_t stride,
                       size_t depth, size_t length, size_t residue, size_t tile)
{
  const OfRVec16 zero = {};
  const OfRVec16 one = {1, 1, 1, 1, 1, 1, 1, 1};
  const OfRVec16 two = {2, 2, 2, 2, 2, 2, 2, 2};
  const OfRVec16 three = {3, 3, 3, 3, 3, 3, 3, 3};
  ResidueBalance16 states;
  const size_t gate_count = length / 2;
  if (length > 2)
  {
    std::fill(states.diagonal, states.diagonal + tile, 0);
    std::fill(states.fixed_left, states.fixed_left + tile, 0);
    std::fill(states.fixed_right, states.fixed_right + tile, 0);
    std::fill(states.rank_diagonal, states.rank_diagonal + tile, 0);
    std::fill(states.rank_left, states.rank_left + tile, 0);
    std::fill(states.rank_right, states.rank_right + tile, 0);
    for (size_t gate = 0; gate < gate_count; ++gate)
    {
      const size_t first = 2 * gate * stride + residue;
      for (size_t r = 0; r < tile; r += 8)
      {
        const OfRVec16 x = TagLanes8(tags + first + r);
        const OfRVec16 y = TagLanes8(tags + first + stride + r);
        const OfRVec16 fixed = x & y;
        const OfRVec16 different = x ^ y;
        const OfRVec16 diagonal = ((different >> 1) & different) & ((x >> 1) ^ x) & one;
        StoreVec16(states.diagonal + r,
                    OfRVec16((OfRUVec16)LoadVec16(states.diagonal + r) + (OfRUVec16)diagonal));
        StoreVec16(states.fixed_left + r,
                    OfRVec16((OfRUVec16)LoadVec16(states.fixed_left + r) + (OfRUVec16)((fixed >> 1) & one)));
        StoreVec16(states.fixed_right + r,
                    OfRVec16((OfRUVec16)LoadVec16(states.fixed_right + r) + (OfRUVec16)(fixed & one)));
      }
    }
    const short target = static_cast<short>(length / 4);
    const OfRVec16 targets = {target, target, target, target, target, target, target, target};
    for (size_t r = 0; r < tile; r += 8)
    {
      const OfRVec16 diagonal = LoadVec16(states.diagonal + r);
      const OfRVec16 quota_diagonal = (OfRVec16)((OfRUVec16)diagonal >> 1);
      StoreVec16(states.quota_diagonal + r, quota_diagonal);
      StoreVec16(states.quota_left + r,
                  targets - LoadVec16(states.fixed_left + r) - quota_diagonal);
      StoreVec16(states.quota_right + r,
                  targets - LoadVec16(states.fixed_right + r) -
                  (OfRVec16)((OfRUVec16)diagonal - (OfRUVec16)quota_diagonal));
    }
  }

  size_t layer_start = stride * depth;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t first = 2 * gate * stride + residue;
    for (size_t r = 0; r < tile; r += 8)
    {
      const OfRVec16 x = TagLanes8(tags + first + r);
      const OfRVec16 y = TagLanes8(tags + first + stride + r);
      OfRVec16 top_tag = two;
      OfRVec16 bottom_tag = one;
      OfRVec16 control;
      if (length == 2)
        control = three ^ x;
      else
      {
        const OfRVec16 fixed = x & y;
        const OfRVec16 different = x ^ y;
        const OfRVec16 diagonal = ((different >> 1) & different) &
                                  ((x >> 1) ^ x) & one;
        const OfRVec16 variable = different & (diagonal - one);
        const OfRVec16 left_type = (variable >> 1) & one;
        const OfRVec16 right_type = variable & one;
        const OfRVec16 rank_diagonal = LoadVec16(states.rank_diagonal + r);
        const OfRVec16 rank_left = LoadVec16(states.rank_left + r);
        const OfRVec16 rank_right = LoadVec16(states.rank_right + r);
        const OfRVec16 use_diagonal = diagonal &
            (LoadVec16(states.quota_diagonal + r) > rank_diagonal);
        const OfRVec16 use_left = left_type &
            (LoadVec16(states.quota_left + r) > rank_left);
        const OfRVec16 use_right = right_type &
            (LoadVec16(states.quota_right + r) > rank_right);
        const OfRVec16 top_left = ((fixed >> 1) & one) | use_diagonal | use_left;
        const OfRVec16 top_right = (fixed & one) |
                                   (diagonal & (use_diagonal ^ one)) | use_right;
        top_tag = (top_left << 1) | top_right;
        bottom_tag = x ^ y ^ top_tag;
        const OfRVec16 straight = top_tag == x;
        const OfRVec16 cross = (top_tag == y) & ~straight;
        const OfRVec16 fork_y = (y >> 1) & one & ~(straight | cross);
        control = (((cross & one) | fork_y) << 1) | (straight & one) | fork_y;
        StoreVec16(states.rank_diagonal + r, OfRVec16((OfRUVec16)rank_diagonal + (OfRUVec16)diagonal));
        StoreVec16(states.rank_left + r, OfRVec16((OfRUVec16)rank_left + (OfRUVec16)left_type));
        StoreVec16(states.rank_right + r, OfRVec16((OfRUVec16)rank_right + (OfRUVec16)right_type));
      }

      const uint16_t encoded = PackControlLanes8(control);
      controls.set_eight_encoded(layer_start + residue + r, encoded);
      if (RouteWords)
      {
        if (OneWord)
        {
          const OfRVec16 top_masks = zero - ((control >> 1) & one);
          const OfRVec16 bottom_masks = zero - (control & one);
          for (size_t half = 0; half < 4; ++half)
          {
            const OfRVec32 top32 = half < 2
                ? (OfRVec32)__builtin_ia32_punpcklwd128(top_masks, top_masks)
                : (OfRVec32)__builtin_ia32_punpckhwd128(top_masks, top_masks);
            const OfRVec32 bottom32 = half < 2
                ? (OfRVec32)__builtin_ia32_punpcklwd128(bottom_masks, bottom_masks)
                : (OfRVec32)__builtin_ia32_punpckhwd128(bottom_masks, bottom_masks);
            const OfRVec32 top_mask = (half & 1U) == 0
                ? __builtin_ia32_punpckldq128(top32, top32)
                : __builtin_ia32_punpckhdq128(top32, top32);
            const OfRVec32 bottom_mask = (half & 1U) == 0
                ? __builtin_ia32_punpckldq128(bottom32, bottom32)
                : __builtin_ia32_punpckhdq128(bottom32, bottom32);
            size_t *a = rows + first + r + 2 * half;
            size_t *b = a + stride;
            const OfRVec32 row_x = LoadVec32(a);
            const OfRVec32 row_y = LoadVec32(b);
            const OfRVec32 different = row_x ^ row_y;
            StoreVec32(a, row_x ^ (different & top_mask));
            StoreVec32(b, row_x ^ (different & bottom_mask));
          }
        }
        else
        {
          alignas(16) uint16_t gate_controls[8];
          StoreVec16(gate_controls, control);
          for (size_t lane = 0; lane < 8; ++lane)
            RouteMembershipGate(rows + (first + r + lane) * words_per_row,
                                 rows + (first + stride + r + lane) * words_per_row,
                                 words_per_row, static_cast<uint8_t>(gate_controls[lane]));
        }
      }
      const uint64_t top_bytes = PackTagLanes8(top_tag);
      const uint64_t bottom_bytes = PackTagLanes8(bottom_tag);
      std::memcpy(tags + first + r, &top_bytes, 8);
      std::memcpy(tags + first + stride + r, &bottom_bytes, 8);
    }
    if (gate + 1 < gate_count)
    {
      const size_t carry = static_cast<size_t>(__builtin_ctzll(
          static_cast<unsigned long long>(gate + 1)));
      layer_start += stride * (depth + 1 + (size_t(2) << carry) - 2);
    }
  }
}


inline OfRVec8 LoadVec8(const void *source)
{
  OfRVec8 result;
  std::memcpy(&result, source, sizeof(result));
  return result;
}

inline void StoreVec8(void *destination, OfRVec8 value)
{
  std::memcpy(destination, &value, sizeof(value));
}

inline OfRVec8 TagLanes16(const uint8_t *tags) { return LoadVec8(tags); }

inline OfRVec8 ByteField(OfRVec8 value, unsigned bit)
{
  const OfRVec8 one = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  return (OfRVec8)((OfRVec16)value >> bit) & one;
}

// Values passed here are responsibility bits or tags and cannot carry into
// neighboring bytes at these public shifts.
inline OfRVec8 ByteLeftShift(OfRVec8 value, unsigned shift)
{ return (OfRVec8)((OfRVec16)value << shift); }

inline OfRVec8 ByteRightShift(OfRVec8 value, unsigned shift)
{
  const char byte_mask = static_cast<char>(255U >> shift);
  const OfRVec8 mask = {byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,
                        byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask,byte_mask};
  return (OfRVec8)((OfRVec16)value >> shift) & mask;
}

inline OfRVec8 PairLanes16(OfRVec8 x, OfRVec8 y)
{
  const OfRVec8 one = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  const OfRVec8 different = x ^ y;
  const OfRVec8 diagonal = ByteField(different, 1) & different &
                           (ByteField(x, 1) ^ x) & one;
  const OfRVec8 variable = different & (diagonal - one);
  return (x & y) | ByteLeftShift(variable, 2) | ByteLeftShift(diagonal, 4);
}

inline uint32_t PackControlLanes16(OfRVec8 controls)
{
  const OfRVec16 low_pair = {3,3,3,3,3,3,3,3};
  const OfRVec16 high_pair = {12,12,12,12,12,12,12,12};
  const OfRVec16 pairs = ((OfRVec16)controls & low_pair) |
                         (((OfRVec16)controls >> 6) & high_pair);
  const OfRVec32 low_four = {15,15,15,15};
  const OfRVec32 high_four = {240,240,240,240};
  const OfRVec32 groups = ((OfRVec32)pairs & low_four) |
                          (((OfRVec32)pairs >> 12) & high_four);
  return PackTagLanes4(groups);
}

struct alignas(16) ResidueBalance8
{
  uint8_t diagonal[256], fixed_left[256], fixed_right[256];
  uint8_t quota_diagonal[256], quota_left[256], quota_right[256];
  uint8_t rank_diagonal[256], rank_left[256], rank_right[256];
};

template <bool RouteWords, bool OneWord>
void OFRPostOrderTile8(uint8_t *tags, PackedControlMutableView controls,
                       size_t *rows, size_t words_per_row, size_t stride,
                       size_t depth, size_t length, size_t residue, size_t tile)
{
  const OfRVec8 zero = {};
  const OfRVec8 one = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  const OfRVec8 two = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
  const OfRVec8 three = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
  ResidueBalance8 states;
  const size_t gate_count = length / 2;
  if (length > 2)
  {
    std::fill(states.diagonal, states.diagonal + tile, 0);
    std::fill(states.fixed_left, states.fixed_left + tile, 0);
    std::fill(states.fixed_right, states.fixed_right + tile, 0);
    std::fill(states.rank_diagonal, states.rank_diagonal + tile, 0);
    std::fill(states.rank_left, states.rank_left + tile, 0);
    std::fill(states.rank_right, states.rank_right + tile, 0);
    for (size_t gate = 0; gate < gate_count; ++gate)
    {
      const size_t first = 2 * gate * stride + residue;
      for (size_t r = 0; r < tile; r += 16)
      {
        const OfRVec8 x = TagLanes16(tags + first + r);
        const OfRVec8 y = TagLanes16(tags + first + stride + r);
        const OfRVec8 fixed = x & y;
        const OfRVec8 different = x ^ y;
        const OfRVec8 diagonal = ByteField(different, 1) & different & (ByteField(x, 1) ^ x) & one;
        StoreVec8(states.diagonal + r,
                    OfRVec8((OfRUVec8)LoadVec8(states.diagonal + r) + (OfRUVec8)diagonal));
        StoreVec8(states.fixed_left + r,
                    OfRVec8((OfRUVec8)LoadVec8(states.fixed_left + r) + (OfRUVec8)ByteField(fixed, 1)));
        StoreVec8(states.fixed_right + r,
                    OfRVec8((OfRUVec8)LoadVec8(states.fixed_right + r) + (OfRUVec8)(fixed & one)));
      }
    }
    const char target = static_cast<char>(length / 4);
    const OfRVec8 targets = {target, target, target, target, target, target, target, target, target, target, target, target, target, target, target, target};
    for (size_t r = 0; r < tile; r += 16)
    {
      const OfRVec8 diagonal = LoadVec8(states.diagonal + r);
      const OfRVec8 quota_diagonal = ByteRightShift(diagonal, 1);
      StoreVec8(states.quota_diagonal + r, quota_diagonal);
      StoreVec8(states.quota_left + r,
                  targets - LoadVec8(states.fixed_left + r) - quota_diagonal);
      StoreVec8(states.quota_right + r,
                  targets - LoadVec8(states.fixed_right + r) -
                  (OfRVec8)((OfRUVec8)diagonal - (OfRUVec8)quota_diagonal));
    }
  }

  size_t layer_start = stride * depth;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t first = 2 * gate * stride + residue;
    for (size_t r = 0; r < tile; r += 16)
    {
      const OfRVec8 x = TagLanes16(tags + first + r);
      const OfRVec8 y = TagLanes16(tags + first + stride + r);
      OfRVec8 top_tag = two;
      OfRVec8 bottom_tag = one;
      OfRVec8 control;
      if (length == 2)
        control = three ^ x;
      else
      {
        const OfRVec8 fixed = x & y;
        const OfRVec8 different = x ^ y;
        const OfRVec8 diagonal = ByteField(different, 1) & different &
                                (ByteField(x, 1) ^ x) & one;
        const OfRVec8 variable = different & (diagonal - one);
        const OfRVec8 left_type = ByteField(variable, 1);
        const OfRVec8 right_type = variable & one;
        const OfRVec8 rank_diagonal = LoadVec8(states.rank_diagonal + r);
        const OfRVec8 rank_left = LoadVec8(states.rank_left + r);
        const OfRVec8 rank_right = LoadVec8(states.rank_right + r);
        const OfRVec8 use_diagonal = diagonal &
            (LoadVec8(states.quota_diagonal + r) > rank_diagonal);
        const OfRVec8 use_left = left_type &
            (LoadVec8(states.quota_left + r) > rank_left);
        const OfRVec8 use_right = right_type &
            (LoadVec8(states.quota_right + r) > rank_right);
        const OfRVec8 top_left = ByteField(fixed, 1) | use_diagonal | use_left;
        const OfRVec8 top_right = (fixed & one) |
                                   (diagonal & (use_diagonal ^ one)) | use_right;
        top_tag = ByteLeftShift(top_left, 1) | top_right;
        bottom_tag = x ^ y ^ top_tag;
        const OfRVec8 straight = top_tag == x;
        const OfRVec8 cross = (top_tag == y) & ~straight;
        const OfRVec8 fork_y = ByteField(y, 1) & ~(straight | cross);
        control = ByteLeftShift((cross & one) | fork_y, 1) | (straight & one) | fork_y;
        StoreVec8(states.rank_diagonal + r, OfRVec8((OfRUVec8)rank_diagonal + (OfRUVec8)diagonal));
        StoreVec8(states.rank_left + r, OfRVec8((OfRUVec8)rank_left + (OfRUVec8)left_type));
        StoreVec8(states.rank_right + r, OfRVec8((OfRUVec8)rank_right + (OfRUVec8)right_type));
      }

      const uint32_t encoded = PackControlLanes16(control);
      controls.set_sixteen_encoded(layer_start + residue + r, encoded);
      if (RouteWords)
      {
        if (OneWord)
        {
          const OfRVec8 top_masks = zero - ByteField(control, 1);
          const OfRVec8 bottom_masks = zero - (control & one);
          for (size_t half = 0; half < 8; ++half)
          {
            const OfRVec16 top16 = half < 4
                ? (OfRVec16)__builtin_ia32_punpcklbw128(top_masks, top_masks)
                : (OfRVec16)__builtin_ia32_punpckhbw128(top_masks, top_masks);
            const OfRVec16 bottom16 = half < 4
                ? (OfRVec16)__builtin_ia32_punpcklbw128(bottom_masks, bottom_masks)
                : (OfRVec16)__builtin_ia32_punpckhbw128(bottom_masks, bottom_masks);
            const OfRVec32 top32 = (half & 2U) == 0
                ? (OfRVec32)__builtin_ia32_punpcklwd128(top16, top16)
                : (OfRVec32)__builtin_ia32_punpckhwd128(top16, top16);
            const OfRVec32 bottom32 = (half & 2U) == 0
                ? (OfRVec32)__builtin_ia32_punpcklwd128(bottom16, bottom16)
                : (OfRVec32)__builtin_ia32_punpckhwd128(bottom16, bottom16);
            const OfRVec32 top_mask = (half & 1U) == 0
                ? __builtin_ia32_punpckldq128(top32, top32)
                : __builtin_ia32_punpckhdq128(top32, top32);
            const OfRVec32 bottom_mask = (half & 1U) == 0
                ? __builtin_ia32_punpckldq128(bottom32, bottom32)
                : __builtin_ia32_punpckhdq128(bottom32, bottom32);
            size_t *a = rows + first + r + 2 * half;
            size_t *b = a + stride;
            const OfRVec32 row_x = LoadVec32(a);
            const OfRVec32 row_y = LoadVec32(b);
            const OfRVec32 different = row_x ^ row_y;
            StoreVec32(a, row_x ^ (different & top_mask));
            StoreVec32(b, row_x ^ (different & bottom_mask));
          }
        }
        else
        {
          alignas(16) uint8_t gate_controls[16];
          StoreVec8(gate_controls, control);
          for (size_t lane = 0; lane < 16; ++lane)
            RouteMembershipGate(rows + (first + r + lane) * words_per_row,
                                 rows + (first + stride + r + lane) * words_per_row,
                                 words_per_row, static_cast<uint8_t>(gate_controls[lane]));
        }
      }
      StoreVec8(tags + first + r, top_tag);
      StoreVec8(tags + first + stride + r, bottom_tag);
    }
    if (gate + 1 < gate_count)
    {
      const size_t carry = static_cast<size_t>(__builtin_ctzll(
          static_cast<unsigned long long>(gate + 1)));
      layer_start += stride * (depth + 1 + (size_t(2) << carry) - 2);
    }
  }
}

inline uint32_t SumLanes4(OfRVec32 values)
{
  values += __builtin_ia32_pshufd(values, 0x4e);
  values += __builtin_ia32_pshufd(values, 0xb1);
  return static_cast<uint32_t>(values[0]);
}

inline OfRVec32 PrefixChoice4(OfRVec32 type, int32_t &remaining)
{
  OfRVec32 prefix = type + (OfRVec32)__builtin_ia32_pslldqi128((OfRVec64)type, 32);
  prefix += (OfRVec32)__builtin_ia32_pslldqi128((OfRVec64)prefix, 64);
  const OfRVec32 before = (OfRVec32)__builtin_ia32_pslldqi128((OfRVec64)prefix, 32);
  const OfRVec32 quotas = {remaining, remaining, remaining, remaining};
  const OfRVec32 choice = type & (quotas > before);
  remaining -= prefix[3];
  return choice;
}

inline void LoadGatePairQuad(const uint8_t *tags, size_t first, size_t stride,
                             OfRVec32 &x, OfRVec32 &y)
{
  if (stride == 1)
  {
    const OfRVec8 zero = {};
    OfRVec8 bytes = {};
    std::memcpy(&bytes, tags + first, 8);
    const OfRVec32 pairs = (OfRVec32)__builtin_ia32_punpcklbw128(bytes, zero);
    const OfRVec32 mask = {65535, 65535, 65535, 65535};
    x = pairs & mask;
    y = pairs >> 16;
  }
  else
  {
    x = OfRVec32{tags[first], tags[first + 2 * stride],
                 tags[first + 4 * stride], tags[first + 6 * stride]};
    y = OfRVec32{tags[first + stride], tags[first + 3 * stride],
                 tags[first + 5 * stride], tags[first + 7 * stride]};
  }
}

template <bool RouteWords>
void OFRPostOrderPrefixImpl(uint8_t *tags, PackedControlMutableView controls,
                            size_t *rows, size_t words_per_row, size_t stride,
                            size_t depth, size_t length)
{
  const OfRVec32 zero = {};
  const OfRVec16 zero16 = {};
  const OfRVec32 one = {1,1,1,1};
  const size_t gate_count = length / 2;
  for (size_t residue = 0; residue < stride; ++residue)
  {
    size_t diagonal_count = 0, fixed_left_count = 0, fixed_right_count = 0;
    for (size_t gate = 0; gate < gate_count; gate += 4)
    {
      OfRVec32 x, y;
      LoadGatePairQuad(tags, 2 * gate * stride + residue, stride, x, y);
      const OfRVec32 fixed = x & y;
      const OfRVec32 different = x ^ y;
      const OfRVec32 diagonal = ((different >> 1) & different) &
                                ((x >> 1) ^ x) & one;
      diagonal_count += SumLanes4(diagonal);
      fixed_left_count += SumLanes4((fixed >> 1) & one);
      fixed_right_count += SumLanes4(fixed & one);
    }
    int32_t remaining_diagonal = static_cast<int32_t>(diagonal_count / 2);
    int32_t remaining_left = static_cast<int32_t>(
        length / 4 - fixed_left_count - diagonal_count / 2);
    int32_t remaining_right = static_cast<int32_t>(
        length / 4 - fixed_right_count - (diagonal_count - diagonal_count / 2));
    size_t layer_start = stride * depth;
    for (size_t gate = 0; gate < gate_count; gate += 4)
    {
      const size_t first = 2 * gate * stride + residue;
      OfRVec32 x, y;
      LoadGatePairQuad(tags, first, stride, x, y);
      const OfRVec32 fixed = x & y;
      const OfRVec32 different = x ^ y;
      const OfRVec32 diagonal = ((different >> 1) & different) &
                                ((x >> 1) ^ x) & one;
      const OfRVec32 variable = different & (diagonal - one);
      const OfRVec32 left_type = (variable >> 1) & one;
      const OfRVec32 right_type = variable & one;
      const OfRVec32 use_diagonal = PrefixChoice4(diagonal, remaining_diagonal);
      const OfRVec32 use_left = PrefixChoice4(left_type, remaining_left);
      const OfRVec32 use_right = PrefixChoice4(right_type, remaining_right);
      const OfRVec32 top_left = ((fixed >> 1) & one) | use_diagonal | use_left;
      const OfRVec32 top_right = (fixed & one) |
          (diagonal & (use_diagonal ^ one)) | use_right;
      const OfRVec32 top_tag = (top_left << 1) | top_right;
      const OfRVec32 bottom_tag = x ^ y ^ top_tag;
      const OfRVec32 straight = top_tag == x;
      const OfRVec32 cross = (top_tag == y) & ~straight;
      const OfRVec32 fork_y = (y >> 1) & one & ~(straight | cross);
      const OfRVec32 control = (((cross & one) | fork_y) << 1) |
                                (straight & one) | fork_y;
      alignas(16) uint32_t gate_controls[4];
      StoreVec32(gate_controls, control);
      if (stride == 1)
      {
        controls.set_two_encoded(layer_start,
            static_cast<uint8_t>(gate_controls[0] | (gate_controls[1] << 2U)));
        const size_t carry = static_cast<size_t>(__builtin_ctzll(
            static_cast<unsigned long long>(gate + 2)));
        layer_start += 1 + (size_t(2) << carry) - 1;
        controls.set_two_encoded(layer_start,
            static_cast<uint8_t>(gate_controls[2] | (gate_controls[3] << 2U)));
        if (gate + 4 < gate_count)
        {
          const size_t next_carry = static_cast<size_t>(__builtin_ctzll(
              static_cast<unsigned long long>(gate + 4)));
          layer_start += (size_t(2) << next_carry);
        }
      }
      else
        for (size_t lane = 0; lane < 4; ++lane)
        {
          controls.set(layer_start + residue, static_cast<uint8_t>(gate_controls[lane]));
          if (gate + lane + 1 < gate_count)
          {
            const size_t carry = static_cast<size_t>(__builtin_ctzll(
                static_cast<unsigned long long>(gate + lane + 1)));
            layer_start += stride * (depth + 1 + (size_t(2) << carry) - 2);
          }
        }

      if (RouteWords && words_per_row == 1 && sizeof(size_t) == 8 && stride == 1)
      {
        const OfRVec32 top_masks = zero - ((control >> 1) & one);
        const OfRVec32 bottom_masks = zero - (control & one);
        for (size_t half = 0; half < 2; ++half)
        {
          size_t *a = rows + first + 4 * half;
          const OfRVec64 first_pair = (OfRVec64)LoadVec32(a);
          const OfRVec64 second_pair = (OfRVec64)LoadVec32(a + 2);
          const OfRVec32 row_x = (OfRVec32)__builtin_ia32_punpcklqdq128(first_pair, second_pair);
          const OfRVec32 row_y = (OfRVec32)__builtin_ia32_punpckhqdq128(first_pair, second_pair);
          const OfRVec32 top_mask = half == 0
              ? __builtin_ia32_punpckldq128(top_masks, top_masks)
              : __builtin_ia32_punpckhdq128(top_masks, top_masks);
          const OfRVec32 bottom_mask = half == 0
              ? __builtin_ia32_punpckldq128(bottom_masks, bottom_masks)
              : __builtin_ia32_punpckhdq128(bottom_masks, bottom_masks);
          const OfRVec32 different_rows = row_x ^ row_y;
          const OfRVec64 top_rows = (OfRVec64)(row_x ^ (different_rows & top_mask));
          const OfRVec64 bottom_rows = (OfRVec64)(row_x ^ (different_rows & bottom_mask));
          StoreVec32(a, (OfRVec32)__builtin_ia32_punpcklqdq128(top_rows, bottom_rows));
          StoreVec32(a + 2, (OfRVec32)__builtin_ia32_punpckhqdq128(top_rows, bottom_rows));
        }
      }
      else if (RouteWords)
        for (size_t lane = 0; lane < 4; ++lane)
          RouteMembershipGate(rows + (first + 2 * lane * stride) * words_per_row,
                               rows + (first + (2 * lane + 1) * stride) * words_per_row,
                               words_per_row, static_cast<uint8_t>(gate_controls[lane]));

      if (stride == 1)
      {
        const OfRVec16 top16 = __builtin_ia32_packssdw128(top_tag, zero);
        const OfRVec16 bottom16 = __builtin_ia32_packssdw128(bottom_tag, zero);
        const OfRVec16 interleaved = __builtin_ia32_punpcklwd128(top16, bottom16);
        const OfRVec8 bytes = __builtin_ia32_packuswb128(interleaved, zero16);
        std::memcpy(tags + first, &bytes, 8);
      }
      else
      {
        alignas(16) uint32_t top_values[4], bottom_values[4];
        StoreVec32(top_values, top_tag);
        StoreVec32(bottom_values, bottom_tag);
        for (size_t lane = 0; lane < 4; ++lane)
        {
          tags[first + 2 * lane * stride] = static_cast<uint8_t>(top_values[lane]);
          tags[first + (2 * lane + 1) * stride] = static_cast<uint8_t>(bottom_values[lane]);
        }
      }
    }
  }
}

size_t ResidueTileSize(size_t words_per_row, size_t counter_bytes)
{
  const size_t budget = 24 * 1024;
  // Metadata and the two active tags plus both membership rows must fit the
  // public working-set budget. Very wide rows use a one-residue tile.
  if (words_per_row > budget / (2 * sizeof(size_t)))
    return 1;
  const size_t bytes = 9 * counter_bytes + 2 +
                       2 * words_per_row * sizeof(size_t);
  const size_t limit = std::min<size_t>(256, budget / bytes);
  size_t tile = 1;
  while (tile <= limit / 2)
    tile *= 2;
  return tile;
}

template <bool RouteWords>
void OFRControlPostOrderTiledImpl(uint8_t *tags,
                                 PackedControlMutableView controls,
                                 size_t *rows, size_t words_per_row, size_t n)
{
  std::array<ResidueBalanceState, 256> states;
  size_t depth = 0;
  // At stride s, each residue is an independent DFS view of length n/s.
  // Processing those views by tiles preserves each view's gate/rank order
  // while accessing contiguous tag and membership lanes.
  for (size_t stride = 1; stride < n; stride *= 2, ++depth)
  {
    const size_t length = n / stride;
    const size_t gate_count = length / 2;
    const size_t lane_width = length <= 256 ? 16 : length <= 65536 ? 8 :
        n <= static_cast<size_t>(std::numeric_limits<int32_t>::max()) ? 4 : 1;
    const size_t counter_bytes = lane_width == 16 ? sizeof(uint8_t) : lane_width == 8 ? sizeof(uint16_t) :
        lane_width == 4 ? sizeof(uint32_t) : sizeof(size_t);
    size_t max_tile = ResidueTileSize(RouteWords ? words_per_row : 0, counter_bytes);
    if (max_tile < lane_width)
      max_tile = ResidueTileSize(RouteWords ? words_per_row : 0, sizeof(size_t));
    const size_t tile = std::min(stride, max_tile);
    if (stride <= 4 && length >= 8 &&
        n <= static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    {
      OFRPostOrderPrefixImpl<RouteWords>(tags, controls, rows, words_per_row,
                                         stride, depth, length);
      continue;
    }
    for (size_t residue = 0; residue < stride; residue += tile)
    {
      if (length <= 256 && tile >= 16)
      {
        if (!RouteWords)
          OFRPostOrderTile8<false, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else if (words_per_row == 1 && sizeof(size_t) == 8)
          OFRPostOrderTile8<true, true>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else
          OFRPostOrderTile8<true, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        continue;
      }
      if (length <= 65536 && tile >= 8)
      {
        if (!RouteWords)
          OFRPostOrderTile16<false, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else if (words_per_row == 1 && sizeof(size_t) == 8)
          OFRPostOrderTile16<true, true>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else
          OFRPostOrderTile16<true, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        continue;
      }
      // Signed SSE2 comparisons are valid because all ranks/quotas are at
      // most n/2. This choice depends only on public dimensions.
      if (n <= static_cast<size_t>(std::numeric_limits<int32_t>::max()) && tile >= 4)
      {
        if (!RouteWords)
          OFRPostOrderTile32<false, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else if (words_per_row == 1 && sizeof(size_t) == 8)
          OFRPostOrderTile32<true, true>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        else
          OFRPostOrderTile32<true, false>(tags, controls, rows, words_per_row,
              stride, depth, length, residue, tile);
        continue;
      }
      if (length > 2)
      {
        for (size_t r = 0; r < tile; ++r)
        {
          ResidueBalanceState &state = states[r];
          state.diagonal = state.fixed_left = state.fixed_right = 0;
          state.rank_diagonal = state.rank_left = state.rank_right = 0;
        }
        for (size_t gate = 0; gate < gate_count; ++gate)
        {
          const size_t first = 2 * gate * stride + residue;
          for (size_t r = 0; r < tile; ++r)
          {
            const uint8_t pair = ClassifyPairBits(
                tags[first + r], tags[first + stride + r]);
            ResidueBalanceState &state = states[r];
            state.diagonal += (pair >> 4U) & 1U;
            state.fixed_left += (pair >> 1U) & 1U;
            state.fixed_right += pair & 1U;
          }
        }
        for (size_t r = 0; r < tile; ++r)
        {
          ResidueBalanceState &state = states[r];
          state.quota_diagonal = state.diagonal / 2;
          state.quota_left = length / 4 - state.fixed_left -
                             state.quota_diagonal;
          state.quota_right = length / 4 - state.fixed_right -
                              (state.diagonal - state.quota_diagonal);
        }
      }

      // This is the postorder layer start for the first contiguous block of
      // width 2*s. Subsequent starts depend on the carry bits of gate+1;
      // neither an n-entry index table nor a second control tape is needed.
      size_t layer_start = stride * depth;
      for (size_t gate = 0; gate < gate_count; ++gate)
      {
        const size_t first = 2 * gate * stride + residue;
        for (size_t r = 0; r < tile; ++r)
        {
          const uint8_t x = tags[first + r];
          const uint8_t y = tags[first + stride + r];
          uint8_t top_tag = OFR_TAG_LEFT;
          uint8_t bottom_tag = OFR_TAG_RIGHT;
          uint8_t control;
          if (length == 2)
            control = static_cast<uint8_t>(OFR_TAG_BOTH ^ x);
          else
          {
            // Reclassify the already-loaded pair rather than writing and
            // rereading a root-sized feature array.
            const uint8_t pair = ClassifyPairBits(x, y);
            const uint8_t diagonal_type = static_cast<uint8_t>((pair >> 4U) & 1U);
            const uint8_t left_type = static_cast<uint8_t>((pair >> 3U) & 1U);
            const uint8_t right_type = static_cast<uint8_t>((pair >> 2U) & 1U);
            ResidueBalanceState &state = states[r];
            const uint8_t use_diagonal = static_cast<uint8_t>(
                diagonal_type & CtLessSize(state.rank_diagonal, state.quota_diagonal));
            const uint8_t use_left = static_cast<uint8_t>(
                left_type & CtLessSize(state.rank_left, state.quota_left));
            const uint8_t use_right = static_cast<uint8_t>(
                right_type & CtLessSize(state.rank_right, state.quota_right));
            const uint8_t top_left = static_cast<uint8_t>(
                ((pair >> 1U) & 1U) | use_diagonal | use_left);
            const uint8_t top_right = static_cast<uint8_t>(
                (pair & 1U) | (diagonal_type & (use_diagonal ^ 1U)) | use_right);
            top_tag = static_cast<uint8_t>((top_left << 1U) | top_right);
            bottom_tag = static_cast<uint8_t>(x ^ y ^ top_tag);
            control = GateControlForBalancedPair(x, y, top_tag);
            state.rank_diagonal += diagonal_type;
            state.rank_left += left_type;
            state.rank_right += right_type;
          }
          controls.set(layer_start + residue + r, control);
          if (RouteWords)
            RouteMembershipGate(rows + (first + r) * words_per_row,
                                 rows + (first + stride + r) * words_per_row,
                                 words_per_row, control);
          tags[first + r] = top_tag;
          tags[first + stride + r] = bottom_tag;
        }
        if (gate + 1 < gate_count)
        {
          const size_t carry = static_cast<size_t>(__builtin_ctzll(
              static_cast<unsigned long long>(gate + 1)));
          layer_start += stride * (depth + 1 + (size_t(2) << carry) - 2);
        }
      }
    }
  }
}

void ValidateTagView(const std::vector<uint8_t> &tags,
                     size_t base, size_t stride, size_t n,
                     size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (stride == 0 || base >= tags.size() ||
      n - 1 > (tags.size() - 1 - base) / stride)
    throw std::invalid_argument("Invalid OFR tag view");
  size_t left_weight = 0, right_weight = 0;
  uint8_t invalid = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const uint8_t tag = tags[base + i * stride];
    invalid = static_cast<uint8_t>(invalid | (tag >> 2U));
    left_weight += LeftBit(tag);
    right_weight += RightBit(tag);
  }
  if (invalid != 0 || left_weight != n_left || right_weight != n_right)
    throw std::invalid_argument("OFR tag view capacities mismatch");
}

} // namespace

OFRBalanceResult OFRBalance(const std::vector<uint8_t> &tags,
                            size_t n, size_t n_left, size_t n_right)
{
  ValidateTags(tags, n, n_left, n_right, true);
  if (n < 3)
    throw std::invalid_argument("OFR balance requires at least three tags");
  std::vector<uint8_t> work(tags);
  OFRBalanceResult result;
  result.controls.resize(n / 2);
  std::vector<uint8_t> scratch(n / 2);
  OFRBalanceInPlaceImpl(work.data(), result.controls, scratch.data(), 0, 1, n,
                        n_left, n_right, 0);
  result.top_tags.reserve(n / 2 + n % 2);
  result.bottom_tags.reserve(n / 2);
  for (size_t i = 0; i < n; ++i)
    (i % 2 == 0 ? result.top_tags : result.bottom_tags).push_back(work[i]);
  return result;
}

size_t OFRBalanceInPlace(std::vector<uint8_t> &tags,
                         PackedControls &controls,
                         size_t base, size_t stride, size_t n,
                         size_t n_left, size_t n_right, size_t position)
{
  if (n < 3)
    throw std::invalid_argument("OFR balance requires at least three tags");
  ValidateTagView(tags, base, stride, n, n_left, n_right);
  const size_t required = n / 2;
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR balance exceeds control array");
  std::vector<uint8_t> scratch(n / 2);
  return OFRBalanceInPlaceImpl(tags.data(), controls, scratch.data(), base, stride, n,
                               n_left, n_right, position);
}

size_t OFRControlCount(size_t n, size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  return OFRControlCountImpl(n, n_left, n_right);
}

size_t OFRControlWrite(const std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position)
{
  ValidateTags(tags, n, n_left, n_right, true);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR control write exceeds destination array");
  std::vector<uint8_t> work(tags);
  std::vector<uint8_t> scratch(n / 2);
  return OFRControlWriteImpl(
      work.data(), controls, scratch.data(), 0, 1, n, n_left, n_right, position);
}

size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t base, size_t stride, size_t n,
                       size_t n_left, size_t n_right, size_t position)
{
  ValidateTagView(tags, base, stride, n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR control write exceeds destination array");
  std::vector<uint8_t> scratch(n / 2);
  return OFRControlWriteImpl(tags.data(), controls, scratch.data(), base, stride, n,
                             n_left, n_right, position);
}

size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       PackedControls &controls,
                       size_t n, size_t n_left, size_t n_right,
                       size_t position)
{
  return OFRControlWrite(tags, controls, 0, 1, n,
                         n_left, n_right, position);
}

size_t OFRControlWriteAndRoute(std::vector<uint8_t> &tags,
                               PackedControls &controls,
                               size_t *rows, size_t words_per_row,
                               size_t n, size_t n_left, size_t n_right,
                               size_t position)
{
  ValidateTagView(tags, 0, 1, n, n_left, n_right);
  return detail::OFRControlWriteAndRouteNormalized(
      tags, controls, rows, words_per_row, n, n_left, n_right, position,
      OFRControlCountImpl(n, n_left, n_right));
}

size_t detail::OFRControlWriteAndRouteNormalized(
    std::vector<uint8_t> &tags, PackedControls &controls,
    size_t *rows, size_t words_per_row, size_t n,
    size_t n_left, size_t n_right, size_t position, size_t control_count,
    std::vector<uint8_t> *feature_scratch)
{
  ValidateCapacities(n, n_left, n_right);
  size_t row_words = 0, row_bytes = 0;
  if (tags.size() != n || rows == NULL || words_per_row == 0 ||
      MulOverflowSize(n, words_per_row, &row_words) ||
      MulOverflowSize(row_words, sizeof(size_t), &row_bytes))
    throw std::invalid_argument("Invalid OFR membership dimensions");
  (void)row_words;
  (void)row_bytes;
  if (position > controls.size() || control_count > controls.size() - position)
    throw std::length_error("OFR control write exceeds destination array");
  std::vector<uint8_t> local_scratch;
  std::vector<uint8_t> &scratch = feature_scratch ? *feature_scratch : local_scratch;
  scratch.resize(n / 2);
  const size_t next = OFRControlWriteAndRouteImpl(
      tags.data(), controls, scratch.data(), rows, words_per_row,
      0, 1, n, n_left, n_right, position);
  if (next - position != control_count)
    throw std::logic_error("OFR normalized control count mismatch");
  return next;
}

size_t OFRControlWritePostOrderAndRoute(std::vector<uint8_t> &tags,
                                        PackedControls &controls,
                                        size_t *rows, size_t words_per_row,
                                        size_t n, size_t n_left, size_t n_right,
                                        size_t position)
{
  ValidateTagView(tags, 0, 1, n, n_left, n_right);
  return detail::OFRControlWritePostOrderAndRouteNormalized(
      tags, controls, rows, words_per_row, n, n_left, n_right, position,
      OFRControlCountImpl(n, n_left, n_right));
}

size_t detail::OFRControlWritePostOrderAndRouteNormalized(
    std::vector<uint8_t> &tags, PackedControls &controls,
    size_t *rows, size_t words_per_row, size_t n,
    size_t n_left, size_t n_right, size_t position, size_t control_count)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Postorder OFR requires balanced power-of-two capacities");
  size_t row_words = 0, row_bytes = 0;
  if (tags.size() != n || rows == NULL || words_per_row == 0 ||
      MulOverflowSize(n, words_per_row, &row_words) ||
      MulOverflowSize(row_words, sizeof(size_t), &row_bytes))
    throw std::invalid_argument("Invalid OFR membership dimensions");
  (void)row_words;
  (void)row_bytes;
  if (position > controls.size() || control_count > controls.size() - position)
    throw std::length_error("OFR control write exceeds destination array");
  OFRControlPostOrderTiledImpl<true>(
      tags.data(), controls.mutable_view(position, control_count),
      rows, words_per_row, n);
  return position + control_count;
}

PackedControls OFRControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right)
{
  std::vector<uint8_t> normalized = OFRNormalize(
      tags, n, n_left, n_right);
  PackedControls controls(
      OFRControlCountImpl(n, n_left, n_right));
  std::vector<uint8_t> scratch(n / 2);
  const size_t next = OFRControlWriteImpl(
      normalized.data(), controls, scratch.data(), 0, 1, n, n_left, n_right, 0);
  if (next != controls.size())
    throw std::logic_error("OFR control write/count mismatch");
  return controls;
}

PackedControls OFRControlPostOrder(const std::vector<uint8_t> &tags,
                                         size_t n,
                                         size_t n_left,
                                         size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Postorder OFR requires balanced power-of-two capacities");
  std::vector<uint8_t> normalized = OFRNormalize(
      tags, n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  PackedControls controls(required);
  OFRControlPostOrderTiledImpl<false>(
      normalized.data(), controls.mutable_view(), NULL, 0, n);
  return controls;
}

PackedControls OFRLevelOrderControls(
    const PackedControls &controls,
    size_t n,
    size_t n_left,
    size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Level-order OFR requires balanced power-of-two capacities");
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("OFR control array length mismatch");

  PackedControls level_controls(required);
  const size_t next = WriteLevelOrderControls(
      controls, &level_controls, n, n / 2, 0, 1, 0, 0);
  if (next != required)
    throw std::logic_error("OFR level-order conversion mismatch");
  return level_controls;
}

PackedControls OFRPostOrderControls(
    const PackedControls &controls,
    size_t n,
    size_t n_left,
    size_t n_right)
{
  return OFRPostOrderControls(controls.view(),
                              n, n_left, n_right);
}

PackedControls OFRPostOrderControls(
    PackedControlView controls,
    size_t n, size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Postorder OFR requires balanced power-of-two capacities");
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("OFR control array length mismatch");

  std::vector<size_t> starts(n);
  if (BuildDfsControlStarts(n, 1, 0, 0, &starts) != required)
    throw std::logic_error("OFR DFS control layout mismatch");
  PackedControls postorder_controls(required);
  const size_t next = WritePostOrderFromDfs(
      controls, starts, &postorder_controls, 0, n, 0);
  if (next != postorder_controls.size())
    throw std::logic_error("OFR postorder conversion mismatch");
  return postorder_controls;
}

void OFRApplyInPlace(unsigned char *data,
                     const PackedControls &controls,
                     size_t n,
                     size_t n_left,
                     size_t n_right,
                     size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("OFR control array length mismatch");
  OFRApplyPreparedInPlace(data, controls, n, n_left, n_right, block_size);
}

void OFRApplyPreparedInPlace(unsigned char *data,
                             const PackedControls &controls,
                             size_t n,
                             size_t n_left,
                             size_t n_right,
                             size_t block_size)
{
  OFRApplyPreparedInPlace(data, controls.view(),
                          n, n_left, n_right, block_size);
}

void OFRApplyPreparedInPlace(unsigned char *data,
                             PackedControlView controls,
                             size_t n,
                             size_t n_left,
                             size_t n_right,
                             size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid OFR data dimensions");
  (void)data_bytes;
  if (controls.size() != OFRControlCountImpl(n, n_left, n_right))
    throw std::length_error("OFR control array length mismatch");
  if (n_left == 0 || n_right == 0)
  {
    if (controls.size() != 0)
      throw std::logic_error("OFR control consumption mismatch");
    return;
  }

  const size_t next = OFRApplyStrided(
      data, controls, n, n_left, n_right, block_size, 1, 0);
  if (next != controls.size())
    throw std::logic_error("OFR control consumption mismatch");
}

void OFRApplyLevelOrderedInPlace(
    unsigned char *data,
    const PackedControls &level_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (level_controls.size() != required)
    throw std::length_error("OFR control array length mismatch");
  OFRApplyPreparedLevelOrderedInPlace(
      data, level_controls, n, n_left, n_right, block_size);
}

void OFRApplyPreparedLevelOrderedInPlace(
    unsigned char *data,
    const PackedControls &level_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (!IsBalancedPowerOfTwo(n, n_left, n_right) ||
      data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid level-order OFR dimensions");
  (void)data_bytes;

  if (level_controls.size() != OFRControlCountImpl(n, n_left, n_right))
    throw std::length_error("OFR control array length mismatch");

#ifndef BEFTS_MODE
  if (block_size == 4)
    ApplyLevelOrderedWithStyle<OFORK_4>(
        data, level_controls.view(), n, block_size);
  else if (block_size == 8)
    ApplyLevelOrderedWithStyle<OFORK_8>(
        data, level_controls.view(), n, block_size);
  else if (block_size == 12)
    ApplyLevelOrderedWithStyle<OFORK_12>(
        data, level_controls.view(), n, block_size);
  else if (block_size == 16)
    ApplyLevelOrderedWithStyle<OFORK_16>(
        data, level_controls.view(), n, block_size);
  else if (block_size == 24)
    ApplyLevelOrderedWithStyle<OFORK_24>(
        data, level_controls.view(), n, block_size);
  else if (block_size >= 16 && block_size % 16 == 0 &&
           block_size <= std::numeric_limits<uint32_t>::max())
    ApplyLevelOrderedWithStyle<OFORK_16X>(
        data, level_controls.view(), n, block_size);
  else if (block_size >= 24 && block_size % 16 == 8 &&
           block_size <= std::numeric_limits<uint32_t>::max())
    ApplyLevelOrderedWithStyle<OFORK_8_16X>(
        data, level_controls.view(), n, block_size);
  else
#endif
    ApplyLevelOrderedGates(data, level_controls.view(), n, block_size,
                           GenericGate());

}

void OFRApplyPostOrderInPlace(
    unsigned char *data,
    const PackedControls &postorder_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (postorder_controls.size() != required)
    throw std::length_error("OFR control array length mismatch");
  OFRApplyPreparedPostOrderInPlace(
      data, postorder_controls, n, n_left, n_right, block_size);
}

void OFRApplyPreparedPostOrderInPlace(
    unsigned char *data,
    const PackedControls &postorder_controls,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size)
{
  OFRApplyPreparedPostOrderInPlace(
      data, postorder_controls.view(),
      n, n_left, n_right, block_size);
}

void OFRApplyPreparedPostOrderInPlace(
    unsigned char *data, PackedControlView postorder_controls, size_t n, size_t n_left, size_t n_right,
    size_t block_size)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (!IsBalancedPowerOfTwo(n, n_left, n_right) ||
      data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid postorder OFR dimensions");
  (void)data_bytes;

  if (postorder_controls.size() != OFRControlCountImpl(n, n_left, n_right))
    throw std::length_error("OFR control array length mismatch");

  PackedControlCursor control_data(postorder_controls);
  size_t next = 0;
#ifndef BEFTS_MODE
  if (block_size == 4)
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_4>());
  else if (block_size == 8)
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_8>());
  else if (block_size == 12)
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_12>());
  else if (block_size == 16)
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_16>());
  else if (block_size == 24)
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_24>());
  else if (block_size >= 16 && block_size % 16 == 0 &&
           block_size <= std::numeric_limits<uint32_t>::max())
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_16X>());
  else if (block_size >= 24 && block_size % 16 == 8 &&
           block_size <= std::numeric_limits<uint32_t>::max())
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               StyledGate<OFORK_8_16X>());
  else
#endif
    next = ApplyPostOrderGates(data, control_data, n, block_size, 0,
                               GenericGate());

  if (next != postorder_controls.size())
    throw std::logic_error("OFR postorder control consumption mismatch");
}

void OFRApplyWordsInPlace(
    size_t *rows, size_t words_per_row,
    const PackedControls &controls, size_t position,
    size_t n, size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  size_t row_words = 0, row_bytes = 0;
  if (rows == NULL || words_per_row == 0 ||
      MulOverflowSize(n, words_per_row, &row_words) ||
      MulOverflowSize(row_words, sizeof(size_t), &row_bytes))
    throw std::invalid_argument("Invalid OFR membership dimensions");
  (void)row_words;
  (void)row_bytes;
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR membership replay exceeds control tape");
  const size_t next = OFRApplyWordsStrided(
      rows, words_per_row,
      controls.view(position, required),
      n, n_left, n_right, 1, 0);
  if (next != required)
    throw std::logic_error("OFR membership replay count mismatch");
}

OFRControlReadResult OFRControlRead(const unsigned char *data,
                                    const PackedControls &controls,
                                    size_t n,
                                    size_t n_left,
                                    size_t n_right,
                                    size_t block_size,
                                    size_t position)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid OFR data dimensions");
  (void)data_bytes;

  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR control read exceeds source array");

  std::vector<unsigned char> work(data, data + data_bytes);
  const size_t next = OFRApplyStrided(
      work.data(), controls.view(position, required),
      n, n_left, n_right, block_size, 1, 0);
  if (next != required)
    throw std::logic_error("OFR control consumption mismatch");
  OFRControlReadResult result;
  const size_t left_bytes = n_left * block_size;
  result.left.assign(work.begin(), work.begin() + left_bytes);
  result.right.assign(work.begin() + left_bytes, work.end());
  result.next_pos = position + next;
  return result;
}

OFRDataResult OFRApply(const unsigned char *data,
                       const PackedControls &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size)
{
  const size_t required = OFRControlCount(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("OFR control array length mismatch");
  OFRControlReadResult read = OFRControlRead(
      data, controls, n, n_left, n_right, block_size, 0);
  if (read.next_pos != required)
    throw std::logic_error("OFR control consumption mismatch");
  OFRDataResult result;
  result.left.swap(read.left);
  result.right.swap(read.right);
  return result;
}

OFRDataResult OFRCompact(const unsigned char *data,
                         const std::vector<uint8_t> &tags,
                         size_t n,
                         size_t n_left,
                         size_t n_right,
                         size_t block_size)
{
  const PackedControls controls = OFRControl(
      tags, n, n_left, n_right);
  return OFRApply(
      data, controls, n, n_left, n_right, block_size);
}

} // namespace ofr
