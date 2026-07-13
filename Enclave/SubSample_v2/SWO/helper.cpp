#include "SuppleSWO.hpp"

#include <algorithm>
#include <cstring>
#include <new>

size_t MarkWords(size_t k)
{
  if (k == 0)
  {
    return 0;
  }
  return ((k - 1) / SwoWordBits()) + 1;
}

bool AddOverflowSizeT(size_t a, size_t b, size_t *out)
{
  if (out == nullptr)
  {
    return true;
  }
  if (a > SIZE_MAX - b)
  {
    return true;
  }
  *out = a + b;
  return false;
}

size_t MakeRangeMarkWord(size_t range_start, size_t range_end, size_t word_index)
{
  if (range_end <= range_start)
  {
    return 0;
  }

  const size_t word_start = word_index * SwoWordBits();
  const size_t word_end = word_start + SwoWordBits();
  if (range_end <= word_start || range_start >= word_end)
  {
    return 0;
  }

  const size_t local_start = (range_start > word_start) ? (range_start - word_start) : 0;
  const size_t local_end =
      (range_end < word_end) ? (range_end - word_start) : SwoWordBits();
  const size_t width = local_end - local_start;

  if (width >= SwoWordBits())
  {
    return ~size_t(0);
  }
  return ((size_t(1) << width) - 1) << local_start;
}

void ProjectMarkSlice(const size_t *src_row,
                      size_t src_words,
                      size_t slice_start,
                      size_t slice_bits,
                      size_t *dst_row,
                      size_t dst_words)
{
  if (slice_bits == 0 || dst_words == 0)
  {
    return;
  }

  for (size_t w = 0; w < dst_words; w++)
  {
    const size_t bit_pos = slice_start + (w * SwoWordBits());
    const size_t src_w = bit_pos / SwoWordBits();
    const size_t off = bit_pos % SwoWordBits();

    const size_t low = (src_w < src_words) ? (src_row[src_w] >> off) : 0;
    size_t high = 0;
    if (off != 0 && (src_w + 1) < src_words)
    {
      high = src_row[src_w + 1] << (SwoWordBits() - off);
    }

    dst_row[w] = (low | high);
  }

  const size_t valid_bits_last_word = slice_bits % SwoWordBits();
  if (valid_bits_last_word != 0)
  {
    const size_t keep = (size_t(1) << valid_bits_last_word) - 1;
    dst_row[dst_words - 1] &= keep;
  }
}

bool *AcquireSelectedScratchA(size_t required)
{
  thread_local bool *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required == 0)
  {
    return scratch;
  }
  if (required <= capacity)
  {
    return scratch;
  }

  bool *new_scratch = new (std::nothrow) bool[required];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required;
  return scratch;
}

bool *AcquireSelectedScratchB(size_t required)
{
  thread_local bool *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required == 0)
  {
    return scratch;
  }
  if (required <= capacity)
  {
    return scratch;
  }

  bool *new_scratch = new (std::nothrow) bool[required];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required;
  return scratch;
}

bool GetControlBit(const std::vector<uint8_t> &C, size_t pos)
{
  const size_t byte_index = pos / 8;
  const size_t bit_index = pos % 8;
  if (byte_index >= C.size())
  {
    return false;
  }
  return ((C[byte_index] >> bit_index) & 1U) != 0;
}

void UnpackControlBitsToBoolArray(const std::vector<uint8_t> &C,
                                  size_t bit_pos,
                                  size_t bit_count,
                                  bool *dst)
{
  if (dst == nullptr || bit_count == 0)
  {
    return;
  }

  size_t byte_index = bit_pos / 8;
  size_t bit_index = bit_pos % 8;
  size_t out = 0;

  if (byte_index >= C.size())
  {
    std::fill(dst, dst + bit_count, false);
    return;
  }

  if (bit_index != 0)
  {
    const uint8_t bits = C[byte_index];
    while (out < bit_count && bit_index < 8)
    {
      dst[out++] = ((bits >> bit_index) & 1U) != 0;
      bit_index++;
    }
    byte_index++;
  }

  while ((out + 8) <= bit_count && byte_index < C.size())
  {
    const uint8_t bits = C[byte_index++];
    dst[out + 0] = (bits & 0x01U) != 0;
    dst[out + 1] = (bits & 0x02U) != 0;
    dst[out + 2] = (bits & 0x04U) != 0;
    dst[out + 3] = (bits & 0x08U) != 0;
    dst[out + 4] = (bits & 0x10U) != 0;
    dst[out + 5] = (bits & 0x20U) != 0;
    dst[out + 6] = (bits & 0x40U) != 0;
    dst[out + 7] = (bits & 0x80U) != 0;
    out += 8;
  }

  if (out < bit_count && byte_index < C.size())
  {
    const uint8_t bits = C[byte_index];
    bit_index = 0;
    while (out < bit_count && bit_index < 8)
    {
      dst[out++] = ((bits >> bit_index) & 1U) != 0;
      bit_index++;
    }
  }

  if (out < bit_count)
  {
    std::fill(dst + out, dst + bit_count, false);
  }
}

void PackBoolArrayToControlBits(std::vector<uint8_t> &C,
                                size_t bit_pos,
                                size_t bit_count,
                                const bool *src)
{
  if (src == nullptr || bit_count == 0)
  {
    return;
  }

  size_t byte_index = bit_pos / 8;
  size_t bit_index = bit_pos % 8;
  size_t in = 0;

  if (byte_index >= C.size())
  {
    return;
  }

  if (bit_index != 0)
  {
    uint8_t byte = C[byte_index];
    while (in < bit_count && bit_index < 8)
    {
      const uint8_t mask = static_cast<uint8_t>(1U << bit_index);
      byte = static_cast<uint8_t>(byte & ~mask);
      byte = static_cast<uint8_t>(byte | (src[in] ? mask : 0));
      in++;
      bit_index++;
    }
    C[byte_index++] = byte;
  }

  while ((in + 8) <= bit_count && byte_index < C.size())
  {
    uint8_t byte = 0;
    byte = static_cast<uint8_t>(byte | (src[in + 0] ? 0x01U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 1] ? 0x02U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 2] ? 0x04U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 3] ? 0x08U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 4] ? 0x10U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 5] ? 0x20U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 6] ? 0x40U : 0));
    byte = static_cast<uint8_t>(byte | (src[in + 7] ? 0x80U : 0));
    C[byte_index++] = byte;
    in += 8;
  }

  if (in < bit_count && byte_index < C.size())
  {
    uint8_t byte = C[byte_index];
    bit_index = 0;
    while (in < bit_count && bit_index < 8)
    {
      const uint8_t mask = static_cast<uint8_t>(1U << bit_index);
      byte = static_cast<uint8_t>(byte & ~mask);
      byte = static_cast<uint8_t>(byte | (src[in] ? mask : 0));
      in++;
      bit_index++;
    }
    C[byte_index] = byte;
  }
}

void SetControlBit(std::vector<uint8_t> &C, size_t pos, bool value)
{
  const size_t byte_index = pos / 8;
  const size_t bit_index = pos % 8;
  if (byte_index >= C.size())
  {
    return;
  }

  const uint8_t mask = static_cast<uint8_t>(1U << bit_index);
  if (value)
  {
    C[byte_index] |= mask;
  }
  else
  {
    C[byte_index] &= static_cast<uint8_t>(~mask);
  }
}

bool RowHasSliceBit(const size_t *row,
                    size_t row_words,
                    size_t slice_start,
                    size_t slice_bits)
{
  if (slice_bits == 0)
  {
    return false;
  }

  size_t slice_end = 0;
  if (AddOverflowSizeT(slice_start, slice_bits, &slice_end))
  {
    return false;
  }
  for (size_t w = 0; w < row_words; w++)
  {
    const size_t mask = MakeRangeMarkWord(slice_start, slice_end, w);
    if ((row[w] & mask) != 0)
    {
      return true;
    }
  }
  return false;
}

std::vector<bool> ControlSliceToSelected(const std::vector<uint8_t> &C,
                                         size_t p,
                                         size_t n)
{
  std::vector<bool> selected(n, false);
  for (size_t i = 0; i < n; i++)
  {
    selected[i] = GetControlBit(C, p + i);
  }
  return selected;
}

void CopyVectorBoolToBoolArray(const std::vector<bool> &src, bool *dst)
{
  for (size_t i = 0; i < src.size(); i++)
  {
    dst[i] = src[i];
  }
}

size_t OutputBlocksFor(size_t n, size_t m, size_t k)
{
  size_t clamped_m = m;
  if (clamped_m > n)
  {
    clamped_m = n;
  }

  size_t total_blocks = 0;
  if (MulOverflowSizeT(clamped_m, k, &total_blocks))
  {
    return 0;
  }
  return total_blocks;
}

size_t OMBSUBSAMPLE(const unsigned char *D,
                    size_t n,
                    size_t m,
                    size_t k,
                    size_t block_size,
                    unsigned char *S,
                    size_t out_capacity_blocks)
{
  if (S == nullptr)
  {
    return 0;
  }

  std::vector<unsigned char> result = OMBSUBSAMPLE(D, n, m, k, block_size);
  const size_t total_blocks = block_size == 0 ? 0 : (result.size() / block_size);
  const size_t copy_blocks = std::min(total_blocks, out_capacity_blocks);
  if (copy_blocks > 0)
  {
    std::memcpy(S, result.data(), copy_blocks * block_size);
  }
  return copy_blocks;
}
