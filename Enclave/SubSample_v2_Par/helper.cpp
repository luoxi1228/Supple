#include "helper.hpp"
#ifndef BEFTS_MODE
#include "../ObliviousPrimitives.hpp"
#include "../utils.hpp"
#endif
#include <algorithm>
#include <cstring>
#include <new>

// The parallel path keeps its historical edge layout and serial fallback.
namespace swo_parallel {
namespace detail {

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
    k = (k + 1) / 2;
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

bool MakeMarkSliceRange(size_t row_words,
                        size_t slice_start,
                        size_t slice_bits,
                        MarkSliceRange *range)
{
  if (range == nullptr)
  {
    return false;
  }

  range->first_word = 0;
  range->last_word = 0;
  range->first_mask = 0;
  range->last_mask = 0;
  range->slice_start = 0;
  range->slice_bits = 0;
  range->dst_words = 0;
  range->valid = false;

  if (row_words == 0 || slice_bits == 0)
  {
    return false;
  }

  size_t slice_end = 0;
  if (AddOverflowSizeT(slice_start, slice_bits, &slice_end))
  {
    return false;
  }

  const size_t first_word = slice_start / SwoWordBits();
  if (first_word >= row_words)
  {
    return false;
  }

  size_t last_word = (slice_end - 1) / SwoWordBits();
  if (last_word >= row_words)
  {
    last_word = row_words - 1;
  }

  range->first_word = first_word;
  range->last_word = last_word;
  range->first_mask = MakeRangeMarkWord(slice_start, slice_end, first_word);
  range->last_mask = MakeRangeMarkWord(slice_start, slice_end, last_word);
  range->slice_start = slice_start;
  range->slice_bits = slice_bits;
  range->dst_words = MarkWords(slice_bits);
  range->valid = true;
  return true;
}

void CompactMarkToWorkspaceAndControl(const size_t *mark,
                                      size_t len_items,
                                      size_t src_mark_words,
                                      bool *selected,
                                      size_t target_size,
                                      const MarkSliceRange &slice_range,
                                      std::vector<uint8_t> &C,
                                      size_t bit_pos,
                                      SwoMarkWorkspace &ws)
{
  const size_t dst_mark_words = slice_range.dst_words;
  ws.ensure_capacity(len_items, dst_mark_words);
  ws.item_count = std::min(target_size, len_items);
  ws.mark_words_per_item = dst_mark_words;

  if (mark == nullptr || selected == nullptr || len_items == 0 ||
      src_mark_words == 0 || dst_mark_words == 0 || !slice_range.valid)
  {
    if (selected != nullptr)
    {
      std::fill(selected, selected + len_items, false);
    }
    ws.item_count = 0;
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
                                  slice_range.slice_start,
                                  slice_range.slice_bits);
      dst_row[0] = projected;
      nonzero = projected;
    }
    else
    {
      ProjectMarkSlice(src_row,
                       src_mark_words,
                       slice_range.slice_start,
                       slice_range.slice_bits,
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

  if (ws.item_count == 0)
  {
    return;
  }

  TightCompact_v2(reinterpret_cast<unsigned char *>(dst),
                  len_items,
                  sizeof(size_t) * dst_mark_words,
                  selected);
}

void CompactDataToWorkspace(const unsigned char *data,
                            size_t len_items,
                            const bool *selected,
                            size_t target_size,
                            size_t block_size,
                            SwoDataWorkspace &ws)
{
  ws.ensure_capacity(len_items, block_size);
  ws.item_count = std::min(target_size, len_items);

  if (data == nullptr || selected == nullptr || len_items == 0 ||
      ws.item_count == 0 || block_size == 0)
  {
    return;
  }

  std::memcpy(ws.data_ptr(), data, len_items * block_size);
  TightCompact_v2(ws.data_ptr(),
                  len_items,
                  block_size,
                  const_cast<bool *>(selected));
}

size_t CompactDataInPlace(unsigned char *data,
                          size_t len_items,
                          const bool *selected,
                          size_t target_size,
                          size_t block_size)
{
  const size_t item_count = std::min(target_size, len_items);
  if (data == nullptr || selected == nullptr || len_items == 0 ||
      item_count == 0 || block_size == 0)
  {
    return item_count;
  }

  TightCompact_v2(data,
                  len_items,
                  block_size,
                  const_cast<bool *>(selected));
  return item_count;
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

struct ControlNumCacheEntry
{
  size_t n;
  size_t m;
  size_t k;
  size_t value;
};

static const size_t kControlNumCacheCapacity = 4096;
static ControlNumCacheEntry g_control_num_cache[kControlNumCacheCapacity];
static size_t g_control_num_cache_size = 0;


std::vector<size_t> MarkMembership(size_t n, size_t m, size_t k)
{
  const size_t mark_words = MarkWords(k);
  size_t mark_list_size = 0;
  if (MulOverflowSizeT(n, mark_words, &mark_list_size))
  {
    return std::vector<size_t>();
  }

  std::vector<size_t> M(mark_list_size, 0);
  if (M.empty() || n == 0 || k == 0 || mark_words == 0)
  {
    return M;
  }

  if (m > n)
  {
    m = n;
  }

  std::vector<uint64_t> left_to_mark(k, m);

  PRB_buffer *randpool = PRB_pool + g_thread_id;
  static const size_t kMaxCoins = 2048;
  uint32_t coins[kMaxCoins];
  size_t coinsleft = 0;

  for (size_t i = 0; i < n; i++)
  {
    size_t *current_mark_ptr = &M[i * mark_words];
    const uint64_t remaining = static_cast<uint64_t>(n - i);

    for (size_t w = 0; w < mark_words; w++)
    {
      size_t mark_word = 0;
      const size_t begin = w * SwoWordBits();
      const size_t end = std::min(begin + SwoWordBits(), k);

      for (size_t j = begin; j < end; j++)
      {
        if (coinsleft == 0)
        {
          randpool->getRandomBytes(reinterpret_cast<unsigned char *>(coins),
                                   sizeof(coins[0]) * kMaxCoins);
          coinsleft = kMaxCoins;
        }

        const uint32_t random_coin = coins[--coinsleft];
        const uint64_t threshold =
            (static_cast<uint64_t>(left_to_mark[j]) << 32) / remaining;
        const size_t mark_element = static_cast<size_t>(random_coin < threshold);

        mark_word |= (mark_element << (j - begin));
        left_to_mark[j] -= mark_element;
      }

      current_mark_ptr[w] = mark_word;
    }
  }

  return M;
}

std::vector<FrontierNode> BuildFrontier(size_t s, size_t l, size_t n, size_t m)
{
  if (l == 0)
  {
    return std::vector<FrontierNode>();
  }

  size_t ml = 0;
  const bool overflow = MulOverflowSizeT(m, l, &ml);
  if (l == 1 || (!overflow && ml < n))
  {
    std::vector<FrontierNode> F;
    F.push_back(FrontierNode{s, l});
    return F;
  }

  const size_t l_left = l / 2;
  const size_t l_right = l - l_left;
  std::vector<FrontierNode> F_left = BuildFrontier(s, l_left, n, m);
  std::vector<FrontierNode> F_right = BuildFrontier(s + l_left, l_right, n, m);

  F_left.insert(F_left.end(), F_right.begin(), F_right.end());
  return F_left;
}

std::vector<FrontierNode> NextNodes(const std::vector<FrontierNode> &F, size_t k)
{
  if (!F.empty())
  {
    return F;
  }

  std::vector<FrontierNode> V;
  if (k <= 1)
  {
    return V;
  }

  const size_t k_left = k / 2;
  const size_t k_right = k - k_left;
  V.push_back(FrontierNode{0, k_left});
  V.push_back(FrontierNode{k_left, k_right});
  return V;
}

size_t CountControlBits(const std::vector<FrontierNode> &F, size_t n, size_t m, size_t k)
{
  if (k <= 1)
  {
    return 0;
  }

  if (F.empty())
  {
    for (size_t i = 0; i < g_control_num_cache_size; i++)
    {
      if (g_control_num_cache[i].n == n &&
          g_control_num_cache[i].m == m &&
          g_control_num_cache[i].k == k)
      {
        return g_control_num_cache[i].value;
      }
    }
  }

  const std::vector<FrontierNode> V = NextNodes(F, k);
  size_t L = 0;
  const std::vector<FrontierNode> empty_frontier;

  for (size_t i = 0; i < V.size(); i++)
  {
    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, V[i].count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);

    const size_t child = CountControlBits(empty_frontier, tv, m, V[i].count);
    size_t with_node = 0;
    size_t next_L = 0;
    if (AddOverflowSizeT(L, n, &with_node) ||
        AddOverflowSizeT(with_node, child, &next_L))
    {
      return 0;
    }
    L = next_L;
  }

  if (F.empty())
  {
    if (g_control_num_cache_size < kControlNumCacheCapacity)
    {
      ControlNumCacheEntry entry = {n, m, k, L};
      g_control_num_cache[g_control_num_cache_size++] = entry;
    }
  }
  return L;
}

size_t WriteControlWorkspace(const size_t *M,
                              size_t n,
                              size_t mark_words,
                              std::vector<uint8_t> &C,
                              const std::vector<FrontierNode> &F,
                              size_t m,
                              size_t k,
                              size_t p,
                              std::vector<SwoMarkWorkspace> &workspaces,
                              size_t depth)
{
  if (M == nullptr || k <= 1 || mark_words == 0)
  {
    return p;
  }

  const std::vector<FrontierNode> V = NextNodes(F, k);
  const std::vector<FrontierNode> empty_frontier;

  for (size_t i = 0; i < V.size(); i++)
  {
    const FrontierNode node = V[i];

    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, node.count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);

    bool *selected = AcquireSelectedScratchA(n);
    if (selected == nullptr || depth >= workspaces.size())
    {
      return p;
    }

    MarkSliceRange node_range;
    MakeMarkSliceRange(mark_words, node.start, node.count, &node_range);

    SwoMarkWorkspace &child_ws = workspaces[depth];
    CompactMarkToWorkspaceAndControl(M,
                                     n,
                                     mark_words,
                                     selected,
                                     tv,
                                     node_range,
                                     C,
                                     p,
                                     child_ws);
    if (AddOverflowSizeT(p, n, &p))
    {
      return p;
    }

    p = WriteControlWorkspace(child_ws.mark_ptr(),
                               child_ws.item_count,
                               MarkWords(node.count),
                               C,
                               empty_frontier,
                               m,
                               node.count,
                               p,
                               workspaces,
                               depth + 1);
  }

  return p;
}

ControlReadResult ReadControlWorkspace(unsigned char *D,
                                        const std::vector<uint8_t> &C,
                                        const std::vector<FrontierNode> &F,
                                        size_t n,
                                        size_t m,
                                        size_t k,
                                        size_t block_size,
                                        unsigned char *S,
                                        size_t out_capacity_blocks,
                                        size_t p,
                                        std::vector<SwoDataWorkspace> &workspaces,
                                        size_t depth)
{
  ControlReadResult result = {0, p};
  if (D == nullptr || S == nullptr || block_size == 0 || out_capacity_blocks == 0)
  {
    return result;
  }

  if (k <= 1)
  {
    size_t copy_count = std::min(m, n);
    copy_count = std::min(copy_count, out_capacity_blocks);
    if (copy_count > 0)
    {
      std::memcpy(S, D, copy_count * block_size);
      // RecursiveShuffle_M2(S, copy_count, block_size); //任务调整
    }
    result.written_blocks = copy_count;
    return result;
  }

  const std::vector<FrontierNode> V = NextNodes(F, k);
  const std::vector<FrontierNode> empty_frontier;
  size_t written = 0;

  if (V.size() == 2)
  {
    if (depth >= workspaces.size())
    {
      result.next_pos = p;
      return result;
    }

    const FrontierNode left_node = V[0];
    const FrontierNode right_node = V[1];

    size_t ml_left = 0;
    size_t ml_right = 0;
    const bool overflow_left = MulOverflowSizeT(m, left_node.count, &ml_left);
    const bool overflow_right = MulOverflowSizeT(m, right_node.count, &ml_right);
    const size_t tv_left = overflow_left ? n : std::min(n, ml_left);
    const size_t tv_right = overflow_right ? n : std::min(n, ml_right);

    bool *selected_left = AcquireSelectedScratchA(n);
    bool *selected_right = AcquireSelectedScratchB(n);
    if (selected_left == nullptr || selected_right == nullptr)
    {
      result.next_pos = p;
      return result;
    }

    size_t p_left_child = 0;
    if (AddOverflowSizeT(p, n, &p_left_child))
    {
      result.next_pos = p;
      return result;
    }

    const size_t left_child_bits =
        CountControlBits(empty_frontier, tv_left, m, left_node.count);
    size_t p_right = 0;
    if (AddOverflowSizeT(p_left_child, left_child_bits, &p_right))
    {
      result.next_pos = p;
      return result;
    }

    size_t p_right_child = 0;
    if (AddOverflowSizeT(p_right, n, &p_right_child))
    {
      result.next_pos = p;
      return result;
    }

    UnpackControlBitsToBoolArray(C, p, n, selected_left);
    UnpackControlBitsToBoolArray(C, p_right, n, selected_right);

    SwoDataWorkspace &right_ws = workspaces[depth];
    CompactDataToWorkspace(D, n, selected_right, tv_right, block_size, right_ws);
    const size_t left_items =
        CompactDataInPlace(D, n, selected_left, tv_left, block_size);

    ControlReadResult left_child =
        ReadControlWorkspace(D,
                              C,
                              empty_frontier,
                              left_items,
                              m,
                              left_node.count,
                              block_size,
                              S,
                              out_capacity_blocks,
                              p_left_child,
                              workspaces,
                              depth + 1);

    written = left_child.written_blocks;
    if (written >= out_capacity_blocks)
    {
      result.written_blocks = written;
      result.next_pos = left_child.next_pos;
      return result;
    }

    ControlReadResult right_child =
        ReadControlWorkspace(right_ws.data_ptr(),
                              C,
                              empty_frontier,
                              right_ws.item_count,
                              m,
                              right_node.count,
                              block_size,
                              S + (written * block_size),
                              out_capacity_blocks - written,
                              p_right_child,
                              workspaces,
                              depth + 1);

    written += right_child.written_blocks;
    result.written_blocks = written;
    result.next_pos = right_child.next_pos;
    return result;
  }

  for (size_t i = 0; i < V.size(); i++)
  {
    const FrontierNode node = V[i];

    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, node.count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);

    bool *selected = AcquireSelectedScratchA(n);
    if (selected == nullptr || depth >= workspaces.size())
    {
      result.written_blocks = written;
      result.next_pos = p;
      return result;
    }

    UnpackControlBitsToBoolArray(C, p, n, selected);
    if (AddOverflowSizeT(p, n, &p))
    {
      result.written_blocks = written;
      result.next_pos = p;
      return result;
    }

    SwoDataWorkspace &child_ws = workspaces[depth];
    CompactDataToWorkspace(D, n, selected, tv, block_size, child_ws);

    ControlReadResult child =
        ReadControlWorkspace(child_ws.data_ptr(),
                              C,
                              empty_frontier,
                              child_ws.item_count,
                              m,
                              node.count,
                              block_size,
                              S + (written * block_size),
                              out_capacity_blocks - written,
                              p,
                              workspaces,
                              depth + 1);

    written += child.written_blocks;
    p = child.next_pos;
    if (written >= out_capacity_blocks)
    {
      break;
    }
  }

  result.written_blocks = written;
  result.next_pos = p;
  return result;
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

} // namespace detail
} // namespace swo_parallel
