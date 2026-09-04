#include "OFork.hpp"
#include "utill.hpp"

#include <limits>
#include <new>

#ifndef BEFTS_MODE
#include "Enclave_t.h"
#include "../ObliviousPrimitives.hpp"
#endif

using ofork_internal::ApplyLevelOrdered;
using ofork_internal::CtEqualU8;
using ofork_internal::CtLessSize;
using ofork_internal::CtSelectU8;
using ofork_internal::LeftBit;
using ofork_internal::MulOverflowSize;
using ofork_internal::RightBit;
using ofork_internal::SupportedBlockSize;
using ofork_internal::WriteLevelOrderedControls;

bool FMSIsPowerOfTwo(size_t n)
{
  return n >= 2 && (n & (n - 1)) == 0;
}

size_t FMSControlNum(size_t n)
{
  if (!FMSIsPowerOfTwo(n))
  {
    return 0;
  }

  const size_t gates_per_level = n / 2;
  size_t total = 0;
  for (size_t node_size = n; node_size > 1; node_size >>= 1U)
  {
    if (total > std::numeric_limits<size_t>::max() - gates_per_level)
    {
      return 0;
    }
    total += gates_per_level;
  }
  return total;
}

void TagPairMask(uint8_t x, uint8_t y, uint8_t mask[10])
{
  const uint8_t x00 = CtEqualU8(x, FMS_TAG_NONE);
  const uint8_t x10 = CtEqualU8(x, FMS_TAG_LEFT);
  const uint8_t x01 = CtEqualU8(x, FMS_TAG_RIGHT);
  const uint8_t x11 = CtEqualU8(x, FMS_TAG_BOTH);
  const uint8_t y00 = CtEqualU8(y, FMS_TAG_NONE);
  const uint8_t y10 = CtEqualU8(y, FMS_TAG_LEFT);
  const uint8_t y01 = CtEqualU8(y, FMS_TAG_RIGHT);
  const uint8_t y11 = CtEqualU8(y, FMS_TAG_BOTH);

  mask[0] = static_cast<uint8_t>(x00 & y00);
  mask[1] = static_cast<uint8_t>((x00 & y10) | (x10 & y00));
  mask[2] = static_cast<uint8_t>((x00 & y01) | (x01 & y00));
  mask[3] = static_cast<uint8_t>((x00 & y11) | (x11 & y00));
  mask[4] = static_cast<uint8_t>(x10 & y10);
  mask[5] = static_cast<uint8_t>((x10 & y01) | (x01 & y10));
  mask[6] = static_cast<uint8_t>((x10 & y11) | (x11 & y10));
  mask[7] = static_cast<uint8_t>(x01 & y01);
  mask[8] = static_cast<uint8_t>((x01 & y11) | (x11 & y01));
  mask[9] = static_cast<uint8_t>(x11 & y11);
}

bool TagNormalize(const uint8_t *tags,
                  size_t n,
                  std::vector<uint8_t> *normalized_tags)
{
  if (tags == NULL || normalized_tags == NULL || !FMSIsPowerOfTwo(n))
  {
    return false;
  }

  size_t left_weight = 0;
  size_t right_weight = 0;
  uint8_t invalid_tag = 0;
  for (size_t index = 0; index < n; ++index)
  {
    invalid_tag = static_cast<uint8_t>(invalid_tag | (tags[index] >> 2U));
    left_weight += LeftBit(tags[index]);
    right_weight += RightBit(tags[index]);
  }

  const size_t half = n / 2;
  if (invalid_tag != 0 || left_weight > half || right_weight > half)
  {
    return false;
  }

  normalized_tags->assign(tags, tags + n);
  const size_t assign_left = half - left_weight;
  const size_t assign_right = half - right_weight;
  size_t zero_rank = 0;

  for (size_t index = 0; index < n; ++index)
  {
    const uint8_t is_zero = CtEqualU8(tags[index], FMS_TAG_NONE);
    const uint8_t use_left = static_cast<uint8_t>(
        is_zero & CtLessSize(zero_rank, assign_left));
    const uint8_t after_left = static_cast<uint8_t>(
        CtLessSize(zero_rank, assign_left) ^ 1U);
    const uint8_t before_end = CtLessSize(
        zero_rank, assign_left + assign_right);
    const uint8_t use_right = static_cast<uint8_t>(
        is_zero & after_left & before_end);

    uint8_t normalized = (*normalized_tags)[index];
    normalized = CtSelectU8(normalized, FMS_TAG_LEFT, use_left);
    normalized = CtSelectU8(normalized, FMS_TAG_RIGHT, use_right);
    (*normalized_tags)[index] = normalized;
    zero_rank += is_zero;
  }

  return true;
}

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t t_top,
                     uint8_t t_bottom)
{
  const uint8_t straight = static_cast<uint8_t>(
      CtEqualU8(t_top, x) & CtEqualU8(t_bottom, y));
  const uint8_t cross = static_cast<uint8_t>(
      CtEqualU8(t_top, y) & CtEqualU8(t_bottom, x));

  const uint8_t fork_x = static_cast<uint8_t>(
      CtEqualU8(x, FMS_TAG_BOTH) & CtEqualU8(y, FMS_TAG_NONE));
  const uint8_t fork_y = static_cast<uint8_t>(
      CtEqualU8(x, FMS_TAG_NONE) & CtEqualU8(y, FMS_TAG_BOTH));
  const uint8_t fork_pair = static_cast<uint8_t>(fork_x | fork_y);

  const uint8_t left_right = static_cast<uint8_t>(
      CtEqualU8(t_top, FMS_TAG_LEFT) &
      CtEqualU8(t_bottom, FMS_TAG_RIGHT));
  const uint8_t right_left = static_cast<uint8_t>(
      CtEqualU8(t_top, FMS_TAG_RIGHT) &
      CtEqualU8(t_bottom, FMS_TAG_LEFT));
  const uint8_t fork = static_cast<uint8_t>(
      fork_pair & (left_right | right_left));

  const uint8_t use_cross = static_cast<uint8_t>(
      cross & (straight ^ 1U));
  const uint8_t use_fork = static_cast<uint8_t>(
      fork & (straight ^ 1U) & (cross ^ 1U));
  const uint8_t fork_control = static_cast<uint8_t>(
      (fork_y << 1U) | fork_y);

  uint8_t control = 1; // 01: straight-through.
  control = CtSelectU8(control, 2, use_cross); // 10: cross.
  control = CtSelectU8(control, fork_control, use_fork);
  return control;
}

bool FMSBalance(const uint8_t *tags,
                size_t n,
                FMSBalanceOutput *output)
{
  if (tags == NULL || output == NULL || n < 4 || !FMSIsPowerOfTwo(n))
  {
    return false;
  }

  const size_t half = n / 2;
  size_t left_weight = 0;
  size_t right_weight = 0;
  uint8_t invalid_tag = 0;
  size_t histogram[10] = {};

  for (size_t index = 0; index < n; ++index)
  {
    invalid_tag = static_cast<uint8_t>(invalid_tag | (tags[index] >> 2U));
    left_weight += LeftBit(tags[index]);
    right_weight += RightBit(tags[index]);
  }
  if (invalid_tag != 0 || left_weight != half || right_weight != half)
  {
    return false;
  }

  for (size_t gate = 0; gate < half; ++gate)
  {
    uint8_t pair_mask[10];
    TagPairMask(tags[2 * gate], tags[2 * gate + 1], pair_mask);
    for (size_t type = 0; type < 10; ++type)
    {
      histogram[type] += pair_mask[type];
    }
  }

  const size_t diagonal = histogram[5];
  const size_t variable_left =
      histogram[1] + histogram[3] + histogram[8];
  const size_t variable_right =
      histogram[2] + histogram[3] + histogram[6];

  const size_t quota_diagonal = diagonal / 2;       // floor(d / 2)
  const size_t quota_left = (variable_left / 2) +   // ceil(v_L / 2)
                            (variable_left & 1U);
  const size_t quota_right = variable_right / 2;    // floor(v_R / 2)

  output->controls.assign(half, 0);
  output->top_tags.assign(half, 0);
  output->bottom_tags.assign(half, 0);

  size_t rank_diagonal = 0;
  size_t rank_left = 0;
  size_t rank_right = 0;

  for (size_t gate = 0; gate < half; ++gate)
  {
    const uint8_t x = tags[2 * gate];
    const uint8_t y = tags[2 * gate + 1];
    uint8_t pair_mask[10];
    TagPairMask(x, y, pair_mask);

    const uint8_t diagonal_type = pair_mask[5];
    const uint8_t left_variable_type = static_cast<uint8_t>(
        pair_mask[1] | pair_mask[3] | pair_mask[8]);
    const uint8_t right_variable_type = static_cast<uint8_t>(
        pair_mask[2] | pair_mask[3] | pair_mask[6]);

    const uint8_t assign_diagonal = static_cast<uint8_t>(
        diagonal_type & CtLessSize(rank_diagonal, quota_diagonal));
    const uint8_t assign_left = static_cast<uint8_t>(
        left_variable_type & CtLessSize(rank_left, quota_left));
    const uint8_t assign_right = static_cast<uint8_t>(
        right_variable_type & CtLessSize(rank_right, quota_right));

    const uint8_t fixed_left = static_cast<uint8_t>(
        pair_mask[4] | pair_mask[6] | pair_mask[9]);
    const uint8_t fixed_right = static_cast<uint8_t>(
        pair_mask[7] | pair_mask[8] | pair_mask[9]);

    const uint8_t top_left = static_cast<uint8_t>(
        fixed_left | assign_diagonal | assign_left);
    const uint8_t top_right = static_cast<uint8_t>(
        fixed_right |
        (diagonal_type & (assign_diagonal ^ 1U)) |
        assign_right);
    const uint8_t bottom_left = static_cast<uint8_t>(
        LeftBit(x) + LeftBit(y) - top_left);
    const uint8_t bottom_right = static_cast<uint8_t>(
        RightBit(x) + RightBit(y) - top_right);

    const uint8_t top_tag = static_cast<uint8_t>(
        (top_left << 1U) | top_right);
    const uint8_t bottom_tag = static_cast<uint8_t>(
        (bottom_left << 1U) | bottom_right);

    output->controls[gate] = OForkControl(x, y, top_tag, bottom_tag);
    output->top_tags[gate] = top_tag;
    output->bottom_tags[gate] = bottom_tag;

    rank_diagonal += diagonal_type;
    rank_left += left_variable_type;
    rank_right += right_variable_type;
  }

  return true;
}

bool FMSControlWrite(const uint8_t *normalized_tags,
                     size_t n,
                     std::vector<uint8_t> *controls,
                     size_t position,
                     size_t *next_pos)
{
  if (normalized_tags == NULL || controls == NULL || next_pos == NULL ||
      !FMSIsPowerOfTwo(n))
  {
    return false;
  }

  const size_t required = FMSControlNum(n);
  if (required == 0 || position > controls->size() ||
      required > controls->size() - position)
  {
    return false;
  }

  if (!WriteLevelOrderedControls(normalized_tags,
                                 n,
                                 controls,
                                 position))
  {
    return false;
  }

  *next_pos = position + required;
  return true;
}

bool FMSControlBits(const uint8_t *tags,
                    size_t n,
                    std::vector<uint8_t> *controls,
                    std::vector<uint8_t> *normalized_tags)
{
  if (tags == NULL || controls == NULL || controls == normalized_tags)
  {
    return false;
  }

  const size_t control_count = FMSControlNum(n);
  if (control_count == 0)
  {
    return false;
  }

  try
  {
    std::vector<uint8_t> normalized;
    if (!TagNormalize(tags, n, &normalized))
    {
      return false;
    }

    controls->assign(control_count, 0);
    size_t next_pos = 0;
    if (!FMSControlWrite(normalized.data(),
                         n,
                         controls,
                         0,
                         &next_pos) ||
        next_pos != control_count)
    {
      controls->clear();
      return false;
    }

    if (normalized_tags != NULL)
    {
      *normalized_tags = normalized;
    }
  }
  catch (const std::bad_alloc &)
  {
    controls->clear();
    if (normalized_tags != NULL)
    {
      normalized_tags->clear();
    }
    return false;
  }

  return true;
}

bool FMSApply(unsigned char *data,
              size_t n,
              size_t block_size,
              const uint8_t *controls,
              size_t control_count)
{
  size_t data_bytes = 0;
  if (data == NULL || controls == NULL || !FMSIsPowerOfTwo(n) ||
      !SupportedBlockSize(block_size) ||
      control_count != FMSControlNum(n) ||
      MulOverflowSize(n, block_size, &data_bytes))
  {
    return false;
  }
  (void)data_bytes;

  return ApplyLevelOrdered(data, n, block_size, controls, control_count);
}

#ifndef BEFTS_MODE

extern "C" int FMSPrepare(uint8_t *routing_tags,
                          size_t n,
                          size_t *control_words,
                          double *control_bits_us)
{
  if (routing_tags == NULL || control_words == NULL ||
      control_bits_us == NULL ||
      !FMSIsPowerOfTwo(n))
  {
    return -1;
  }

  *control_bits_us = -1.0;
  std::vector<uint8_t> controls;
  std::vector<uint8_t> normalized_tags;
  long start_time = 0;
  long stop_time = 0;
  ocall_clock(&start_time);
  if (!FMSControlBits(routing_tags,
                      n,
                      &controls,
                      &normalized_tags))
  {
    return -2;
  }
  ocall_clock(&stop_time);
  *control_bits_us = static_cast<double>(stop_time - start_time);

  if (!ofork_internal::StoreContext(&controls, &normalized_tags))
  {
    return -3;
  }

  *control_words = ofork_internal::ContextControlCount();
  return 0;
}

extern "C" double FMSApplyOnline(unsigned char *buffer,
                                  size_t n,
                                  size_t block_size)
{
  const uint8_t *controls = ofork_internal::ContextControls();
  const size_t control_count = ofork_internal::ContextControlCount();
  if (n != ofork_internal::ContextSize() || controls == NULL ||
      !SupportedBlockSize(block_size) ||
      !ofork_internal::IsOutsideBuffer(buffer, n, block_size))
  {
    return -1.0;
  }

  long start_time = 0;
  long stop_time = 0;
  ocall_clock(&start_time);
  const bool success = FMSApply(buffer,
                                n,
                                block_size,
                                controls,
                                control_count);
  ocall_clock(&stop_time);
  if (!success)
  {
    return -1.0;
  }
  return static_cast<double>(stop_time - start_time);
}

extern "C" double TwoCompactOnline(unsigned char *left_buffer,
                                    unsigned char *right_buffer,
                                    size_t n,
                                    size_t block_size)
{
  bool *left_selected = ofork_internal::ContextLeftSelected();
  bool *right_selected = ofork_internal::ContextRightSelected();
  if (n != ofork_internal::ContextSize() || left_selected == NULL ||
      right_selected == NULL ||
      !SupportedBlockSize(block_size) ||
      !ofork_internal::IsOutsideBuffer(left_buffer, n, block_size) ||
      !ofork_internal::IsOutsideBuffer(right_buffer, n, block_size))
  {
    return -1.0;
  }

  long start_time = 0;
  long stop_time = 0;
  ocall_clock(&start_time);
  TightCompact_v2(left_buffer,
                  n,
                  block_size,
                  left_selected);
  TightCompact_v2(right_buffer,
                  n,
                  block_size,
                  right_selected);
  ocall_clock(&stop_time);
  return static_cast<double>(stop_time - start_time);
}

extern "C" void FMSRelease(void)
{
  ofork_internal::ReleaseContext();
}

#endif
