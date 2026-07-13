#include "SuppleSWO.hpp"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdlib>

std::vector<size_t> MARKMATRIX(size_t n, size_t m, size_t k)
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
  std::vector<uint64_t> total_left(k, n);

  PRB_buffer *randpool = PRB_pool + g_thread_id;
  static const size_t kMaxCoins = 2048;
  uint32_t coins[kMaxCoins];
  size_t coinsleft = 0;

  for (size_t i = 0; i < n; i++)
  {
    size_t *current_mark_ptr = &M[i * mark_words];

    for (size_t j = 0; j < k; j++)
    {
      if (coinsleft == 0)
      {
        randpool->getRandomBytes(reinterpret_cast<unsigned char *>(coins),
                                 sizeof(coins[0]) * kMaxCoins);
        coinsleft = kMaxCoins;
      }

      const uint32_t random_coin = coins[--coinsleft];
      const uint64_t threshold =
          (static_cast<uint64_t>(left_to_mark[j]) << 32) / total_left[j];
      const uint8_t mark_element = static_cast<uint8_t>(random_coin < threshold);

      const size_t word_index = j / SwoWordBits();
      const size_t bit_index = j % SwoWordBits();

      current_mark_ptr[word_index] |= (static_cast<size_t>(mark_element) << bit_index);
      left_to_mark[j] -= mark_element;
      total_left[j]--;
    }
  }

  return M;
}

static size_t WorkspaceDepth(size_t k)
{
  size_t depth = 1;
  while (k > 1)
  {
    k = (k + 1) / 2;
    depth++;
  }
  return depth + 1;
}

static bool RowHasSliceBitFast(const size_t *row,
                               size_t row_words,
                               size_t slice_start,
                               size_t slice_bits)
{
  if (slice_bits == 0)
  {
    return false;
  }

  const size_t word_start = slice_start / SwoWordBits();
  const size_t bit_start = slice_start % SwoWordBits();
  if (word_start >= row_words)
  {
    return false;
  }

  if (bit_start == 0 && slice_bits == SwoWordBits())
  {
    return row[word_start] != 0;
  }

  if (bit_start + slice_bits <= SwoWordBits())
  {
    const size_t mask = MakeRangeMarkWord(slice_start,
                                          slice_start + slice_bits,
                                          word_start);
    return (row[word_start] & mask) != 0;
  }

  return RowHasSliceBit(row, row_words, slice_start, slice_bits);
}

static size_t ProjectMarkSliceOneWord(const size_t *src_row,
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

static void CompactMarkToWorkspace(const size_t *mark,
                                   size_t len_items,
                                   size_t src_mark_words,
                                   const bool *selected,
                                   size_t target_size,
                                   size_t slice_start,
                                   size_t slice_bits,
                                   SwoMarkWorkspace &ws)
{
  const size_t dst_mark_words = MarkWords(slice_bits);
  ws.ensure_capacity(len_items, dst_mark_words);
  ws.item_count = std::min(target_size, len_items);
  ws.mark_words_per_item = dst_mark_words;

  if (mark == nullptr || selected == nullptr || len_items == 0 ||
      ws.item_count == 0 || src_mark_words == 0 || dst_mark_words == 0)
  {
    return;
  }

  size_t *dst = ws.mark_ptr();
  if (dst_mark_words == 1)
  {
    for (size_t i = 0; i < len_items; i++)
    {
      dst[i] = ProjectMarkSliceOneWord(mark + (i * src_mark_words),
                                       src_mark_words,
                                       slice_start,
                                       slice_bits);
    }
  }
  else
  {
    for (size_t i = 0; i < len_items; i++)
    {
      ProjectMarkSlice(mark + (i * src_mark_words),
                       src_mark_words,
                       slice_start,
                       slice_bits,
                       dst + (i * dst_mark_words),
                       dst_mark_words);
    }
  }

  TightCompact_v2(reinterpret_cast<unsigned char *>(dst),
                  len_items,
                  sizeof(size_t) * dst_mark_words,
                  const_cast<bool *>(selected));
}

static void CompactDataToWorkspace(const unsigned char *data,
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

static size_t CompactDataInPlace(unsigned char *data,
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

static size_t CONTROLWRITE_WORKSPACE(const size_t *M,
                                     size_t n,
                                     size_t mark_words,
                                     std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t m,
                                     size_t k,
                                     size_t p,
                                     std::vector<SwoMarkWorkspace> &workspaces,
                                     size_t depth);

static ControlReadResult CONTROLREAD_WORKSPACE(unsigned char *D,
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
                                               size_t depth);

std::vector<FrontierNode> FRONTIER(size_t s, size_t l, size_t n, size_t m)
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
  std::vector<FrontierNode> F_left = FRONTIER(s, l_left, n, m);
  std::vector<FrontierNode> F_right = FRONTIER(s + l_left, l_right, n, m);

  F_left.insert(F_left.end(), F_right.begin(), F_right.end());
  return F_left;
}

std::vector<FrontierNode> NEXTNODES(const std::vector<FrontierNode> &F,
                                    size_t k)
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

size_t CONTROLNUM(const std::vector<FrontierNode> &F,
                  size_t n,
                  size_t m,
                  size_t k)
{
  if (k <= 1)
  {
    return 0;
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
  size_t L = 0;
  const std::vector<FrontierNode> empty_frontier;

  for (size_t i = 0; i < V.size(); i++)
  {
    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, V[i].count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);

    const size_t child = CONTROLNUM(empty_frontier, tv, m, V[i].count);
    size_t with_node = 0;
    size_t next_L = 0;
    if (AddOverflowSizeT(L, n, &with_node) ||
        AddOverflowSizeT(with_node, child, &next_L))
    {
      return 0;
    }
    L = next_L;
  }

  return L;
}

std::vector<uint8_t> CONTROLBITS(const std::vector<size_t> &M,
                                 const std::vector<FrontierNode> &F,
                                 size_t n,
                                 size_t m,
                                 size_t k)
{
  const size_t L = CONTROLNUM(F, n, m, k);
  size_t rounded_bits = 0;
  if (AddOverflowSizeT(L, 7, &rounded_bits))
  {
    return std::vector<uint8_t>();
  }
  std::vector<uint8_t> C(rounded_bits / 8, 0);
  const size_t p = CONTROLWRITE(M, C, F, m, k, 0);
  (void)p;
  return C;
}

size_t CONTROLWRITE(const std::vector<size_t> &M,
                    std::vector<uint8_t> &C,
                    const std::vector<FrontierNode> &F,
                    size_t m,
                    size_t k,
                    size_t p)
{
  const size_t mark_words = MarkWords(k);
  if (k <= 1 || mark_words == 0)
  {
    return p;
  }

  const size_t n = (mark_words == 0) ? 0 : (M.size() / mark_words);
  std::vector<SwoMarkWorkspace> workspaces(WorkspaceDepth(k));
  return CONTROLWRITE_WORKSPACE(M.data(),
                                n,
                                mark_words,
                                C,
                                F,
                                m,
                                k,
                                p,
                                workspaces,
                                0);
}

static size_t CONTROLWRITE_WORKSPACE(const size_t *M,
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

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
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

    for (size_t j = 0; j < n; j++)
    {
      const size_t *row = M + (j * mark_words);
      const bool is_selected =
          RowHasSliceBitFast(row, mark_words, node.start, node.count);
      selected[j] = is_selected;
    }
    PackBoolArrayToControlBits(C, p, n, selected);
    if (AddOverflowSizeT(p, n, &p))
    {
      return p;
    }

    SwoMarkWorkspace &child_ws = workspaces[depth];
    CompactMarkToWorkspace(M,
                           n,
                           mark_words,
                           selected,
                           tv,
                           node.start,
                           node.count,
                           child_ws);

    p = CONTROLWRITE_WORKSPACE(child_ws.mark_ptr(),
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

std::vector<unsigned char> RECSAMPLE(const unsigned char *D,
                                     const std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t n,
                                     size_t m,
                                     size_t k,
                                     size_t block_size)
{
  if (D == nullptr)
  {
    return std::vector<unsigned char>();
  }

  const size_t total_blocks = OutputBlocksFor(n, m, k);
  size_t result_bytes = 0;
  if (MulOverflowSizeT(total_blocks, block_size, &result_bytes))
  {
    return std::vector<unsigned char>();
  }

  std::vector<unsigned char> S(result_bytes, 0);
  std::vector<unsigned char> mutable_D;
  size_t input_bytes = 0;
  if (MulOverflowSizeT(n, block_size, &input_bytes))
  {
    return std::vector<unsigned char>();
  }
  mutable_D.resize(input_bytes);
  if (input_bytes > 0)
  {
    std::memcpy(mutable_D.data(), D, input_bytes);
  }
  CONTROLREAD(mutable_D.data(), C, F, n, m, k, block_size,
              S.data(), total_blocks, 0);
  return S;
}

ControlReadResult CONTROLREAD(unsigned char *D,
                              const std::vector<uint8_t> &C,
                              const std::vector<FrontierNode> &F,
                              size_t n,
                              size_t m,
                              size_t k,
                              size_t block_size,
                              unsigned char *S,
                              size_t out_capacity_blocks,
                              size_t p)
{
  std::vector<SwoDataWorkspace> workspaces(WorkspaceDepth(k));
  return CONTROLREAD_WORKSPACE(D,
                               C,
                               F,
                               n,
                               m,
                               k,
                               block_size,
                               S,
                               out_capacity_blocks,
                               p,
                               workspaces,
                               0);
}

static ControlReadResult CONTROLREAD_WORKSPACE(unsigned char *D,
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
      RecursiveShuffle_M2(S, copy_count, block_size);
    }
    result.written_blocks = copy_count;
    return result;
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
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
        CONTROLNUM(empty_frontier, tv_left, m, left_node.count);
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
        CONTROLREAD_WORKSPACE(D,
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
        CONTROLREAD_WORKSPACE(right_ws.data_ptr(),
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
        CONTROLREAD_WORKSPACE(child_ws.data_ptr(),
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

std::vector<unsigned char> FILTERDATA(const unsigned char *D,
                                      const std::vector<uint8_t> &Cv,
                                      size_t n,
                                      size_t t,
                                      size_t block_size)
{
  size_t data_bytes = 0;
  if (D == nullptr || block_size == 0 || MulOverflowSizeT(n, block_size, &data_bytes))
  {
    return std::vector<unsigned char>();
  }

  std::vector<unsigned char> D0(data_bytes, 0);
  if (data_bytes > 0)
  {
    std::memcpy(D0.data(), D, data_bytes);
  }

  bool *selected = AcquireSelectedScratchA(n);
  if (selected == nullptr)
  {
    return std::vector<unsigned char>();
  }
  UnpackControlBitsToBoolArray(Cv, 0, n, selected);

  TightCompact_v2(D0.data(), n, block_size, selected);

  size_t out_bytes = 0;
  t = std::min(t, n);
  if (MulOverflowSizeT(t, block_size, &out_bytes))
  {
    return std::vector<unsigned char>();
  }
  D0.resize(out_bytes);
  return D0;
}

std::vector<size_t> FILTERMARK(const std::vector<size_t> &M,
                               const std::vector<uint8_t> &Cv,
                               size_t t,
                               size_t s,
                               size_t l,
                               size_t k)
{
  const size_t src_words = MarkWords(k);
  const size_t dst_words = MarkWords(l);
  if (src_words == 0 || dst_words == 0)
  {
    return std::vector<size_t>();
  }

  const size_t n = M.size() / src_words;
  t = std::min(t, n);

  std::vector<size_t> projected(n * dst_words, 0);
  if (dst_words == 1)
  {
    for (size_t i = 0; i < n; i++)
    {
      projected[i] = ProjectMarkSliceOneWord(M.data() + (i * src_words),
                                             src_words,
                                             s,
                                             l);
    }
  }
  else
  {
    for (size_t i = 0; i < n; i++)
    {
      ProjectMarkSlice(M.data() + (i * src_words),
                       src_words,
                       s,
                       l,
                       projected.data() + (i * dst_words),
                       dst_words);
    }
  }

  bool *selected = AcquireSelectedScratchB(n);
  if (selected == nullptr)
  {
    return std::vector<size_t>();
  }
  UnpackControlBitsToBoolArray(Cv, 0, n, selected);

  TightCompact_v2(reinterpret_cast<unsigned char *>(projected.data()),
                  n,
                  sizeof(size_t) * dst_words,
                  selected);

  projected.resize(t * dst_words);
  return projected;
}

std::vector<unsigned char> OMBSUBSAMPLE(const unsigned char *D,
                                        size_t n,
                                        size_t m,
                                        size_t k,
                                        size_t block_size)
{
  if (D == nullptr || n == 0 || m == 0 || k == 0 || block_size == 0)
  {
    return std::vector<unsigned char>();
  }
  if (m > n)
  {
    m = n;
  }

  std::vector<size_t> M = MARKMATRIX(n, m, k);

  std::vector<FrontierNode> F;
  size_t mk = 0;
  const bool overflow = MulOverflowSizeT(m, k, &mk);
  if (overflow || mk > n)
  {
    F = FRONTIER(0, k, n, m);
  }

  std::vector<uint8_t> C = CONTROLBITS(M, F, n, m, k);
  return RECSAMPLE(D, C, F, n, m, k, block_size);
}

void DecSuppleSWO(unsigned char *encrypted_buffer,
                  size_t N,
                  size_t M,
                  size_t K,
                  size_t encrypted_block_size,
                  unsigned char *encrypt_result_buffer,
                  enc_ret *ret)
{
  unsigned char *decrypted_buffer = NULL;
  const size_t decrypted_block_size =
      decryptBuffer(encrypted_buffer, static_cast<uint64_t>(N),
                    encrypted_block_size, &decrypted_buffer);

  if (ret == nullptr)
  {
    free(decrypted_buffer);
    return;
  }

  ret->ptime = 0.0;
  ret->gen_perm_time = 0.0;
  ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
  ret->OSWAP_count = 0;
#endif

  if (decrypted_buffer == NULL || decrypted_block_size == static_cast<size_t>(-1))
  {
    free(decrypted_buffer);
    return;
  }

  if (M > N)
  {
    M = N;
  }

  if (M == 0 || K == 0 || encrypt_result_buffer == NULL)
  {
    free(decrypted_buffer);
    return;
  }

  size_t total_blocks = 0;
  if (MulOverflowSizeT(M, K, &total_blocks))
  {
    free(decrypted_buffer);
    return;
  }

  size_t result_bytes = 0;
  if (MulOverflowSizeT(total_blocks, decrypted_block_size, &result_bytes))
  {
    free(decrypted_buffer);
    return;
  }

  PRB_pool_init(1);

  long t0, t1;

  ocall_clock(&t0);
  std::vector<size_t> M_matrix = MARKMATRIX(N, M, K);

  std::vector<FrontierNode> F;
  size_t mk = 0;
  const bool frontier_overflow = MulOverflowSizeT(M, K, &mk);
  if (frontier_overflow || mk > N)
  {
    F = FRONTIER(0, K, N, M);
  }

  std::vector<uint8_t> C = CONTROLBITS(M_matrix, F, N, M, K);
  std::vector<size_t>().swap(M_matrix);
  ocall_clock(&t1);
  ret->gen_perm_time = static_cast<double>(t1 - t0) / 1000.0;

#ifdef COUNT_OSWAPS
  const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif

  std::vector<unsigned char> plain_result(result_bytes, 0);

  ocall_clock(&t0);
  CONTROLREAD(decrypted_buffer, C, F, N, M, K, decrypted_block_size,
              plain_result.data(), total_blocks, 0);
  ocall_clock(&t1);
  ret->apply_perm_time = static_cast<double>(t1 - t0) / 1000.0;
  ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif

  encryptBuffer(plain_result.data(), static_cast<uint64_t>(total_blocks),
                decrypted_block_size, encrypt_result_buffer);

  PRB_pool_shutdown();
  free(decrypted_buffer);
}
