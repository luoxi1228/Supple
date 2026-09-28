#include "OFR.hpp"
#include "helper.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifndef BEFTS_MODE
#include "../../oasm_lib.h"
#endif

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

size_t OFRBalanceInPlaceImpl(uint8_t *tags, std::vector<uint8_t> &controls,
                             size_t base, size_t stride, size_t n,
                             size_t n_left, size_t n_right, size_t position);

size_t OFRControlWriteImpl(uint8_t *tags, std::vector<uint8_t> &controls,
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
      tags, controls, base, stride, n, n_left, n_right, position);

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = OFRControlWriteImpl(tags, controls, base, stride * 2,
                                 split.n_top,
                                 split.top_left,
                                 split.top_right,
                                 position);
  return OFRControlWriteImpl(tags, controls, base + stride, stride * 2,
                             split.n_bottom,
                             split.bottom_left,
                             split.bottom_right,
                             position);
}

// The two recursive children occupy the even and odd record lanes. Recurse
// with a doubled stride instead of materializing either child in a vector.
size_t OFRApplyStrided(unsigned char *data,
                       const std::vector<uint8_t> &controls,
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
  if (n == 2)
  {
    detail::ApplyOFork(data, data + byte_stride, block_size, controls[position]);
    return position + 1;
  }

  const size_t gate_count = n / 2;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    unsigned char *first = data + (2 * gate) * byte_stride;
    detail::ApplyOFork(first,
                       first + byte_stride,
                       block_size,
                       controls[position + gate]);
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

bool IsBalancedPowerOfTwo(size_t n, size_t n_left, size_t n_right)
{
  return n >= 2 && (n & (n - 1)) == 0 &&
         n_left == n / 2 && n_right == n / 2;
}

size_t WriteLevelOrderControls(const std::vector<uint8_t> &source,
                               std::vector<uint8_t> *destination,
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

size_t BuildPostOrderControlStarts(size_t length,
                                   size_t node,
                                   size_t position,
                                   std::vector<size_t> *starts)
{
  if (length > 2)
  {
    position = BuildPostOrderControlStarts(length / 2, node * 2,
                                            position, starts);
    position = BuildPostOrderControlStarts(length / 2, node * 2 + 1,
                                            position, starts);
  }
  (*starts)[node] = position;
  return position + length / 2;
}

// Emit the contiguous-block postorder directly from the DFS tape. The public
// gate geometry determines every source position, so no level-order tape is
// materialized.
size_t WritePostOrderFromDfs(const std::vector<uint8_t> &source,
                             const std::vector<size_t> &starts,
                             std::vector<uint8_t> *destination,
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
void ApplyLevelOrderedGates(unsigned char *data,
                            const uint8_t *controls,
                            size_t n,
                            size_t block_size,
                            const Gate &apply_gate)
{
  size_t position = 0;
  for (size_t stride = 1; stride < n; stride *= 2)
  {
    for (size_t base = 0; base < n; base += stride * 2)
    {
      unsigned char *first = data + base * block_size;
      unsigned char *second = first + stride * block_size;
      for (size_t offset = 0; offset < stride; ++offset)
      {
        apply_gate(first, second, block_size, controls[position++]);
        first += block_size;
        second += block_size;
      }
    }
  }
}

template <typename Gate>
size_t ApplyPostOrderGates(unsigned char *data,
                           const uint8_t *controls,
                           size_t n,
                           size_t block_size,
                           size_t position,
                           const Gate &apply_gate)
{
  const size_t half = n / 2;
  if (n > 2)
  {
    position = ApplyPostOrderGates(data, controls, half, block_size,
                                   position, apply_gate);
    position = ApplyPostOrderGates(data + half * block_size, controls,
                                   half, block_size, position, apply_gate);
  }
  unsigned char *first = data;
  unsigned char *second = data + half * block_size;
  for (size_t i = 0; i < half; ++i)
  {
    apply_gate(first, second, block_size, controls[position++]);
    first += block_size;
    second += block_size;
  }
  return position;
}

#ifndef BEFTS_MODE
// Keep the hot loop equivalent to the original OFork implementation: choose
// the assembly specialization once and read controls through a raw pointer.
template <OFork_Style style>
void ApplyLevelOrderedWithStyle(unsigned char *data,
                                const uint8_t *controls,
                                size_t n,
                                size_t block_size)
{
  size_t position = 0;
  for (size_t stride = 1; stride < n; stride <<= 1U)
  {
    for (size_t base = 0; base < n; base += stride * 2)
    {
      unsigned char *first = data + base * block_size;
      unsigned char *second = first + stride * block_size;
      for (size_t offset = 0; offset < stride; ++offset)
      {
        const uint8_t control = controls[position++];
        const uint8_t left_flag = static_cast<uint8_t>((control >> 1U) & 1U);
        const uint8_t right_flag = static_cast<uint8_t>(control & 1U);
        ofork_buffer<style>(first,
                            second,
                            static_cast<uint32_t>(block_size),
                            left_flag,
                            right_flag);
        first += block_size;
        second += block_size;
      }
    }
  }
}

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

std::vector<uint8_t> OFRNormalize(const std::vector<uint8_t> &tags,
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

  const size_t assign_left = n_left - left_weight;
  const size_t assign_right = n_right - right_weight;
  std::vector<uint8_t> normalized(tags);
  size_t zero_rank = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const uint8_t is_zero = CtEqualU8(tags[i], OFR_TAG_ZERO);
    const uint8_t use_left = static_cast<uint8_t>(
        is_zero & CtLessSize(zero_rank, assign_left));
    const uint8_t after_left = static_cast<uint8_t>(
        CtLessSize(zero_rank, assign_left) ^ 1U);
    const uint8_t before_end = CtLessSize(
        zero_rank, assign_left + assign_right);
    const uint8_t use_right = static_cast<uint8_t>(
        is_zero & after_left & before_end);

    uint8_t tag = normalized[i];
    tag = CtSelectU8(tag, OFR_TAG_LEFT, use_left);
    tag = CtSelectU8(tag, OFR_TAG_RIGHT, use_right);
    normalized[i] = tag;
    zero_rank += is_zero;
  }
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

template <bool PostOrder>
size_t OFRBalanceLayer(uint8_t *tags, std::vector<uint8_t> &controls,
                       uint8_t *scratch, const size_t *postorder_starts,
                       size_t root_n, size_t base, size_t stride, size_t n,
                       size_t n_left, size_t n_right, size_t position)
{
  const size_t gate_count = n / 2;
  uint8_t *features = PostOrder ? scratch : controls.data() + position;
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
    // The DFS path reuses its destination bytes; direct postorder generation
    // reuses one scratch layer shared by all recursive views.
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
      features[gate] = control;
    tags[first] = top_tag;
    tags[first + stride] = bottom_tag;
    rank_diagonal += diagonal_type;
    rank_left += left_variable_type;
    rank_right += right_variable_type;
  }

  return position + gate_count;
}

size_t OFRBalanceInPlaceImpl(uint8_t *tags, std::vector<uint8_t> &controls,
                             size_t base, size_t stride, size_t n,
                             size_t n_left, size_t n_right, size_t position)
{
  return OFRBalanceLayer<false>(tags, controls, NULL, NULL, 0,
                                base, stride, n, n_left, n_right, position);
}

void OFRControlPostOrderImpl(uint8_t *tags,
                             std::vector<uint8_t> &controls,
                             uint8_t *scratch,
                             const size_t *postorder_starts,
                             size_t root_n,
                             size_t base,
                             size_t stride,
                             size_t n)
{
  if (n == 2)
  {
    controls[postorder_starts[1] + base] =
        static_cast<uint8_t>(OFR_TAG_BOTH ^ tags[base]);
    return;
  }

  OFRBalanceLayer<true>(tags, controls, scratch, postorder_starts, root_n,
                        base, stride, n, n / 2, n / 2, 0);
  OFRControlPostOrderImpl(tags, controls, scratch, postorder_starts,
                          root_n, base, stride * 2, n / 2);
  OFRControlPostOrderImpl(tags, controls, scratch, postorder_starts,
                          root_n, base + stride, stride * 2, n / 2);
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
  OFRBalanceInPlaceImpl(work.data(), result.controls, 0, 1, n,
                        n_left, n_right, 0);
  result.top_tags.reserve(n / 2 + n % 2);
  result.bottom_tags.reserve(n / 2);
  for (size_t i = 0; i < n; ++i)
    (i % 2 == 0 ? result.top_tags : result.bottom_tags).push_back(work[i]);
  return result;
}

size_t OFRBalanceInPlace(std::vector<uint8_t> &tags,
                         std::vector<uint8_t> &controls,
                         size_t base, size_t stride, size_t n,
                         size_t n_left, size_t n_right, size_t position)
{
  if (n < 3)
    throw std::invalid_argument("OFR balance requires at least three tags");
  ValidateTagView(tags, base, stride, n, n_left, n_right);
  const size_t required = n / 2;
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR balance exceeds control array");
  return OFRBalanceInPlaceImpl(tags.data(), controls, base, stride, n,
                               n_left, n_right, position);
}

size_t OFRControlCount(size_t n, size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  return OFRControlCountImpl(n, n_left, n_right);
}

size_t OFRControlWrite(const std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
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
  return OFRControlWriteImpl(
      work.data(), controls, 0, 1, n, n_left, n_right, position);
}

size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t base, size_t stride, size_t n,
                       size_t n_left, size_t n_right, size_t position)
{
  ValidateTagView(tags, base, stride, n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("OFR control write exceeds destination array");
  return OFRControlWriteImpl(tags.data(), controls, base, stride, n,
                             n_left, n_right, position);
}

size_t OFRControlWrite(std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t n, size_t n_left, size_t n_right,
                       size_t position)
{
  return OFRControlWrite(tags, controls, 0, 1, n,
                         n_left, n_right, position);
}

std::vector<uint8_t> OFRControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right)
{
  std::vector<uint8_t> normalized = OFRNormalize(
      tags, n, n_left, n_right);
  std::vector<uint8_t> controls(
      OFRControlCountImpl(n, n_left, n_right));
  const size_t next = OFRControlWriteImpl(
      normalized.data(), controls, 0, 1, n, n_left, n_right, 0);
  if (next != controls.size())
    throw std::logic_error("OFR control write/count mismatch");
  return controls;
}

std::vector<uint8_t> OFRControlPostOrder(const std::vector<uint8_t> &tags,
                                         size_t n,
                                         size_t n_left,
                                         size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Postorder OFR requires balanced power-of-two capacities");
  // Direct DFS-time placement has scattered writes. Once the tape outgrows
  // cache, a sequential DFS tape followed by conversion is faster. The
  // choice depends only on the public network size.
  const size_t direct_limit = size_t(1) << 17U;
  if (n > direct_limit)
    return OFRPostOrderControls(
        OFRControl(tags, n, n_left, n_right), n, n_left, n_right);
  std::vector<uint8_t> normalized = OFRNormalize(
      tags, n, n_left, n_right);
  const size_t required = OFRControlCountImpl(n, n_left, n_right);
  std::vector<size_t> starts(n);
  if (BuildPostOrderControlStarts(n, 1, 0, &starts) != required)
    throw std::logic_error("OFR postorder control layout mismatch");
  std::vector<uint8_t> controls(required);
  std::vector<uint8_t> scratch(n / 2);
  OFRControlPostOrderImpl(normalized.data(), controls, scratch.data(),
                          starts.data(), n, 0, 1, n);
  return controls;
}

std::vector<uint8_t> OFRLevelOrderControls(
    const std::vector<uint8_t> &controls,
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

  std::vector<uint8_t> level_controls(required);
  const size_t next = WriteLevelOrderControls(
      controls, &level_controls, n, n / 2, 0, 1, 0, 0);
  if (next != required)
    throw std::logic_error("OFR level-order conversion mismatch");
  return level_controls;
}

std::vector<uint8_t> OFRPostOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right)
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
  std::vector<uint8_t> postorder_controls(required);
  const size_t next = WritePostOrderFromDfs(
      controls, starts, &postorder_controls, 0, n, 0);
  if (next != postorder_controls.size())
    throw std::logic_error("OFR postorder conversion mismatch");
  return postorder_controls;
}

void OFRApplyInPlace(unsigned char *data,
                     const std::vector<uint8_t> &controls,
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
                             const std::vector<uint8_t> &controls,
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

  const size_t next = OFRApplyStrided(
      data, controls, n, n_left, n_right, block_size, 1, 0);
  if (next != controls.size())
    throw std::logic_error("OFR control consumption mismatch");
}

void OFRApplyLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
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
    const std::vector<uint8_t> &level_controls,
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

#ifndef BEFTS_MODE
  if (block_size == 4)
    ApplyLevelOrderedWithStyle<OFORK_4>(
        data, level_controls.data(), n, block_size);
  else if (block_size == 8)
    ApplyLevelOrderedWithStyle<OFORK_8>(
        data, level_controls.data(), n, block_size);
  else if (block_size == 12)
    ApplyLevelOrderedWithStyle<OFORK_12>(
        data, level_controls.data(), n, block_size);
  else if (block_size == 16)
    ApplyLevelOrderedWithStyle<OFORK_16>(
        data, level_controls.data(), n, block_size);
  else if (block_size == 24)
    ApplyLevelOrderedWithStyle<OFORK_24>(
        data, level_controls.data(), n, block_size);
  else if (block_size >= 16 && block_size % 16 == 0)
    ApplyLevelOrderedWithStyle<OFORK_16X>(
        data, level_controls.data(), n, block_size);
  else if (block_size >= 24 && block_size % 16 == 8)
    ApplyLevelOrderedWithStyle<OFORK_8_16X>(
        data, level_controls.data(), n, block_size);
  else
#endif
    ApplyLevelOrderedGates(data, level_controls.data(), n, block_size,
                           GenericGate());

}

void OFRApplyPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
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
    const std::vector<uint8_t> &postorder_controls,
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
    throw std::invalid_argument("Invalid postorder OFR dimensions");
  (void)data_bytes;

  const uint8_t *control_data = postorder_controls.data();
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

OFRControlReadResult OFRControlRead(const unsigned char *data,
                                    const std::vector<uint8_t> &controls,
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
  uint8_t invalid_control = 0;
  for (size_t i = 0; i < required; ++i)
    invalid_control = static_cast<uint8_t>(
        invalid_control | (controls[position + i] >> 2U));
  if (invalid_control != 0)
    throw std::invalid_argument("Invalid OFR control word");

  std::vector<unsigned char> work(data, data + data_bytes);
  const size_t next = OFRApplyStrided(
      work.data(), controls, n, n_left, n_right, block_size, 1, position);
  if (next != position + required)
    throw std::logic_error("OFR control consumption mismatch");
  OFRControlReadResult result;
  const size_t left_bytes = n_left * block_size;
  result.left.assign(work.begin(), work.begin() + left_bytes);
  result.right.assign(work.begin() + left_bytes, work.end());
  result.next_pos = next;
  return result;
}

OFRDataResult OFRApply(const unsigned char *data,
                       const std::vector<uint8_t> &controls,
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
  const std::vector<uint8_t> controls = OFRControl(
      tags, n, n_left, n_right);
  return OFRApply(
      data, controls, n, n_left, n_right, block_size);
}

} // namespace ofr
