#include "helper.hpp"

#include <climits>
#include <limits>
#include <stdexcept>

#ifndef BEFTS_MODE
#include "../../oasm_lib.h"
#endif

namespace fms
{
namespace detail
{

uint8_t CtEqualU8(uint8_t lhs, uint8_t rhs)
{
  uint8_t result;
  __asm__ volatile(
      "cmpb %[rhs], %[lhs]\n\t"
      "sete %[result]"
      : [result] "=r"(result)
      : [lhs] "r"(lhs), [rhs] "r"(rhs)
      : "cc");
  return result;
}

uint8_t CtLessSize(size_t lhs, size_t rhs)
{
  uint8_t result;
  __asm__ volatile(
      "cmp %[rhs], %[lhs]\n\t"
      "setb %[result]"
      : [result] "=r"(result)
      : [lhs] "r"(lhs), [rhs] "r"(rhs)
      : "cc");
  return result;
}

uint8_t CtLessI64(int64_t lhs, int64_t rhs)
{
  uint8_t result;
  __asm__ volatile(
      "cmpq %[rhs], %[lhs]\n\t"
      "setl %[result]"
      : [result] "=r"(result)
      : [lhs] "r"(lhs), [rhs] "r"(rhs)
      : "cc");
  return result;
}

uint8_t CtSelectU8(uint8_t old_value, uint8_t new_value, uint8_t flag)
{
  const uint8_t mask = static_cast<uint8_t>(0U - (flag & 1U));
  return static_cast<uint8_t>((old_value & static_cast<uint8_t>(~mask)) |
                              (new_value & mask));
}

int64_t CtSelectI64(int64_t old_value, int64_t new_value, uint8_t flag)
{
  const uint64_t mask = 0U - static_cast<uint64_t>(flag & 1U);
  const uint64_t old_bits = static_cast<uint64_t>(old_value);
  const uint64_t new_bits = static_cast<uint64_t>(new_value);
  return static_cast<int64_t>((old_bits & ~mask) | (new_bits & mask));
}

int64_t CtMinI64(int64_t lhs, int64_t rhs)
{
  return CtSelectI64(lhs, rhs, CtLessI64(rhs, lhs));
}

int64_t CtMaxI64(int64_t lhs, int64_t rhs)
{
  return CtSelectI64(lhs, rhs, CtLessI64(lhs, rhs));
}

uint8_t LeftBit(uint8_t tag)
{
  return static_cast<uint8_t>((tag >> 1U) & 1U);
}

uint8_t RightBit(uint8_t tag)
{
  return static_cast<uint8_t>(tag & 1U);
}

bool MulOverflowSize(size_t lhs, size_t rhs, size_t *result)
{
  if (result == NULL)
    return true;
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
    return true;
  *result = lhs * rhs;
  return false;
}

bool AddOverflowSize(size_t lhs, size_t rhs, size_t *result)
{
  if (result == NULL || lhs > std::numeric_limits<size_t>::max() - rhs)
    return true;
  *result = lhs + rhs;
  return false;
}

void ValidateCapacities(size_t n, size_t n_left, size_t n_right)
{
  if (n == 0 || n > static_cast<size_t>(INT64_MAX) ||
      n_left > n || n_right != n - n_left)
  {
    throw std::invalid_argument("FMS capacities must be nonnegative and sum to n");
  }
}

void ValidateTags(const std::vector<uint8_t> &tags,
                  size_t n,
                  size_t n_left,
                  size_t n_right,
                  bool require_exact)
{
  ValidateCapacities(n, n_left, n_right);
  if (tags.size() != n)
    throw std::invalid_argument("FMS tag array length mismatch");

  size_t left_weight = 0;
  size_t right_weight = 0;
  uint8_t invalid = 0;
  for (size_t i = 0; i < n; ++i)
  {
    invalid = static_cast<uint8_t>(invalid | (tags[i] >> 2U));
    left_weight += LeftBit(tags[i]);
    right_weight += RightBit(tags[i]);
  }

  const bool weights_invalid = require_exact
      ? (left_weight != n_left || right_weight != n_right)
      : (left_weight > n_left || right_weight > n_right);
  if (invalid != 0 || weights_invalid)
    throw std::invalid_argument("FMS routing tags do not match output capacities");
}

#ifndef BEFTS_MODE

template <OFork_Style style>
static void ApplyOForkWithStyle(unsigned char *top,
                                unsigned char *bottom,
                                size_t block_size,
                                uint8_t control)
{
  ofork_buffer<style>(top,
                      bottom,
                      static_cast<uint32_t>(block_size),
                      LeftBit(control),
                      RightBit(control));
}

#endif

static void ApplyOForkBytes(unsigned char *top,
                            unsigned char *bottom,
                            size_t block_size,
                            uint8_t control)
{
  const uint8_t top_mask = static_cast<uint8_t>(
      0U - static_cast<uint8_t>(LeftBit(control)));
  const uint8_t bottom_mask = static_cast<uint8_t>(
      0U - static_cast<uint8_t>(RightBit(control)));
  for (size_t byte = 0; byte < block_size; ++byte)
  {
    const uint8_t x = top[byte];
    const uint8_t y = bottom[byte];
    top[byte] = static_cast<uint8_t>((x & static_cast<uint8_t>(~top_mask)) |
                                     (y & top_mask));
    bottom[byte] = static_cast<uint8_t>(
        (x & static_cast<uint8_t>(~bottom_mask)) | (y & bottom_mask));
  }
}

void ApplyOFork(unsigned char *top,
                unsigned char *bottom,
                size_t block_size,
                uint8_t control)
{
  if (top == NULL || bottom == NULL || block_size == 0 || (control >> 2U) != 0)
    throw std::invalid_argument("Invalid OFork arguments");

#ifndef BEFTS_MODE
  if (block_size <= std::numeric_limits<uint32_t>::max())
  {
    if (block_size == 4)
      return ApplyOForkWithStyle<OFORK_4>(top, bottom, block_size, control);
    if (block_size == 8)
      return ApplyOForkWithStyle<OFORK_8>(top, bottom, block_size, control);
    if (block_size == 12)
      return ApplyOForkWithStyle<OFORK_12>(top, bottom, block_size, control);
    if (block_size == 16)
      return ApplyOForkWithStyle<OFORK_16>(top, bottom, block_size, control);
    if (block_size == 24)
      return ApplyOForkWithStyle<OFORK_24>(top, bottom, block_size, control);
    if (block_size >= 16 && block_size % 16 == 0)
      return ApplyOForkWithStyle<OFORK_16X>(top, bottom, block_size, control);
    if (block_size >= 24 && block_size % 16 == 8)
      return ApplyOForkWithStyle<OFORK_8_16X>(top, bottom, block_size, control);
  }
#endif
  // The assembly library specializes the common record widths. The byte-wise
  // path preserves the same oblivious semantics for every other width.
  ApplyOForkBytes(top, bottom, block_size, control);
}

} // namespace detail
} // namespace fms
