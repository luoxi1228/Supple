#include "FMS.hpp"
#include "helper.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifndef BEFTS_MODE
#include "../../oasm_lib.h"
#endif

namespace fms
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

size_t FMSControlCountImpl(size_t n, size_t n_left, size_t n_right)
{
  if (n_left == 0 || n_right == 0)
    return 0;
  if (n == 2)
    return 1;

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  const size_t top_count = FMSControlCountImpl(
      split.n_top, split.top_left, split.top_right);
  const size_t bottom_count = FMSControlCountImpl(
      split.n_bottom, split.bottom_left, split.bottom_right);

  size_t result = 0;
  if (AddOverflowSize(n / 2, top_count, &result) ||
      AddOverflowSize(result, bottom_count, &result))
    throw std::length_error("FMS control count overflow");
  return result;
}

size_t FMSControlWriteImpl(const std::vector<uint8_t> &tags,
                           std::vector<uint8_t> &controls,
                           size_t n,
                           size_t n_left,
                           size_t n_right,
                           size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;

  if (n == 2)
  {
    controls[position] = OForkControl(tags[0],
                                      tags[1],
                                      FMS_TAG_LEFT,
                                      FMS_TAG_RIGHT);
    return position + 1;
  }

  const FMSBalanceResult balanced = FMSBalance(
      tags, n, n_left, n_right);
  std::copy(balanced.controls.begin(),
            balanced.controls.end(),
            controls.begin() + position);
  position += n / 2;

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  position = FMSControlWriteImpl(balanced.top_tags,
                                 controls,
                                 split.n_top,
                                 split.top_left,
                                 split.top_right,
                                 position);
  return FMSControlWriteImpl(balanced.bottom_tags,
                             controls,
                             split.n_bottom,
                             split.bottom_left,
                             split.bottom_right,
                             position);
}

// The two recursive children occupy the even and odd record lanes. Recurse
// with a doubled stride instead of materializing either child in a vector.
size_t FMSApplyStrided(unsigned char *data,
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
  position = FMSApplyStrided(data,
                             controls,
                             split.n_top,
                             split.top_left,
                             split.top_right,
                             block_size,
                             stride * 2,
                             position);
  return FMSApplyStrided(data + byte_stride,
                          controls,
                          split.n_bottom,
                          split.bottom_left,
                          split.bottom_right,
                          block_size,
                          stride * 2,
                          position);
}

// Build the public permutation from strided lanes to the left/right DFS
// concatenation order specified by FMSControlRead.
void BuildOutputDestinations(size_t n,
                             size_t n_left,
                             size_t n_right,
                             size_t base,
                             size_t stride,
                             size_t left_base,
                             size_t right_base,
                             std::vector<size_t> *destinations)
{
  if (n_left == 0 || n_right == 0)
  {
    const size_t output_base = n_left == 0 ? right_base : left_base;
    for (size_t i = 0; i < n; ++i)
      (*destinations)[base + i * stride] = output_base + i;
    return;
  }
  if (n == 2)
  {
    (*destinations)[base] = left_base;
    (*destinations)[base + stride] = right_base;
    return;
  }

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  BuildOutputDestinations(split.n_top,
                          split.top_left,
                          split.top_right,
                          base,
                          stride * 2,
                          left_base,
                          right_base,
                          destinations);
  BuildOutputDestinations(split.n_bottom,
                          split.bottom_left,
                          split.bottom_right,
                          base + stride,
                          stride * 2,
                          left_base + split.top_left,
                          right_base + split.top_right,
                          destinations);
}

void ApplyOutputSwaps(unsigned char *data,
                      size_t n,
                      size_t block_size,
                      const std::vector<FMSOutputSwap> &swaps)
{
  for (size_t i = 0; i < swaps.size(); ++i)
  {
    const FMSOutputSwap &swap = swaps[i];
    if (swap.first >= n || swap.second >= n)
      throw std::invalid_argument("Invalid FMS output swap");
    unsigned char *first = data + swap.first * block_size;
    unsigned char *second = data + swap.second * block_size;
    for (size_t byte = 0; byte < block_size; ++byte)
      std::swap(first[byte], second[byte]);
  }
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

// The level-order tape identifies a gate by its butterfly stage and its
// position within that stage. Emit the same gates in the contiguous-block
// postorder used by TightCompact_2power_inner.
size_t WritePostOrderControls(const std::vector<uint8_t> &level_controls,
                              std::vector<uint8_t> *destination,
                              size_t base,
                              size_t length,
                              size_t stage,
                              size_t root_half,
                              size_t position)
{
  const size_t half = length / 2;
  if (length > 2)
  {
    position = WritePostOrderControls(level_controls, destination, base,
                                      half, stage - 1, root_half, position);
    position = WritePostOrderControls(level_controls, destination, base + half,
                                      half, stage - 1, root_half, position);
  }
  const size_t source = stage * root_half + base / 2;
  std::copy(level_controls.begin() + source,
            level_controls.begin() + source + half,
            destination->begin() + position);
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

std::array<uint8_t, 10> FMSPairMask(uint8_t x, uint8_t y)
{
  const uint8_t x_zero = CtEqualU8(x, FMS_TAG_ZERO);
  const uint8_t x_left = CtEqualU8(x, FMS_TAG_LEFT);
  const uint8_t x_right = CtEqualU8(x, FMS_TAG_RIGHT);
  const uint8_t x_both = CtEqualU8(x, FMS_TAG_BOTH);
  const uint8_t y_zero = CtEqualU8(y, FMS_TAG_ZERO);
  const uint8_t y_left = CtEqualU8(y, FMS_TAG_LEFT);
  const uint8_t y_right = CtEqualU8(y, FMS_TAG_RIGHT);
  const uint8_t y_both = CtEqualU8(y, FMS_TAG_BOTH);

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
      CtEqualU8(x, FMS_TAG_BOTH) & CtEqualU8(y, FMS_TAG_ZERO));
  const uint8_t copy_y = static_cast<uint8_t>(
      CtEqualU8(x, FMS_TAG_ZERO) & CtEqualU8(y, FMS_TAG_BOTH));
  const uint8_t copy_pair = static_cast<uint8_t>(copy_x | copy_y);
  const uint8_t left_right = static_cast<uint8_t>(
      CtEqualU8(top_tag, FMS_TAG_LEFT) &
      CtEqualU8(bottom_tag, FMS_TAG_RIGHT));
  const uint8_t right_left = static_cast<uint8_t>(
      CtEqualU8(top_tag, FMS_TAG_RIGHT) &
      CtEqualU8(bottom_tag, FMS_TAG_LEFT));
  const uint8_t fork = static_cast<uint8_t>(
      copy_pair & (left_right | right_left));

  const uint8_t use_cross = static_cast<uint8_t>(cross & (straight ^ 1U));
  const uint8_t use_fork = static_cast<uint8_t>(
      fork & (straight ^ 1U) & (cross ^ 1U));
  const uint8_t fork_control = static_cast<uint8_t>((copy_y << 1U) | copy_y);

  uint8_t control = FMS_STRAIGHT;
  control = CtSelectU8(control, FMS_SWAP, use_cross);
  control = CtSelectU8(control, fork_control, use_fork);
  return control;
}

std::vector<uint8_t> FMSNormalize(const std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right)
{
  ValidateTags(tags, n, n_left, n_right, false);

  size_t left_weight = 0;
  size_t right_weight = 0;
  for (size_t i = 0; i < n; ++i)
  {
    left_weight += LeftBit(tags[i]);
    right_weight += RightBit(tags[i]);
  }

  const size_t assign_left = n_left - left_weight;
  const size_t assign_right = n_right - right_weight;
  std::vector<uint8_t> normalized(tags);
  size_t zero_rank = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const uint8_t is_zero = CtEqualU8(tags[i], FMS_TAG_ZERO);
    const uint8_t use_left = static_cast<uint8_t>(
        is_zero & CtLessSize(zero_rank, assign_left));
    const uint8_t after_left = static_cast<uint8_t>(
        CtLessSize(zero_rank, assign_left) ^ 1U);
    const uint8_t before_end = CtLessSize(
        zero_rank, assign_left + assign_right);
    const uint8_t use_right = static_cast<uint8_t>(
        is_zero & after_left & before_end);

    uint8_t tag = normalized[i];
    tag = CtSelectU8(tag, FMS_TAG_LEFT, use_left);
    tag = CtSelectU8(tag, FMS_TAG_RIGHT, use_right);
    normalized[i] = tag;
    zero_rank += is_zero;
  }
  return normalized;
}

FMSBalanceResult FMSBalance(const std::vector<uint8_t> &tags,
                            size_t n,
                            size_t n_left,
                            size_t n_right)
{
  ValidateTags(tags, n, n_left, n_right, true);
  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  const size_t gate_count = n / 2;

  size_t histogram[10] = {};
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const std::array<uint8_t, 10> pair = FMSPairMask(
        tags[2 * gate], tags[2 * gate + 1]);
    for (size_t type = 0; type < 10; ++type)
      histogram[type] += pair[type];
  }

  const int64_t e_left = (n & 1U) != 0 ? LeftBit(tags[n - 1]) : 0;
  const int64_t e_right = (n & 1U) != 0 ? RightBit(tags[n - 1]) : 0;
  const int64_t target_left = static_cast<int64_t>(split.top_left) - e_left;
  const int64_t target_right = static_cast<int64_t>(split.top_right) - e_right;
  const int64_t diagonal = static_cast<int64_t>(histogram[5]);
  const int64_t variable_left = static_cast<int64_t>(
      histogram[1] + histogram[3] + histogram[8]);
  const int64_t variable_right = static_cast<int64_t>(
      histogram[2] + histogram[3] + histogram[6]);
  const int64_t fixed_left = static_cast<int64_t>(
      histogram[4] + histogram[6] + histogram[9]);
  const int64_t fixed_right = static_cast<int64_t>(
      histogram[7] + histogram[8] + histogram[9]);

  int64_t quota_diagonal_min = CtMaxI64(
      0, target_left - fixed_left - variable_left);
  quota_diagonal_min = CtMaxI64(
      quota_diagonal_min, fixed_right + diagonal - target_right);
  int64_t quota_diagonal_max = CtMinI64(
      diagonal, target_left - fixed_left);
  quota_diagonal_max = CtMinI64(
      quota_diagonal_max,
      fixed_right + diagonal + variable_right - target_right);
  const int64_t quota_diagonal = CtMinI64(
      quota_diagonal_max,
      CtMaxI64(quota_diagonal_min, diagonal / 2));
  const int64_t quota_left =
      target_left - fixed_left - quota_diagonal;
  const int64_t quota_right =
      target_right - fixed_right - diagonal + quota_diagonal;

  FMSBalanceResult result;
  result.controls.resize(gate_count);
  result.top_tags.resize(split.n_top);
  result.bottom_tags.resize(split.n_bottom);
  size_t rank_diagonal = 0;
  size_t rank_left = 0;
  size_t rank_right = 0;

  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const uint8_t x = tags[2 * gate];
    const uint8_t y = tags[2 * gate + 1];
    const std::array<uint8_t, 10> pair = FMSPairMask(x, y);
    const uint8_t diagonal_type = pair[5];
    const uint8_t left_variable_type = static_cast<uint8_t>(
        pair[1] | pair[3] | pair[8]);
    const uint8_t right_variable_type = static_cast<uint8_t>(
        pair[2] | pair[3] | pair[6]);

    const uint8_t use_diagonal = static_cast<uint8_t>(
        diagonal_type & CtLessSize(
            rank_diagonal, static_cast<size_t>(quota_diagonal)));
    const uint8_t use_left = static_cast<uint8_t>(
        left_variable_type & CtLessSize(
            rank_left, static_cast<size_t>(quota_left)));
    const uint8_t use_right = static_cast<uint8_t>(
        right_variable_type & CtLessSize(
            rank_right, static_cast<size_t>(quota_right)));
    const uint8_t top_left_fixed = static_cast<uint8_t>(
        pair[4] | pair[6] | pair[9]);
    const uint8_t top_right_fixed = static_cast<uint8_t>(
        pair[7] | pair[8] | pair[9]);
    const uint8_t top_left = static_cast<uint8_t>(
        top_left_fixed | use_diagonal | use_left);
    const uint8_t top_right = static_cast<uint8_t>(
        top_right_fixed |
        (diagonal_type & (use_diagonal ^ 1U)) |
        use_right);
    const uint8_t bottom_left = static_cast<uint8_t>(
        LeftBit(x) + LeftBit(y) - top_left);
    const uint8_t bottom_right = static_cast<uint8_t>(
        RightBit(x) + RightBit(y) - top_right);
    const uint8_t top_tag = static_cast<uint8_t>(
        (top_left << 1U) | top_right);
    const uint8_t bottom_tag = static_cast<uint8_t>(
        (bottom_left << 1U) | bottom_right);

    result.controls[gate] = OForkControl(x, y, top_tag, bottom_tag);
    result.top_tags[gate] = top_tag;
    result.bottom_tags[gate] = bottom_tag;
    rank_diagonal += diagonal_type;
    rank_left += left_variable_type;
    rank_right += right_variable_type;
  }

  if ((n & 1U) != 0)
    result.top_tags[gate_count] = tags[n - 1];
  return result;
}

size_t FMSControlCount(size_t n, size_t n_left, size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  return FMSControlCountImpl(n, n_left, n_right);
}

size_t FMSControlWrite(const std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position)
{
  ValidateTags(tags, n, n_left, n_right, true);
  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("FMS control write exceeds destination array");
  return FMSControlWriteImpl(
      tags, controls, n, n_left, n_right, position);
}

std::vector<uint8_t> FMSControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right)
{
  ValidateTags(tags, n, n_left, n_right, false);
  std::vector<uint8_t> normalized = FMSNormalize(
      tags, n, n_left, n_right);
  std::vector<uint8_t> controls(
      FMSControlCountImpl(n, n_left, n_right));
  const size_t next = FMSControlWriteImpl(
      normalized, controls, n, n_left, n_right, 0);
  if (next != controls.size())
    throw std::logic_error("FMS control write/count mismatch");
  return controls;
}

std::vector<FMSOutputSwap> FMSOutputSwaps(size_t n,
                                           size_t n_left,
                                           size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  std::vector<size_t> destinations(n);
  BuildOutputDestinations(n, n_left, n_right, 0, 1, 0, n_left,
                          &destinations);

  std::vector<uint8_t> visited(n, 0);
  std::vector<FMSOutputSwap> swaps;
  swaps.reserve(n - 1);
  for (size_t first = 0; first < n; ++first)
  {
    if (visited[first] != 0)
      continue;
    visited[first] = 1;
    size_t next = destinations[first];
    while (next != first)
    {
      if (next >= n || visited[next] != 0)
        throw std::logic_error("Invalid FMS output permutation");
      swaps.push_back(FMSOutputSwap{first, next});
      visited[next] = 1;
      next = destinations[next];
    }
  }
  return swaps;
}

std::vector<uint8_t> FMSLevelOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right)
{
  ValidateCapacities(n, n_left, n_right);
  if (!IsBalancedPowerOfTwo(n, n_left, n_right))
    throw std::invalid_argument("Level-order FMS requires balanced power-of-two capacities");
  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("FMS control array length mismatch");

  std::vector<uint8_t> level_controls(required);
  const size_t next = WriteLevelOrderControls(
      controls, &level_controls, n, n / 2, 0, 1, 0, 0);
  if (next != required)
    throw std::logic_error("FMS level-order conversion mismatch");
  return level_controls;
}

std::vector<uint8_t> FMSPostOrderControls(
    const std::vector<uint8_t> &controls,
    size_t n,
    size_t n_left,
    size_t n_right)
{
  const std::vector<uint8_t> level_controls =
      FMSLevelOrderControls(controls, n, n_left, n_right);
  size_t stage = 0;
  for (size_t length = n; length > 2; length /= 2)
    ++stage;
  std::vector<uint8_t> postorder_controls(level_controls.size());
  const size_t next = WritePostOrderControls(
      level_controls, &postorder_controls, 0, n, stage, n / 2, 0);
  if (next != postorder_controls.size())
    throw std::logic_error("FMS postorder conversion mismatch");
  return postorder_controls;
}

void FMSApplyInPlace(unsigned char *data,
                     const std::vector<uint8_t> &controls,
                     const std::vector<FMSOutputSwap> &output_swaps,
                     size_t n,
                     size_t n_left,
                     size_t n_right,
                     size_t block_size,
                     bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("FMS control array length mismatch");
  FMSApplyPreparedInPlace(data, controls, output_swaps, n, n_left, n_right,
                          block_size, apply_output_swaps);
}

void FMSApplyPreparedInPlace(unsigned char *data,
                             const std::vector<uint8_t> &controls,
                             const std::vector<FMSOutputSwap> &output_swaps,
                             size_t n,
                             size_t n_left,
                             size_t n_right,
                             size_t block_size,
                             bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid FMS data dimensions");
  (void)data_bytes;

  const size_t next = FMSApplyStrided(
      data, controls, n, n_left, n_right, block_size, 1, 0);
  if (next != controls.size())
    throw std::logic_error("FMS control consumption mismatch");
  if (apply_output_swaps)
    ApplyOutputSwaps(data, n, block_size, output_swaps);
}

void FMSApplyLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (level_controls.size() != required)
    throw std::length_error("FMS control array length mismatch");
  FMSApplyPreparedLevelOrderedInPlace(
      data, level_controls, output_swaps, n, n_left, n_right,
      block_size, apply_output_swaps);
}

void FMSApplyPreparedLevelOrderedInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &level_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (!IsBalancedPowerOfTwo(n, n_left, n_right) ||
      data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid level-order FMS dimensions");
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

  if (apply_output_swaps)
    ApplyOutputSwaps(data, n, block_size, output_swaps);
}

void FMSApplyPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (postorder_controls.size() != required)
    throw std::length_error("FMS control array length mismatch");
  FMSApplyPreparedPostOrderInPlace(
      data, postorder_controls, output_swaps, n, n_left, n_right,
      block_size, apply_output_swaps);
}

void FMSApplyPreparedPostOrderInPlace(
    unsigned char *data,
    const std::vector<uint8_t> &postorder_controls,
    const std::vector<FMSOutputSwap> &output_swaps,
    size_t n,
    size_t n_left,
    size_t n_right,
    size_t block_size,
    bool apply_output_swaps)
{
  ValidateCapacities(n, n_left, n_right);
  size_t data_bytes = 0;
  if (!IsBalancedPowerOfTwo(n, n_left, n_right) ||
      data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid postorder FMS dimensions");
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
    throw std::logic_error("FMS postorder control consumption mismatch");
  if (apply_output_swaps)
    ApplyOutputSwaps(data, n, block_size, output_swaps);
}

void FMSApplyOutputSwapsInPlace(
    unsigned char *data,
    size_t n,
    size_t block_size,
    const std::vector<FMSOutputSwap> &output_swaps)
{
  size_t data_bytes = 0;
  if (data == NULL || block_size == 0 ||
      MulOverflowSize(n, block_size, &data_bytes))
    throw std::invalid_argument("Invalid FMS data dimensions");
  (void)data_bytes;
  ApplyOutputSwaps(data, n, block_size, output_swaps);
}

FMSControlReadResult FMSControlRead(const unsigned char *data,
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
    throw std::invalid_argument("Invalid FMS data dimensions");
  (void)data_bytes;

  const size_t required = FMSControlCountImpl(n, n_left, n_right);
  if (position > controls.size() || required > controls.size() - position)
    throw std::length_error("FMS control read exceeds source array");
  uint8_t invalid_control = 0;
  for (size_t i = 0; i < required; ++i)
    invalid_control = static_cast<uint8_t>(
        invalid_control | (controls[position + i] >> 2U));
  if (invalid_control != 0)
    throw std::invalid_argument("Invalid FMS control word");

  std::vector<unsigned char> work(data, data + data_bytes);
  const size_t next = FMSApplyStrided(
      work.data(), controls, n, n_left, n_right, block_size, 1, position);
  if (next != position + required)
    throw std::logic_error("FMS control consumption mismatch");
  const std::vector<FMSOutputSwap> output_swaps =
      FMSOutputSwaps(n, n_left, n_right);
  ApplyOutputSwaps(work.data(), n, block_size, output_swaps);

  FMSControlReadResult result;
  const size_t left_bytes = n_left * block_size;
  result.left.assign(work.begin(), work.begin() + left_bytes);
  result.right.assign(work.begin() + left_bytes, work.end());
  result.next_pos = next;
  return result;
}

FMSDataResult FMSApply(const unsigned char *data,
                       const std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size)
{
  const size_t required = FMSControlCount(n, n_left, n_right);
  if (controls.size() != required)
    throw std::length_error("FMS control array length mismatch");
  FMSControlReadResult read = FMSControlRead(
      data, controls, n, n_left, n_right, block_size, 0);
  if (read.next_pos != required)
    throw std::logic_error("FMS control consumption mismatch");
  FMSDataResult result;
  result.left.swap(read.left);
  result.right.swap(read.right);
  return result;
}

FMSDataResult FMSCompact(const unsigned char *data,
                         const std::vector<uint8_t> &tags,
                         size_t n,
                         size_t n_left,
                         size_t n_right,
                         size_t block_size)
{
  const std::vector<uint8_t> controls = FMSControl(
      tags, n, n_left, n_right);
  return FMSApply(
      data, controls, n, n_left, n_right, block_size);
}

} // namespace fms
