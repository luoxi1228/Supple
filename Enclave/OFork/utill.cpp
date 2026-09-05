#include "utill.hpp"
#include "OFork.hpp"

#include <limits>
#include <new>

#ifndef BEFTS_MODE
#include "../oasm_lib.h"
#include <sgx_trts.h>
#endif

namespace ofork_internal
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

uint8_t CtSelectU8(uint8_t old_value,
                   uint8_t new_value,
                   uint8_t flag)
{
  const uint8_t mask = static_cast<uint8_t>(0U - (flag & 1U));
  return static_cast<uint8_t>((old_value & static_cast<uint8_t>(~mask)) |
                              (new_value & mask));
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
  {
    return true;
  }
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
  {
    return true;
  }
  *result = lhs * rhs;
  return false;
}

bool SupportedBlockSize(size_t block_size)
{
  return block_size <= std::numeric_limits<uint32_t>::max() &&
         (block_size == 4 ||
          block_size == 8 ||
          block_size == 12 ||
          (block_size >= 16 && block_size % 16 == 0) ||
          (block_size >= 24 && block_size % 16 == 8));
}

namespace
{

bool WriteLevelOrderedNode(const uint8_t *tags,
                           size_t node_size,
                           size_t root_half,
                           size_t base_position,
                           size_t depth,
                           size_t stride,
                           size_t residue,
                           std::vector<uint8_t> *controls)
{
  const size_t stage_position = base_position + depth * root_half;

  if (node_size == 2)
  {
    const size_t control_position = stage_position + residue;
    if (control_position >= controls->size())
    {
      return false;
    }
    (*controls)[control_position] = OForkControl(tags[0],
                                                 tags[1],
                                                 FMS_TAG_LEFT,
                                                 FMS_TAG_RIGHT);
    return true;
  }

  FMSBalanceOutput balanced;
  if (!FMSBalance(tags, node_size, &balanced))
  {
    return false;
  }

  const size_t gate_count = node_size / 2;
  for (size_t gate = 0; gate < gate_count; ++gate)
  {
    const size_t control_position =
        stage_position + gate * stride + residue;
    if (control_position >= controls->size())
    {
      return false;
    }
    (*controls)[control_position] = balanced.controls[gate];
  }

  if (!WriteLevelOrderedNode(balanced.top_tags.data(),
                             gate_count,
                             root_half,
                             base_position,
                             depth + 1,
                             stride * 2,
                             residue,
                             controls))
  {
    return false;
  }

  return WriteLevelOrderedNode(balanced.bottom_tags.data(),
                               gate_count,
                               root_half,
                               base_position,
                               depth + 1,
                               stride * 2,
                               residue + stride,
                               controls);
}

#ifndef BEFTS_MODE

template <OFork_Style style>
bool ApplyLevelOrderedWithStyle(unsigned char *data,
                                size_t n,
                                size_t block_size,
                                const uint8_t *controls,
                                size_t control_count)
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
        ofork_buffer<style>(first,
                            second,
                            static_cast<uint32_t>(block_size),
                            LeftBit(control),
                            RightBit(control));
        first += block_size;
        second += block_size;
      }
    }
  }
  return position == control_count;
}

#else

void HostOFork(unsigned char *first,
               unsigned char *second,
               size_t block_size,
               uint8_t first_flag,
               uint8_t second_flag)
{
  const uint8_t first_mask =
      static_cast<uint8_t>(0U - (first_flag & 1U));
  const uint8_t second_mask =
      static_cast<uint8_t>(0U - (second_flag & 1U));

  for (size_t byte = 0; byte < block_size; ++byte)
  {
    const unsigned char x = first[byte];
    const unsigned char y = second[byte];
    first[byte] = static_cast<unsigned char>((x & ~first_mask) |
                                             (y & first_mask));
    second[byte] = static_cast<unsigned char>((x & ~second_mask) |
                                              (y & second_mask));
  }
}

bool ApplyLevelOrderedHost(unsigned char *data,
                           size_t n,
                           size_t block_size,
                           const uint8_t *controls,
                           size_t control_count)
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
        HostOFork(first,
                  second,
                  block_size,
                  LeftBit(control),
                  RightBit(control));
        first += block_size;
        second += block_size;
      }
    }
  }
  return position == control_count;
}

#endif

} // namespace

bool WriteLevelOrderedControls(const uint8_t *normalized_tags,
                               size_t n,
                               std::vector<uint8_t> *controls,
                               size_t position)
{
  const size_t required = FMSControlNum(n);
  if (normalized_tags == NULL || controls == NULL || required == 0 ||
      position > controls->size() ||
      required > controls->size() - position)
  {
    return false;
  }

  return WriteLevelOrderedNode(normalized_tags,
                               n,
                               n / 2,
                               position,
                               0,
                               1,
                               0,
                               controls);
}

bool ApplyLevelOrdered(unsigned char *data,
                       size_t n,
                       size_t block_size,
                       const uint8_t *controls,
                       size_t control_count)
{
  if (control_count != FMSControlNum(n))
  {
    return false;
  }

#ifndef BEFTS_MODE
  if (block_size == 4)
  {
    return ApplyLevelOrderedWithStyle<OFORK_4>(
        data, n, block_size, controls, control_count);
  }
  if (block_size == 8)
  {
    return ApplyLevelOrderedWithStyle<OFORK_8>(
        data, n, block_size, controls, control_count);
  }
  if (block_size == 12)
  {
    return ApplyLevelOrderedWithStyle<OFORK_12>(
        data, n, block_size, controls, control_count);
  }
  if (block_size == 16)
  {
    return ApplyLevelOrderedWithStyle<OFORK_16>(
        data, n, block_size, controls, control_count);
  }
  if (block_size == 24)
  {
    return ApplyLevelOrderedWithStyle<OFORK_24>(
        data, n, block_size, controls, control_count);
  }
  if (block_size % 16 == 0)
  {
    return ApplyLevelOrderedWithStyle<OFORK_16X>(
        data, n, block_size, controls, control_count);
  }
  return ApplyLevelOrderedWithStyle<OFORK_8_16X>(
      data, n, block_size, controls, control_count);
#else
  return ApplyLevelOrderedHost(
      data, n, block_size, controls, control_count);
#endif
}

#ifndef BEFTS_MODE

namespace
{

std::vector<uint8_t> g_fms_controls;
std::vector<uint8_t> g_fms_normalized_tags;
bool *g_fms_left_selected = NULL;
bool *g_fms_right_selected = NULL;
size_t g_fms_n = 0;

} // namespace

bool StoreContext(std::vector<uint8_t> *controls,
                  std::vector<uint8_t> *normalized_tags)
{
  if (controls == NULL || normalized_tags == NULL ||
      normalized_tags->empty())
  {
    return false;
  }

  const size_t n = normalized_tags->size();
  bool *left_selected = NULL;
  bool *right_selected = NULL;
  try
  {
    left_selected = new bool[n];
    right_selected = new bool[n];
  }
  catch (const std::bad_alloc &)
  {
    delete[] left_selected;
    delete[] right_selected;
    return false;
  }

  for (size_t index = 0; index < n; ++index)
  {
    left_selected[index] = LeftBit((*normalized_tags)[index]) != 0;
    right_selected[index] = RightBit((*normalized_tags)[index]) != 0;
  }

  ReleaseContext();
  g_fms_controls.swap(*controls);
  g_fms_normalized_tags.swap(*normalized_tags);
  g_fms_left_selected = left_selected;
  g_fms_right_selected = right_selected;
  g_fms_n = n;
  return true;
}

void ReleaseContext()
{
  delete[] g_fms_left_selected;
  delete[] g_fms_right_selected;
  g_fms_left_selected = NULL;
  g_fms_right_selected = NULL;
  g_fms_n = 0;
  std::vector<uint8_t>().swap(g_fms_controls);
  std::vector<uint8_t>().swap(g_fms_normalized_tags);
}

size_t ContextSize()
{
  return g_fms_n;
}

size_t ContextControlCount()
{
  return g_fms_controls.size();
}

const uint8_t *ContextControls()
{
  return g_fms_controls.empty() ? NULL : g_fms_controls.data();
}

bool *ContextLeftSelected()
{
  return g_fms_left_selected;
}

bool *ContextRightSelected()
{
  return g_fms_right_selected;
}

bool IsOutsideBuffer(unsigned char *buffer,
                     size_t n,
                     size_t block_size)
{
  size_t bytes = 0;
  return buffer != NULL &&
         !MulOverflowSize(n, block_size, &bytes) &&
         sgx_is_outside_enclave(buffer, bytes) != 0;
}

#endif

} // namespace ofork_internal
