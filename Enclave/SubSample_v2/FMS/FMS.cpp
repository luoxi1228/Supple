#include "FMS.hpp"
#include "helper.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

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

void CopyRecords(const unsigned char *source,
                 size_t blocks,
                 size_t block_size,
                 std::vector<unsigned char> *destination)
{
  size_t bytes = 0;
  if (destination == NULL || MulOverflowSize(blocks, block_size, &bytes))
    throw std::length_error("FMS data size overflow");
  destination->resize(bytes);
  if (bytes != 0)
    std::memcpy(destination->data(), source, bytes);
}

void AppendBytes(std::vector<unsigned char> *destination,
                 const std::vector<unsigned char> &suffix)
{
  if (destination->size() >
      std::numeric_limits<size_t>::max() - suffix.size())
    throw std::length_error("FMS output size overflow");
  destination->insert(destination->end(), suffix.begin(), suffix.end());
}

FMSControlReadResult FMSControlReadImpl(const unsigned char *data,
                                        const std::vector<uint8_t> &controls,
                                        size_t n,
                                        size_t n_left,
                                        size_t n_right,
                                        size_t block_size,
                                        size_t position)
{
  FMSControlReadResult result;
  result.next_pos = position;

  if (n_left == 0)
  {
    CopyRecords(data, n, block_size, &result.right);
    return result;
  }
  if (n_right == 0)
  {
    CopyRecords(data, n, block_size, &result.left);
    return result;
  }

  if (n == 2)
  {
    CopyRecords(data, 1, block_size, &result.left);
    CopyRecords(data + block_size, 1, block_size, &result.right);
    detail::ApplyOFork(result.left.data(),
                       result.right.data(),
                       block_size,
                       controls[position]);
    result.next_pos = position + 1;
    return result;
  }

  const CapacitySplit split = SplitCapacities(n, n_left, n_right);
  std::vector<unsigned char> top;
  std::vector<unsigned char> bottom;
  size_t top_bytes = 0;
  size_t bottom_bytes = 0;
  if (MulOverflowSize(split.n_top, block_size, &top_bytes) ||
      MulOverflowSize(split.n_bottom, block_size, &bottom_bytes))
    throw std::length_error("FMS layer workspace size overflow");
  top.resize(top_bytes);
  bottom.resize(bottom_bytes);

  const size_t gate_count = n / 2;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    unsigned char *top_record = top.data() + gate * block_size;
    unsigned char *bottom_record = bottom.data() + gate * block_size;
    std::memcpy(top_record, data + (2 * gate) * block_size, block_size);
    std::memcpy(bottom_record, data + (2 * gate + 1) * block_size, block_size);
    detail::ApplyOFork(top_record,
                       bottom_record,
                       block_size,
                       controls[position + gate]);
  }
  if ((n & 1U) != 0)
  {
    std::memcpy(top.data() + gate_count * block_size,
                data + (n - 1) * block_size,
                block_size);
  }
  position += gate_count;

  FMSControlReadResult top_result = FMSControlReadImpl(
      top.data(),
      controls,
      split.n_top,
      split.top_left,
      split.top_right,
      block_size,
      position);
  FMSControlReadResult bottom_result = FMSControlReadImpl(
      bottom.data(),
      controls,
      split.n_bottom,
      split.bottom_left,
      split.bottom_right,
      block_size,
      top_result.next_pos);

  result.left.swap(top_result.left);
  AppendBytes(&result.left, bottom_result.left);
  result.right.swap(top_result.right);
  AppendBytes(&result.right, bottom_result.right);
  result.next_pos = bottom_result.next_pos;
  return result;
}

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

  return FMSControlReadImpl(
      data, controls, n, n_left, n_right, block_size, position);
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
