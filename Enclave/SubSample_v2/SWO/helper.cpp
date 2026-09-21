#include "helper.hpp"
#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#endif
#include <algorithm>
#include <new>
#include <stdexcept>

namespace swo_detail
{

size_t *SwoMarkWorkspace::mark_ptr() { return mark_buf.data(); }

void SwoMarkWorkspace::ensure_capacity(size_t items, size_t words)
{
  size_t required = 0;
  if (MulOverflowSizeT(items, words, &required))
    throw std::length_error("SWO membership workspace size overflow");
  if (mark_buf.size() < required)
    mark_buf.resize(required);
}

unsigned char *SwoDataWorkspace::data_ptr() { return data_buf.data(); }

void SwoDataWorkspace::ensure_capacity(size_t items, size_t width)
{
  size_t required = 0;
  if (MulOverflowSizeT(items, width, &required))
    throw std::length_error("SWO data workspace size overflow");
  if (data_buf.size() < required)
    data_buf.resize(required);
}

size_t MarkWords(size_t k)
{
  if (k == 0)
  {
    return 0;
  }
  return ((k - 1) / SwoWordBits()) + 1;
}

size_t WorkspaceDepth(size_t k)
{
  size_t depth = 1;
  while (k > 1)
  {
    k = k / 2 + k % 2;
    depth++;
  }
  return depth + 1;
}

bool MulOverflowSizeT(size_t a, size_t b, size_t *out)
{
  if (out == nullptr)
  {
    return true;
  }
  if (a == 0 || b == 0)
  {
    *out = 0;
    return false;
  }
  if (a > (SIZE_MAX / b))
  {
    return true;
  }
  *out = a * b;
  return false;
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

size_t ProjectMarkSliceOneWord(const size_t *src_row,
    size_t src_words,
    size_t slice_start,
    size_t slice_bits)
{
  if (src_row == nullptr || src_words == 0 || slice_bits == 0)
  {
    return 0;
  }

  const size_t src_w = slice_start / SwoWordBits();
  const size_t off = slice_start % SwoWordBits();
  const size_t low = (src_w < src_words) ? (src_row[src_w] >> off) : 0;
  size_t high = 0;
  if (off != 0 && (src_w + 1) < src_words)
  {
    high = src_row[src_w + 1] << (SwoWordBits() - off);
  }

  size_t value = low | high;
  if (slice_bits < SwoWordBits())
  {
    value &= ((size_t(1) << slice_bits) - 1);
  }
  return value;
}

void ProjectMarkToWorkspace(const size_t *mark, size_t len_items,
    size_t src_mark_words, size_t slice_start,
    size_t slice_bits, SwoMarkWorkspace &ws)
{
  const size_t dst_words = MarkWords(slice_bits);
  ws.ensure_capacity(len_items, dst_words);
  for (size_t i = 0; i < len_items; ++i)
  {
    const size_t *src = mark + i * src_mark_words;
    if (dst_words == 1)
    {
      ws.mark_ptr()[i] =
          ProjectMarkSliceOneWord(src, src_mark_words, slice_start, slice_bits);
    }
    else
    {
      ProjectMarkSlice(src, src_mark_words, slice_start, slice_bits,
                       ws.mark_ptr() + i * dst_words, dst_words);
    }
  }
}

// SGX's trusted runtime has no TLS destructors; retain the per-thread scratch
// allocation as a raw pointer and release its previous buffer when it grows.
bool *AcquireSelectedScratch(size_t required)
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

void CompactMarkToWorkspaceAndControl(const size_t *mark,
    size_t len_items,
    size_t src_mark_words,
    bool *selected,
    size_t target_size,
    size_t slice_start,
    size_t slice_bits,
    std::vector<uint8_t> &C,
    size_t bit_pos,
    SwoMarkWorkspace &ws)
{
  const size_t dst_mark_words = MarkWords(slice_bits);
  ws.ensure_capacity(len_items, dst_mark_words);

  if (mark == nullptr || selected == nullptr || len_items == 0 ||
      src_mark_words == 0 || dst_mark_words == 0)
  {
    if (selected != nullptr)
    {
      std::fill(selected, selected + len_items, false);
    }
    return;
  }

  size_t *dst = ws.mark_ptr();
  size_t byte_index = bit_pos / 8;
  size_t bit_index = bit_pos % 8;

  for (size_t i = 0; i < len_items; i++)
  {
    const size_t *src_row = mark + (i * src_mark_words);
    size_t *dst_row = dst + (i * dst_mark_words);
    size_t nonzero = 0;

    if (dst_mark_words == 1)
    {
      const size_t projected =
          ProjectMarkSliceOneWord(src_row,
                                  src_mark_words,
                                  slice_start,
                                  slice_bits);
      dst_row[0] = projected;
      nonzero = projected;
    }
    else
    {
      ProjectMarkSlice(src_row,
                       src_mark_words,
                       slice_start,
                       slice_bits,
                       dst_row,
                       dst_mark_words);
      for (size_t w = 0; w < dst_mark_words; w++)
      {
        nonzero |= dst_row[w];
      }
    }

    const bool is_selected = (nonzero != 0);
    selected[i] = is_selected;

    if (byte_index < C.size())
    {
      const uint8_t bit_mask = static_cast<uint8_t>(1U << bit_index);
      const uint8_t value_mask =
          static_cast<uint8_t>(0U - static_cast<uint8_t>(is_selected));
      C[byte_index] =
          static_cast<uint8_t>((C[byte_index] & ~bit_mask) |
                               (value_mask & bit_mask));
    }

    bit_index++;
    if (bit_index == 8)
    {
      bit_index = 0;
      byte_index++;
    }
  }

  if (target_size == 0)
  {
    return;
  }
  // 非稳定版本
  TightCompact_v2(reinterpret_cast<unsigned char *>(dst),
                  len_items,
                  sizeof(size_t) * dst_mark_words,
                  selected);
}

size_t SwoNodeCapacity(size_t n, size_t m, size_t k)
{
  size_t capacity = 0;
  return MulOverflowSizeT(m, k, &capacity) ? n : std::min(n, capacity);
}

std::vector<FrontierNode> SwoChildren(size_t k)
{
  const size_t left = k / 2;
  return {{0, left}, {left, k - left}};
}

std::vector<FrontierNode> SwoRootNodes(const std::vector<FrontierNode> &F, size_t k)
{
  if (!F.empty())
    return F;
  // Preserve the C++ single-sample extension, including its initial filtering.
  return k == 1 ? std::vector<FrontierNode>{{0, 1}} : SwoChildren(k);
}

size_t SwoNodeDepth(const std::vector<FrontierNode> &nodes)
{
  size_t largest = 1;
  for (const FrontierNode &node : nodes)
    largest = std::max(largest, node.count);
  return WorkspaceDepth(largest);
}

void SwoCheckControlSpan(const std::vector<uint8_t> &C, size_t p, size_t bits)
{
  size_t end = 0;
  if (AddOverflowSizeT(p, bits, &end) ||
      end / 8 + (end % 8 != 0) > C.size())
    throw std::length_error("SWO control span exceeds preallocated array");
}

} // namespace swo_detail
