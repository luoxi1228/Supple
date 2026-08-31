#include "SuppleSWO_parallel.hpp"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace
{
struct ParallelDataWorkspace
{
  std::unique_ptr<unsigned char[]> data_buf;
  size_t capacity_bytes = 0;
  size_t item_count = 0;

  unsigned char *data_ptr()
  {
    return data_buf.get();
  }

  bool ensure_capacity(size_t items, size_t block_size)
  {
    size_t required = 0;
    if (MulOverflowSizeT(items, block_size, &required))
    {
      item_count = 0;
      return false;
    }

    if (required > capacity_bytes)
    {
      std::unique_ptr<unsigned char[]> next(
          new (std::nothrow) unsigned char[required]);
      if (!next)
      {
        item_count = 0;
        return false;
      }
      data_buf = std::move(next);
      capacity_bytes = required;
    }

    item_count = items;
    return true;
  }
};

void CompactDataToParallelWorkspace(const unsigned char *data,
                                    size_t len_items,
                                    const bool *selected,
                                    size_t target_size,
                                    size_t block_size,
                                    ParallelDataWorkspace &ws)
{
  ws.item_count = std::min(target_size, len_items);
  if (data == nullptr || selected == nullptr || len_items == 0 ||
      ws.item_count == 0 || block_size == 0)
  {
    return;
  }

  if (!ws.ensure_capacity(len_items, block_size))
  {
    return;
  }
  ws.item_count = std::min(target_size, len_items);

  std::memcpy(ws.data_ptr(), data, len_items * block_size);
  TightCompact_v2(ws.data_ptr(),
                  len_items,
                  block_size,
                  const_cast<bool *>(selected));
}

bool CopyDataToParallelWorkspace(const unsigned char *data,
                                 size_t len_items,
                                 size_t target_size,
                                 size_t block_size,
                                 ParallelDataWorkspace &ws)
{
  ws.item_count = std::min(target_size, len_items);
  if (data == nullptr || len_items == 0 || ws.item_count == 0 || block_size == 0)
  {
    return false;
  }
  if (!ws.ensure_capacity(len_items, block_size))
  {
    return false;
  }
  ws.item_count = std::min(target_size, len_items);
  std::memcpy(ws.data_ptr(), data, len_items * block_size);
  return true;
}

struct SwoParallelWorkspaceContext
{
  std::vector<SwoMarkWorkspace> left_mark;
  std::vector<SwoMarkWorkspace> right_mark;
  std::vector<SwoMarkWorkspace> serial_mark;
  std::vector<ParallelDataWorkspace> left_data;
  std::vector<ParallelDataWorkspace> right_data;
  std::vector<SwoDataWorkspace> serial_data;

  void ensure_depth(size_t depth)
  {
    const size_t needed = depth + 1;
    if (left_mark.size() < needed)
    {
      left_mark.resize(needed);
      right_mark.resize(needed);
      serial_mark.resize(needed);
      left_data.resize(needed);
      right_data.resize(needed);
      serial_data.resize(needed);
    }
  }
};

struct ChildLayout
{
  FrontierNode node;
  size_t input_items;
  size_t root_pos;
  size_t child_pos;
  size_t child_bits;
  size_t output_pos;
  size_t output_blocks;
};

struct ControlNumMemoEntry
{
  size_t n;
  size_t m;
  size_t k;
  size_t value;
};

static const size_t kMinParallelItems = 4096;
static const size_t kParallelWorkspaceBudgetBytes = size_t(256) << 20;

size_t SelectEffectiveParallelism(size_t n,
                                  size_t m,
                                  size_t k,
                                  size_t requested_threads)
{
  size_t effective = std::max<size_t>(1, requested_threads);
  effective = std::min(effective, std::max<size_t>(1, k));
  if (effective <= 1 || k <= 1 || n < kMinParallelItems)
  {
    return 1;
  }
  (void)m;
  return effective;
}

bool ShouldParallelNode(size_t n, size_t k, size_t nthreads)
{
  return nthreads > 1 && k > 1 && n >= kMinParallelItems;
}

size_t SelectFrontierWorkers(size_t requested,
                             size_t task_count,
                             size_t n,
                             size_t block_size)
{
  if (requested <= 1 || task_count <= 1)
  {
    return 1;
  }

  size_t bytes_per_worker = 0;
  size_t item_bytes = 0;
  if (AddOverflowSizeT(block_size, sizeof(uint32_t) + sizeof(bool), &item_bytes) ||
      MulOverflowSizeT(n, item_bytes, &bytes_per_worker) ||
      bytes_per_worker == 0)
  {
    return 1;
  }

  const size_t memory_workers =
      std::max<size_t>(1, kParallelWorkspaceBudgetBytes / bytes_per_worker);
  return std::max<size_t>(1,
                          std::min(std::min(requested, task_count),
                                   memory_workers));
}

struct FrontierThreadGroup
{
  size_t thread_offset;
  size_t thread_count;
  size_t layout_begin;
  size_t layout_end;
};

std::vector<FrontierThreadGroup> BuildFrontierThreadGroups(
    size_t nthreads,
    size_t layout_count,
    size_t active_groups)
{
  active_groups = std::max<size_t>(1,
                                   std::min(std::min(nthreads, layout_count),
                                            active_groups));
  std::vector<FrontierThreadGroup> groups;
  groups.reserve(active_groups);

  size_t thread_offset = 0;
  for (size_t group = 0; group < active_groups; group++)
  {
    const size_t thread_count =
        (nthreads / active_groups) + (group < (nthreads % active_groups) ? 1 : 0);
    FrontierThreadGroup entry;
    entry.thread_offset = thread_offset;
    entry.thread_count = thread_count;
    entry.layout_begin = (layout_count * group) / active_groups;
    entry.layout_end = (layout_count * (group + 1)) / active_groups;
    groups.push_back(entry);
    thread_offset += thread_count;
  }
  return groups;
}

bool CanWriteLayoutsConcurrently(const std::vector<ChildLayout> &layouts)
{
  for (size_t i = 0; i + 1 < layouts.size(); i++)
  {
    const size_t end = layouts[i].child_pos + layouts[i].child_bits;
    if ((end & 7U) != 0)
    {
      return false;
    }
  }
  return true;
}

void FillMarkWordRange(size_t *matrix,
                       size_t n,
                       size_t m,
                       size_t k,
                       size_t mark_words,
                       size_t word_begin,
                       size_t word_end)
{
  if (matrix == nullptr || word_begin >= word_end)
  {
    return;
  }

  const size_t column_begin = word_begin * SwoWordBits();
  const size_t column_end = std::min(k, word_end * SwoWordBits());
  std::vector<uint64_t> left_to_mark(column_end - column_begin, m);

  PRB_buffer *randpool = PRB_pool + g_thread_id;
  static const size_t kMaxCoins = 2048;
  uint32_t coins[kMaxCoins];
  size_t coins_left = 0;

  for (size_t i = 0; i < n; i++)
  {
    const uint64_t remaining = static_cast<uint64_t>(n - i);
    for (size_t w = word_begin; w < word_end; w++)
    {
      size_t mark_word = 0;
      const size_t begin = w * SwoWordBits();
      const size_t end = std::min(begin + SwoWordBits(), k);
      for (size_t j = begin; j < end; j++)
      {
        if (coins_left == 0)
        {
          randpool->getRandomBytes(reinterpret_cast<unsigned char *>(coins),
                                   sizeof(coins));
          coins_left = kMaxCoins;
        }

        const uint32_t random_coin = coins[--coins_left];
        uint64_t &remaining_marks = left_to_mark[j - column_begin];
        const uint64_t threshold = (remaining_marks << 32) / remaining;
        const size_t selected = static_cast<size_t>(random_coin < threshold);
        mark_word |= selected << (j - begin);
        remaining_marks -= selected;
      }
      matrix[(i * mark_words) + w] = mark_word;
    }
  }
}

struct MarkMatrixTaskArgs
{
  size_t *matrix;
  size_t n;
  size_t m;
  size_t k;
  size_t mark_words;
  size_t word_begin;
  size_t word_end;
};

void *MarkMatrixTaskLaunch(void *raw_args)
{
  MarkMatrixTaskArgs *args = static_cast<MarkMatrixTaskArgs *>(raw_args);
  FillMarkWordRange(args->matrix,
                    args->n,
                    args->m,
                    args->k,
                    args->mark_words,
                    args->word_begin,
                    args->word_end);
  return nullptr;
}

std::vector<size_t> MarkMatrixParallel(size_t n,
                                       size_t m,
                                       size_t k,
                                       size_t nthreads)
{
  const size_t mark_words = MarkWords(k);
  size_t matrix_words = 0;
  if (MulOverflowSizeT(n, mark_words, &matrix_words))
  {
    return std::vector<size_t>();
  }
  std::vector<size_t> matrix(matrix_words, 0);
  if (matrix.empty() || mark_words == 0)
  {
    return matrix;
  }

  m = std::min(m, n);
  const size_t workers = std::min(nthreads, mark_words);
  if (workers <= 1)
  {
    FillMarkWordRange(matrix.data(), n, m, k, mark_words, 0, mark_words);
    return matrix;
  }

  std::vector<MarkMatrixTaskArgs> tasks(workers - 1);
  for (size_t worker = 1; worker < workers; worker++)
  {
    MarkMatrixTaskArgs &args = tasks[worker - 1];
    args.matrix = matrix.data();
    args.n = n;
    args.m = m;
    args.k = k;
    args.mark_words = mark_words;
    args.word_begin = (mark_words * worker) / workers;
    args.word_end = (mark_words * (worker + 1)) / workers;
    threadpool_dispatch(g_thread_id + worker, MarkMatrixTaskLaunch, &args);
  }

  FillMarkWordRange(matrix.data(),
                    n,
                    m,
                    k,
                    mark_words,
                    0,
                    mark_words / workers);
  for (size_t worker = 1; worker < workers; worker++)
  {
    threadpool_join(g_thread_id + worker, nullptr);
  }
  return matrix;
}

bool FindControlNumMemo(const std::vector<ControlNumMemoEntry> &memo,
                        size_t n,
                        size_t m,
                        size_t k,
                        size_t *value)
{
  for (size_t i = 0; i < memo.size(); i++)
  {
    if (memo[i].n == n && memo[i].m == m && memo[i].k == k)
    {
      if (value != nullptr)
      {
        *value = memo[i].value;
      }
      return true;
    }
  }
  return false;
}

size_t ControlNumMemoized(const std::vector<FrontierNode> &F,
                          size_t n,
                          size_t m,
                          size_t k,
                          std::vector<ControlNumMemoEntry> *memo)
{
  if (k <= 1)
  {
    return 0;
  }

  if (F.empty() && memo != nullptr)
  {
    size_t cached = 0;
    if (FindControlNumMemo(*memo, n, m, k, &cached))
    {
      return cached;
    }
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
  const std::vector<FrontierNode> empty_frontier;
  size_t L = 0;

  for (size_t i = 0; i < V.size(); i++)
  {
    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, V[i].count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);
    const size_t child =
        ControlNumMemoized(empty_frontier, tv, m, V[i].count, memo);

    size_t with_node = 0;
    size_t next_L = 0;
    if (AddOverflowSizeT(L, n, &with_node) ||
        AddOverflowSizeT(with_node, child, &next_L))
    {
      return 0;
    }
    L = next_L;
  }

  if (F.empty() && memo != nullptr)
  {
    ControlNumMemoEntry entry = {n, m, k, L};
    memo->push_back(entry);
  }

  return L;
}

void BuildControlNumMemo(const std::vector<FrontierNode> &F,
                         size_t n,
                         size_t m,
                         size_t k,
                         std::vector<ControlNumMemoEntry> &memo)
{
  (void)ControlNumMemoized(F, n, m, k, &memo);
}

size_t ControlNumReadOnly(const std::vector<FrontierNode> &F,
                          size_t n,
                          size_t m,
                          size_t k,
                          const std::vector<ControlNumMemoEntry> &memo)
{
  if (k <= 1)
  {
    return 0;
  }

  if (F.empty())
  {
    size_t cached = 0;
    if (FindControlNumMemo(memo, n, m, k, &cached))
    {
      return cached;
    }
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
  const std::vector<FrontierNode> empty_frontier;
  size_t L = 0;
  for (size_t i = 0; i < V.size(); i++)
  {
    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, V[i].count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);
    const size_t child = ControlNumReadOnly(empty_frontier, tv, m, V[i].count, memo);

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

std::vector<ChildLayout> BuildChildLayouts(const std::vector<FrontierNode> &V,
                                           size_t n,
                                           size_t m,
                                           size_t p,
                                           size_t output_base,
                                           const std::vector<ControlNumMemoEntry> &memo)
{
  std::vector<ChildLayout> layouts;
  layouts.reserve(V.size());

  const std::vector<FrontierNode> empty_frontier;
  size_t control_cursor = p;
  size_t output_cursor = output_base;

  for (size_t i = 0; i < V.size(); i++)
  {
    size_t ml = 0;
    const bool overflow = MulOverflowSizeT(m, V[i].count, &ml);
    const size_t tv = overflow ? n : std::min(n, ml);
    const size_t child_bits =
        ControlNumReadOnly(empty_frontier, tv, m, V[i].count, memo);
    const size_t output_blocks = OutputBlocksFor(tv, m, V[i].count);

    ChildLayout layout;
    layout.node = V[i];
    layout.input_items = tv;
    layout.root_pos = control_cursor;
    layout.child_pos = control_cursor + n;
    layout.child_bits = child_bits;
    layout.output_pos = output_cursor;
    layout.output_blocks = output_blocks;
    layouts.push_back(layout);

    control_cursor += n + child_bits;
    output_cursor += output_blocks;
  }

  return layouts;
}

size_t SerialControlWriteFromPointer(const size_t *M,
                                     size_t n,
                                     size_t mark_words,
                                     std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t m,
                                     size_t k,
                                     size_t p)
{
  std::vector<SwoMarkWorkspace> workspaces(WorkspaceDepth(k));
  return CONTROLWRITE_WORKSPACE(M,
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

ControlReadResult SerialControlRead(unsigned char *D,
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

ControlReadResult SerialControlReadWithWorkspace(
    unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &F,
    size_t n,
    size_t m,
    size_t k,
    size_t block_size,
    unsigned char *S,
    size_t out_capacity_blocks,
    size_t p,
    std::vector<SwoDataWorkspace> &workspaces)
{
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

size_t SplitLeftThreads(size_t nthreads)
{
  return nthreads / 2;
}

size_t SplitRightThreads(size_t nthreads)
{
  return nthreads - SplitLeftThreads(nthreads);
}

size_t ControlWriteParallelWorkspace(const size_t *M,
                                     size_t n,
                                     size_t mark_words,
                                     std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t m,
                                     size_t k,
                                     size_t p,
                                     std::vector<SwoParallelWorkspaceContext> &contexts,
                                     size_t depth,
                                     size_t nthreads,
                                     const std::vector<ControlNumMemoEntry> &memo);

ControlReadResult ControlReadParallelWorkspace(unsigned char *D,
                                               const std::vector<uint8_t> &C,
                                               const std::vector<FrontierNode> &F,
                                               size_t n,
                                               size_t m,
                                               size_t k,
                                               size_t block_size,
                                               unsigned char *S,
                                               size_t out_capacity_blocks,
                                               size_t p,
                                               std::vector<SwoParallelWorkspaceContext> &contexts,
                                               size_t depth,
                                               size_t nthreads,
                                               const std::vector<ControlNumMemoEntry> &memo);

size_t PrepareAndWriteChild(const size_t *M,
                            size_t n,
                            size_t mark_words,
                            std::vector<uint8_t> &C,
                            const ChildLayout &layout,
                            size_t m,
                            std::vector<SwoParallelWorkspaceContext> &contexts,
                            size_t depth,
                            size_t nthreads,
                            const std::vector<ControlNumMemoEntry> &memo);

ControlReadResult PrepareAndReadChild(unsigned char *D,
                                      const std::vector<uint8_t> &C,
                                      const ChildLayout &layout,
                                      size_t n,
                                      size_t m,
                                      size_t block_size,
                                      unsigned char *S,
                                      size_t out_capacity_blocks,
                                      std::vector<SwoParallelWorkspaceContext> &contexts,
                                      size_t depth,
                                      size_t nthreads,
                                      const std::vector<ControlNumMemoEntry> &memo);

ControlReadResult ProcessReadLayoutRange(unsigned char *D,
                                         const std::vector<uint8_t> &C,
                                         const std::vector<ChildLayout> &layouts,
                                         size_t begin,
                                         size_t end,
                                         size_t n,
                                         size_t m,
                                         size_t block_size,
                                         unsigned char *S,
                                         size_t out_capacity_blocks,
                                         std::vector<SwoParallelWorkspaceContext> &contexts,
                                         size_t depth,
                                         size_t nthreads,
                                         const std::vector<ControlNumMemoEntry> &memo);

size_t ProcessWriteLayoutRange(const size_t *M,
                               size_t n,
                               size_t mark_words,
                               std::vector<uint8_t> &C,
                               const std::vector<ChildLayout> &layouts,
                               size_t begin,
                               size_t end,
                               size_t m,
                               std::vector<SwoParallelWorkspaceContext> &contexts,
                               size_t depth,
                               size_t nthreads,
                               const std::vector<ControlNumMemoEntry> &memo);

struct ControlWriteTaskArgs
{
  const size_t *M;
  size_t n;
  size_t mark_words;
  std::vector<uint8_t> *C;
  std::vector<FrontierNode> F;
  size_t m;
  size_t k;
  size_t p;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  size_t next_pos;
};

void *ControlWriteTaskLaunch(void *raw_args)
{
  ControlWriteTaskArgs *args = static_cast<ControlWriteTaskArgs *>(raw_args);
  args->next_pos = ControlWriteParallelWorkspace(args->M,
                                                 args->n,
                                                 args->mark_words,
                                                 *args->C,
                                                 args->F,
                                                 args->m,
                                                 args->k,
                                                 args->p,
                                                 *args->contexts,
                                                 args->depth,
                                                 args->nthreads,
                                                 *args->memo);
  return nullptr;
}

struct ControlWriteChildTaskArgs
{
  const size_t *M;
  size_t n;
  size_t mark_words;
  std::vector<uint8_t> *C;
  ChildLayout layout;
  size_t m;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  size_t next_pos;
};

void *ControlWriteChildTaskLaunch(void *raw_args)
{
  ControlWriteChildTaskArgs *args =
      static_cast<ControlWriteChildTaskArgs *>(raw_args);
  args->next_pos = PrepareAndWriteChild(args->M,
                                        args->n,
                                        args->mark_words,
                                        *args->C,
                                        args->layout,
                                        args->m,
                                        *args->contexts,
                                        args->depth,
                                        args->nthreads,
                                        *args->memo);
  return nullptr;
}

struct ControlWriteRangeTaskArgs
{
  const size_t *M;
  size_t n;
  size_t mark_words;
  std::vector<uint8_t> *C;
  const std::vector<ChildLayout> *layouts;
  size_t begin;
  size_t end;
  size_t m;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  size_t next_pos;
};

void *ControlWriteRangeTaskLaunch(void *raw_args)
{
  ControlWriteRangeTaskArgs *args =
      static_cast<ControlWriteRangeTaskArgs *>(raw_args);
  args->next_pos = ProcessWriteLayoutRange(args->M,
                                           args->n,
                                           args->mark_words,
                                           *args->C,
                                           *args->layouts,
                                           args->begin,
                                           args->end,
                                           args->m,
                                           *args->contexts,
                                           args->depth,
                                           args->nthreads,
                                           *args->memo);
  return nullptr;
}

struct ControlReadTaskArgs
{
  unsigned char *D;
  const std::vector<uint8_t> *C;
  std::vector<FrontierNode> F;
  size_t n;
  size_t m;
  size_t k;
  size_t block_size;
  unsigned char *S;
  size_t out_capacity_blocks;
  size_t p;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  ControlReadResult result;
};

void *ControlReadTaskLaunch(void *raw_args)
{
  ControlReadTaskArgs *args = static_cast<ControlReadTaskArgs *>(raw_args);
  args->result = ControlReadParallelWorkspace(args->D,
                                              *args->C,
                                              args->F,
                                              args->n,
                                              args->m,
                                              args->k,
                                              args->block_size,
                                              args->S,
                                              args->out_capacity_blocks,
                                              args->p,
                                              *args->contexts,
                                              args->depth,
                                              args->nthreads,
                                              *args->memo);
  return nullptr;
}

struct CompactAndReadTaskArgs
{
  unsigned char *D;
  size_t source_items;
  size_t target_items;
  const std::vector<uint8_t> *C;
  size_t control_pos;
  size_t m;
  size_t k;
  size_t block_size;
  unsigned char *S;
  size_t out_capacity_blocks;
  size_t child_pos;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  ControlReadResult result;
};

void *CompactAndReadTaskLaunch(void *raw_args)
{
  CompactAndReadTaskArgs *args =
      static_cast<CompactAndReadTaskArgs *>(raw_args);
  args->result = ControlReadResult{0, args->child_pos};

  bool *selected = AcquireSelectedScratchA(args->source_items);
  if (selected == nullptr)
  {
    return nullptr;
  }

  UnpackControlBitsToBoolArray(*args->C,
                               args->control_pos,
                               args->source_items,
                               selected);
  TightCompact_v2(args->D,
                  args->source_items,
                  args->block_size,
                  selected);

  args->result = ControlReadParallelWorkspace(args->D,
                                              *args->C,
                                              std::vector<FrontierNode>(),
                                              args->target_items,
                                              args->m,
                                              args->k,
                                              args->block_size,
                                              args->S,
                                              args->out_capacity_blocks,
                                              args->child_pos,
                                              *args->contexts,
                                              args->depth,
                                              args->nthreads,
                                              *args->memo);
  return nullptr;
}

struct ControlReadChildTaskArgs
{
  unsigned char *D;
  const std::vector<uint8_t> *C;
  ChildLayout layout;
  size_t n;
  size_t m;
  size_t block_size;
  unsigned char *S;
  size_t out_capacity_blocks;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  ControlReadResult result;
};

void *ControlReadChildTaskLaunch(void *raw_args)
{
  ControlReadChildTaskArgs *args =
      static_cast<ControlReadChildTaskArgs *>(raw_args);
  args->result = PrepareAndReadChild(args->D,
                                     *args->C,
                                     args->layout,
                                     args->n,
                                     args->m,
                                     args->block_size,
                                     args->S,
                                     args->out_capacity_blocks,
                                     *args->contexts,
                                     args->depth,
                                     args->nthreads,
                                     *args->memo);
  return nullptr;
}

struct ControlReadRangeTaskArgs
{
  unsigned char *D;
  const std::vector<uint8_t> *C;
  const std::vector<ChildLayout> *layouts;
  size_t begin;
  size_t end;
  size_t n;
  size_t m;
  size_t block_size;
  unsigned char *S;
  size_t out_capacity_blocks;
  std::vector<SwoParallelWorkspaceContext> *contexts;
  size_t depth;
  size_t nthreads;
  const std::vector<ControlNumMemoEntry> *memo;
  ControlReadResult result;
};

void *ControlReadRangeTaskLaunch(void *raw_args)
{
  ControlReadRangeTaskArgs *args =
      static_cast<ControlReadRangeTaskArgs *>(raw_args);
  args->result = ProcessReadLayoutRange(args->D,
                                        *args->C,
                                        *args->layouts,
                                        args->begin,
                                        args->end,
                                        args->n,
                                        args->m,
                                        args->block_size,
                                        args->S,
                                        args->out_capacity_blocks,
                                        *args->contexts,
                                        args->depth,
                                        args->nthreads,
                                        *args->memo);
  return nullptr;
}

size_t ControlWriteParallelWorkspace(const size_t *M,
                                     size_t n,
                                     size_t mark_words,
                                     std::vector<uint8_t> &C,
                                     const std::vector<FrontierNode> &F,
                                     size_t m,
                                     size_t k,
                                     size_t p,
                                     std::vector<SwoParallelWorkspaceContext> &contexts,
                                     size_t depth,
                                     size_t nthreads,
                                     const std::vector<ControlNumMemoEntry> &memo)
{
  if (M == nullptr || k <= 1 || mark_words == 0)
  {
    return p;
  }

  if (!ShouldParallelNode(n, k, nthreads))
  {
    return SerialControlWriteFromPointer(M, n, mark_words, C, F, m, k, p);
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
  const std::vector<FrontierNode> empty_frontier;
  const std::vector<ChildLayout> layouts = BuildChildLayouts(V, n, m, p, 0, memo);

  if (!CanWriteLayoutsConcurrently(layouts))
  {
    return SerialControlWriteFromPointer(M, n, mark_words, C, F, m, k, p);
  }

  if (V.size() == 2 && nthreads > 1)
  {
    const ChildLayout &left = layouts[0];
    const ChildLayout &right = layouts[1];
    const size_t left_threads = SplitLeftThreads(nthreads);
    const size_t right_threads = SplitRightThreads(nthreads);
    const threadid_t left_thread_id = g_thread_id + right_threads;

    ControlWriteChildTaskArgs left_args;
    left_args.M = M;
    left_args.n = n;
    left_args.mark_words = mark_words;
    left_args.C = &C;
    left_args.layout = left;
    left_args.m = m;
    left_args.contexts = &contexts;
    left_args.depth = depth;
    left_args.nthreads = left_threads;
    left_args.memo = &memo;
    left_args.next_pos = left.child_pos + left.child_bits;

    threadpool_dispatch(left_thread_id, ControlWriteChildTaskLaunch, &left_args);

    const size_t right_next = PrepareAndWriteChild(M,
                                                   n,
                                                   mark_words,
                                                   C,
                                                   right,
                                                   m,
                                                   contexts,
                                                   depth,
                                                   right_threads,
                                                   memo);
    (void)right_next;
    threadpool_join(left_thread_id, nullptr);

    return right.child_pos + right.child_bits;
  }

  if (V.size() > 2 && nthreads > 1)
  {
    size_t mark_block_size = 0;
    if (MulOverflowSizeT(mark_words, sizeof(size_t), &mark_block_size))
    {
      return SerialControlWriteFromPointer(M, n, mark_words, C, F, m, k, p);
    }

    const size_t active_groups =
        SelectFrontierWorkers(nthreads, layouts.size(), n, mark_block_size);
    if (active_groups >= 1)
    {
      const std::vector<FrontierThreadGroup> groups =
          BuildFrontierThreadGroups(nthreads, layouts.size(), active_groups);
      std::vector<ControlWriteRangeTaskArgs> tasks(groups.size() - 1);
      for (size_t group = 1; group < groups.size(); group++)
      {
        const FrontierThreadGroup &allocation = groups[group];
        ControlWriteRangeTaskArgs &args = tasks[group - 1];
        args.M = M;
        args.n = n;
        args.mark_words = mark_words;
        args.C = &C;
        args.layouts = &layouts;
        args.begin = allocation.layout_begin;
        args.end = allocation.layout_end;
        args.m = m;
        args.contexts = &contexts;
        args.depth = depth;
        args.nthreads = allocation.thread_count;
        args.memo = &memo;
        args.next_pos = p;
        threadpool_dispatch(g_thread_id + allocation.thread_offset,
                            ControlWriteRangeTaskLaunch,
                            &args);
      }

      const FrontierThreadGroup &local_group = groups[0];
      (void)ProcessWriteLayoutRange(M,
                                    n,
                                    mark_words,
                                    C,
                                    layouts,
                                    local_group.layout_begin,
                                    local_group.layout_end,
                                    m,
                                    contexts,
                                    depth,
                                    local_group.thread_count,
                                    memo);

      for (size_t group = 1; group < groups.size(); group++)
      {
        threadpool_join(g_thread_id + groups[group].thread_offset, nullptr);
      }
      return layouts.back().child_pos + layouts.back().child_bits;
    }
  }

  size_t next_pos = p;
  for (size_t i = 0; i < layouts.size(); i++)
  {
    const ChildLayout &layout = layouts[i];
    if (g_thread_id >= contexts.size())
    {
      return next_pos;
    }
    contexts[g_thread_id].ensure_depth(depth);

    bool *selected = AcquireSelectedScratchA(n);
    if (selected == nullptr)
    {
      return next_pos;
    }

    MarkSliceRange range;
    MakeMarkSliceRange(mark_words, layout.node.start, layout.node.count, &range);

    SwoMarkWorkspace &child_ws = contexts[g_thread_id].left_mark[depth];
    CompactMarkToWorkspaceAndControl(M,
                                     n,
                                     mark_words,
                                     selected,
                                     layout.input_items,
                                     range,
                                     C,
                                     layout.root_pos,
                                     child_ws);

    next_pos = ControlWriteParallelWorkspace(child_ws.mark_ptr(),
                                             child_ws.item_count,
                                             child_ws.mark_words_per_item,
                                             C,
                                             empty_frontier,
                                             m,
                                             layout.node.count,
                                             layout.child_pos,
                                             contexts,
                                             depth + 1,
                                             1,
                                             memo);
  }

  return layouts.empty() ? p : (layouts.back().child_pos + layouts.back().child_bits);
}

size_t PrepareAndWriteChild(const size_t *M,
                            size_t n,
                            size_t mark_words,
                            std::vector<uint8_t> &C,
                            const ChildLayout &layout,
                            size_t m,
                            std::vector<SwoParallelWorkspaceContext> &contexts,
                            size_t depth,
                            size_t nthreads,
                            const std::vector<ControlNumMemoEntry> &memo)
{
  if (g_thread_id >= contexts.size())
  {
    return layout.child_pos;
  }
  contexts[g_thread_id].ensure_depth(depth);

  bool *selected = AcquireSelectedScratchA(n);
  if (selected == nullptr)
  {
    return layout.child_pos;
  }

  MarkSliceRange range;
  MakeMarkSliceRange(mark_words, layout.node.start, layout.node.count, &range);

  SwoMarkWorkspace &child_ws = contexts[g_thread_id].left_mark[depth];
  CompactMarkToWorkspaceAndControl(M,
                                   n,
                                   mark_words,
                                   selected,
                                   layout.input_items,
                                   range,
                                   C,
                                   layout.root_pos,
                                   child_ws);

  return ControlWriteParallelWorkspace(child_ws.mark_ptr(),
                                       child_ws.item_count,
                                       child_ws.mark_words_per_item,
                                       C,
                                       std::vector<FrontierNode>(),
                                       m,
                                       layout.node.count,
                                       layout.child_pos,
                                       contexts,
                                       depth + 1,
                                       nthreads,
                                       memo);
}

ControlReadResult ControlReadParallelWorkspace(unsigned char *D,
                                               const std::vector<uint8_t> &C,
                                               const std::vector<FrontierNode> &F,
                                               size_t n,
                                               size_t m,
                                               size_t k,
                                               size_t block_size,
                                               unsigned char *S,
                                               size_t out_capacity_blocks,
                                               size_t p,
                                               std::vector<SwoParallelWorkspaceContext> &contexts,
                                               size_t depth,
                                               size_t nthreads,
                                               const std::vector<ControlNumMemoEntry> &memo)
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
      // RecursiveShuffle_M2(S, copy_count, block_size);
    }
    result.written_blocks = copy_count;
    return result;
  }

  if (!ShouldParallelNode(n, k, nthreads))
  {
    if (g_thread_id < contexts.size())
    {
      return SerialControlReadWithWorkspace(D,
                                            C,
                                            F,
                                            n,
                                            m,
                                            k,
                                            block_size,
                                            S,
                                            out_capacity_blocks,
                                            p,
                                            contexts[g_thread_id].serial_data);
    }
    return SerialControlRead(D,
                             C,
                             F,
                             n,
                             m,
                             k,
                             block_size,
                             S,
                             out_capacity_blocks,
                             p);
  }

  const std::vector<FrontierNode> V = NEXTNODES(F, k);
  const std::vector<FrontierNode> empty_frontier;
  const std::vector<ChildLayout> layouts = BuildChildLayouts(V, n, m, p, 0, memo);

  if (V.size() == 2 && nthreads > 1)
  {
    if (g_thread_id >= contexts.size())
    {
      return result;
    }
    contexts[g_thread_id].ensure_depth(depth);

    const ChildLayout &left = layouts[0];
    const ChildLayout &right = layouts[1];
    const size_t left_capacity = std::min(out_capacity_blocks, left.output_blocks);
    const size_t right_capacity =
        (out_capacity_blocks > left.output_blocks)
            ? std::min(out_capacity_blocks - left.output_blocks, right.output_blocks)
            : 0;

    const size_t left_threads = SplitLeftThreads(nthreads);
    const size_t right_threads = SplitRightThreads(nthreads);

    bool *selected_left = AcquireSelectedScratchA(n);
    if (selected_left == nullptr)
    {
      return result;
    }

    UnpackControlBitsToBoolArray(C, left.root_pos, n, selected_left);

    ParallelDataWorkspace &right_ws = contexts[g_thread_id].right_data[depth];
    if (!CopyDataToParallelWorkspace(D,
                                     n,
                                     right.input_items,
                                     block_size,
                                     right_ws))
    {
      return result;
    }

    CompactAndReadTaskArgs right_args;
    right_args.D = right_ws.data_ptr();
    right_args.source_items = n;
    right_args.target_items = right.input_items;
    right_args.C = &C;
    right_args.control_pos = right.root_pos;
    right_args.m = m;
    right_args.k = right.node.count;
    right_args.block_size = block_size;
    right_args.S = S + (left.output_blocks * block_size);
    right_args.out_capacity_blocks = right_capacity;
    right_args.child_pos = right.child_pos;
    right_args.contexts = &contexts;
    right_args.depth = depth + 1;
    right_args.nthreads = right_threads;
    right_args.memo = &memo;
    right_args.result = ControlReadResult{0, right.child_pos};

    const threadid_t right_thread_id = g_thread_id + left_threads;
    if (right_capacity > 0)
    {
      threadpool_dispatch(right_thread_id, CompactAndReadTaskLaunch, &right_args);
    }

    const size_t left_items =
        CompactDataInPlace(D, n, selected_left, left.input_items, block_size);
    ControlReadResult left_result =
        ControlReadParallelWorkspace(D,
                                     C,
                                     empty_frontier,
                                     left_items,
                                     m,
                                     left.node.count,
                                     block_size,
                                     S,
                                     left_capacity,
                                     left.child_pos,
                                     contexts,
                                     depth + 1,
                                     left_threads,
                                     memo);

    if (right_capacity > 0)
    {
      threadpool_join(right_thread_id, nullptr);
    }

    result.written_blocks = left_result.written_blocks + right_args.result.written_blocks;
    result.next_pos = right.child_pos + right.child_bits;
    return result;
  }

  if (V.size() > 2 && nthreads > 1)
  {
    const size_t active_groups =
        SelectFrontierWorkers(nthreads, layouts.size(), n, block_size);
    if (active_groups >= 1)
    {
      const std::vector<FrontierThreadGroup> groups =
          BuildFrontierThreadGroups(nthreads, layouts.size(), active_groups);
      std::vector<ControlReadRangeTaskArgs> tasks(groups.size() - 1);
      for (size_t group = 1; group < groups.size(); group++)
      {
        const FrontierThreadGroup &allocation = groups[group];
        ControlReadRangeTaskArgs &args = tasks[group - 1];
        args.D = D;
        args.C = &C;
        args.layouts = &layouts;
        args.begin = allocation.layout_begin;
        args.end = allocation.layout_end;
        args.n = n;
        args.m = m;
        args.block_size = block_size;
        args.S = S;
        args.out_capacity_blocks = out_capacity_blocks;
        args.contexts = &contexts;
        args.depth = depth;
        args.nthreads = allocation.thread_count;
        args.memo = &memo;
        args.result = ControlReadResult{0, p};
        threadpool_dispatch(g_thread_id + allocation.thread_offset,
                            ControlReadRangeTaskLaunch,
                            &args);
      }

      const FrontierThreadGroup &local_group = groups[0];
      ControlReadResult local =
          ProcessReadLayoutRange(D,
                                 C,
                                 layouts,
                                 local_group.layout_begin,
                                 local_group.layout_end,
                                 n,
                                 m,
                                 block_size,
                                 S,
                                 out_capacity_blocks,
                                 contexts,
                                 depth,
                                 local_group.thread_count,
                                 memo);

      size_t written_total = local.written_blocks;
      for (size_t group = 1; group < groups.size(); group++)
      {
        threadpool_join(g_thread_id + groups[group].thread_offset, nullptr);
        written_total += tasks[group - 1].result.written_blocks;
      }

      result.written_blocks = written_total;
      result.next_pos = layouts.empty()
                            ? p
                            : layouts.back().child_pos + layouts.back().child_bits;
      return result;
    }
  }

  size_t written = 0;
  for (size_t i = 0; i < layouts.size(); i++)
  {
    const ChildLayout &layout = layouts[i];
    if (g_thread_id >= contexts.size())
    {
      result.written_blocks = written;
      result.next_pos = layout.root_pos;
      return result;
    }
    contexts[g_thread_id].ensure_depth(depth);

    bool *selected = AcquireSelectedScratchA(n);
    if (selected == nullptr)
    {
      result.written_blocks = written;
      result.next_pos = layout.root_pos;
      return result;
    }

    UnpackControlBitsToBoolArray(C, layout.root_pos, n, selected);

    ParallelDataWorkspace &child_ws = contexts[g_thread_id].left_data[depth];
    CompactDataToParallelWorkspace(D,
                                   n,
                                   selected,
                                   layout.input_items,
                                   block_size,
                                   child_ws);
    if (child_ws.data_ptr() == nullptr || child_ws.item_count == 0)
    {
      result.written_blocks = written;
      result.next_pos = layout.root_pos;
      return result;
    }

    const size_t child_capacity =
        (out_capacity_blocks > written)
            ? std::min(out_capacity_blocks - written, layout.output_blocks)
            : 0;
    if (child_capacity == 0)
    {
      break;
    }

    ControlReadResult child =
        ControlReadParallelWorkspace(child_ws.data_ptr(),
                                     C,
                                     empty_frontier,
                                     child_ws.item_count,
                                     m,
                                     layout.node.count,
                                     block_size,
                                     S + (written * block_size),
                                     child_capacity,
                                     layout.child_pos,
                                     contexts,
                                     depth + 1,
                                     1,
                                     memo);

    written += child.written_blocks;
    if (written >= out_capacity_blocks)
    {
      break;
    }
  }

  result.written_blocks = written;
  result.next_pos = layouts.empty() ? p : (layouts.back().child_pos + layouts.back().child_bits);
  return result;
}

ControlReadResult PrepareAndReadChild(unsigned char *D,
                                      const std::vector<uint8_t> &C,
                                      const ChildLayout &layout,
                                      size_t n,
                                      size_t m,
                                      size_t block_size,
                                      unsigned char *S,
                                      size_t out_capacity_blocks,
                                      std::vector<SwoParallelWorkspaceContext> &contexts,
                                      size_t depth,
                                      size_t nthreads,
                                      const std::vector<ControlNumMemoEntry> &memo)
{
  ControlReadResult result = {0, layout.child_pos};
  if (g_thread_id >= contexts.size())
  {
    return result;
  }
  contexts[g_thread_id].ensure_depth(depth);

  bool *selected = AcquireSelectedScratchA(n);
  if (selected == nullptr)
  {
    return result;
  }

  UnpackControlBitsToBoolArray(C, layout.root_pos, n, selected);

  ParallelDataWorkspace &child_ws = contexts[g_thread_id].left_data[depth];
  CompactDataToParallelWorkspace(D,
                                 n,
                                 selected,
                                 layout.input_items,
                                 block_size,
                                 child_ws);
  if (child_ws.data_ptr() == nullptr || child_ws.item_count == 0)
  {
    return result;
  }

  return ControlReadParallelWorkspace(child_ws.data_ptr(),
                                      C,
                                      std::vector<FrontierNode>(),
                                      child_ws.item_count,
                                      m,
                                      layout.node.count,
                                      block_size,
                                      S,
                                      out_capacity_blocks,
                                      layout.child_pos,
                                      contexts,
                                      depth + 1,
                                      nthreads,
                                      memo);
}

ControlReadResult ProcessReadLayoutRange(unsigned char *D,
                                         const std::vector<uint8_t> &C,
                                         const std::vector<ChildLayout> &layouts,
                                         size_t begin,
                                         size_t end,
                                         size_t n,
                                         size_t m,
                                         size_t block_size,
                                         unsigned char *S,
                                         size_t out_capacity_blocks,
                                         std::vector<SwoParallelWorkspaceContext> &contexts,
                                         size_t depth,
                                         size_t nthreads,
                                         const std::vector<ControlNumMemoEntry> &memo)
{
  ControlReadResult result = {0, begin < layouts.size() ? layouts[begin].root_pos : 0};
  size_t written = 0;
  end = std::min(end, layouts.size());

  for (size_t i = begin; i < end; i++)
  {
    const ChildLayout &layout = layouts[i];
    if (layout.output_pos >= out_capacity_blocks)
    {
      break;
    }

    const size_t child_capacity =
        std::min(out_capacity_blocks - layout.output_pos, layout.output_blocks);
    if (child_capacity == 0)
    {
      continue;
    }

    ControlReadResult child =
        PrepareAndReadChild(D,
                            C,
                            layout,
                            n,
                            m,
                            block_size,
                            S + (layout.output_pos * block_size),
                            child_capacity,
                            contexts,
                            depth,
                            nthreads,
                            memo);
    written += child.written_blocks;
    result.next_pos = child.next_pos;
  }

  result.written_blocks = written;
  if (end > begin && end <= layouts.size())
  {
    result.next_pos = layouts[end - 1].child_pos + layouts[end - 1].child_bits;
  }
  return result;
}

size_t ProcessWriteLayoutRange(const size_t *M,
                               size_t n,
                               size_t mark_words,
                               std::vector<uint8_t> &C,
                               const std::vector<ChildLayout> &layouts,
                               size_t begin,
                               size_t end,
                               size_t m,
                               std::vector<SwoParallelWorkspaceContext> &contexts,
                               size_t depth,
                               size_t nthreads,
                               const std::vector<ControlNumMemoEntry> &memo)
{
  end = std::min(end, layouts.size());
  size_t next_pos = begin < end ? layouts[begin].root_pos : 0;
  for (size_t i = begin; i < end; i++)
  {
    next_pos = PrepareAndWriteChild(M,
                                    n,
                                    mark_words,
                                    C,
                                    layouts[i],
                                    m,
                                    contexts,
                                    depth,
                                    nthreads,
                                    memo);
  }
  return next_pos;
}
}

std::vector<uint8_t> CONTROLBITS_PARALLEL(const std::vector<size_t> &M,
                                          const std::vector<FrontierNode> &F,
                                          size_t n,
                                          size_t m,
                                          size_t k,
                                          size_t nthreads)
{
  const size_t L = CONTROLNUM(F, n, m, k);
  size_t rounded_bits = 0;
  if (AddOverflowSizeT(L, 7, &rounded_bits))
  {
    return std::vector<uint8_t>();
  }

  std::vector<uint8_t> C(rounded_bits / 8, 0);
  const size_t mark_words = MarkWords(k);
  if (k <= 1 || mark_words == 0 || M.size() < n * mark_words)
  {
    return C;
  }

  nthreads = SelectEffectiveParallelism(n, m, k, nthreads);
  if (nthreads <= 1)
  {
    (void)SerialControlWriteFromPointer(M.data(),
                                        n,
                                        mark_words,
                                        C,
                                        F,
                                        m,
                                        k,
                                        0);
    return C;
  }

  std::vector<ControlNumMemoEntry> memo;
  BuildControlNumMemo(F, n, m, k, memo);
  std::vector<SwoParallelWorkspaceContext> contexts(nthreads);
  const size_t context_depth = WorkspaceDepth(k) + 1;
  for (size_t i = 0; i < contexts.size(); i++)
  {
    contexts[i].ensure_depth(context_depth);
  }

  (void)ControlWriteParallelWorkspace(M.data(),
                                      n,
                                      mark_words,
                                      C,
                                      F,
                                      m,
                                      k,
                                      0,
                                      contexts,
                                      0,
                                      nthreads,
                                      memo);
  return C;
}

size_t CONTROLWRITE_PARALLEL(const std::vector<size_t> &M,
                             std::vector<uint8_t> &C,
                             const std::vector<FrontierNode> &F,
                             size_t m,
                             size_t k,
                             size_t p,
                             size_t nthreads)
{
  const size_t mark_words = MarkWords(k);
  if (k <= 1 || mark_words == 0)
  {
    return p;
  }

  const size_t n = M.size() / mark_words;
  nthreads = SelectEffectiveParallelism(n, m, k, nthreads);
  if (nthreads <= 1)
  {
    return SerialControlWriteFromPointer(M.data(), n, mark_words, C, F, m, k, p);
  }

  std::vector<ControlNumMemoEntry> memo;
  BuildControlNumMemo(F, n, m, k, memo);
  std::vector<SwoParallelWorkspaceContext> contexts(nthreads);
  const size_t context_depth = WorkspaceDepth(k) + 1;
  for (size_t i = 0; i < contexts.size(); i++)
  {
    contexts[i].ensure_depth(context_depth);
  }
  return ControlWriteParallelWorkspace(M.data(),
                                       n,
                                       mark_words,
                                       C,
                                       F,
                                       m,
                                       k,
                                       p,
                                       contexts,
                                       0,
                                       nthreads,
                                       memo);
}

ControlReadResult CONTROLREAD_PARALLEL(unsigned char *D,
                                       const std::vector<uint8_t> &C,
                                       const std::vector<FrontierNode> &F,
                                       size_t n,
                                       size_t m,
                                       size_t k,
                                       size_t block_size,
                                       unsigned char *S,
                                       size_t out_capacity_blocks,
                                       size_t p,
                                       size_t nthreads)
{
  nthreads = SelectEffectiveParallelism(n, m, k, nthreads);
  if (nthreads <= 1)
  {
    return SerialControlRead(D,
                             C,
                             F,
                             n,
                             m,
                             k,
                             block_size,
                             S,
                             out_capacity_blocks,
                             p);
  }

  std::vector<ControlNumMemoEntry> memo;
  BuildControlNumMemo(F, n, m, k, memo);
  std::vector<SwoParallelWorkspaceContext> contexts(nthreads);
  const size_t context_depth = WorkspaceDepth(k) + 1;
  for (size_t i = 0; i < contexts.size(); i++)
  {
    contexts[i].ensure_depth(context_depth);
  }
  return ControlReadParallelWorkspace(D,
                                      C,
                                      F,
                                      n,
                                      m,
                                      k,
                                      block_size,
                                      S,
                                      out_capacity_blocks,
                                      p,
                                      contexts,
                                      0,
                                      nthreads,
                                      memo);
}

std::vector<unsigned char> RECSAMPLE_PARALLEL(const unsigned char *D,
                                             const std::vector<uint8_t> &C,
                                             const std::vector<FrontierNode> &F,
                                             size_t n,
                                             size_t m,
                                             size_t k,
                                             size_t block_size,
                                             size_t nthreads)
{
  if (D == nullptr)
  {
    return std::vector<unsigned char>();
  }

  const size_t total_blocks = OutputBlocksFor(n, m, k);
  size_t result_bytes = 0;
  size_t input_bytes = 0;
  if (MulOverflowSizeT(total_blocks, block_size, &result_bytes) ||
      MulOverflowSizeT(n, block_size, &input_bytes))
  {
    return std::vector<unsigned char>();
  }

  std::vector<unsigned char> S(result_bytes, 0);
  std::vector<unsigned char> mutable_D(input_bytes, 0);
  if (input_bytes > 0)
  {
    std::memcpy(mutable_D.data(), D, input_bytes);
  }

  CONTROLREAD_PARALLEL(mutable_D.data(),
                       C,
                       F,
                       n,
                       m,
                       k,
                       block_size,
                       S.data(),
                       total_blocks,
                       0,
                       nthreads);
  return S;
}

std::vector<unsigned char> OMBSUBSAMPLE_PARALLEL(const unsigned char *D,
                                                size_t n,
                                                size_t m,
                                                size_t k,
                                                size_t block_size,
                                                size_t nthreads)
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

  std::vector<uint8_t> C = CONTROLBITS_PARALLEL(M, F, n, m, k, nthreads);
  return RECSAMPLE_PARALLEL(D, C, F, n, m, k, block_size, nthreads);
}

extern "C" void DecSuppleSWO_parallel(unsigned char *encrypted_buffer,
                                      size_t N,
                                      size_t M,
                                      size_t K,
                                      size_t encrypted_block_size,
                                      unsigned char *encrypt_result_buffer,
                                      enc_ret *ret,
                                      size_t nthreads)
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

  nthreads = SelectEffectiveParallelism(N, M, K, nthreads);

  size_t total_blocks = 0;
  size_t result_bytes = 0;
  if (MulOverflowSizeT(M, K, &total_blocks) ||
      MulOverflowSizeT(total_blocks, decrypted_block_size, &result_bytes))
  {
    free(decrypted_buffer);
    return;
  }

  PRB_pool_init(static_cast<int>(nthreads));
  bool threadpool_started = false;
  if (nthreads > 1)
  {
    if (threadpool_init(nthreads) == 0)
    {
      threadpool_started = true;
    }
    else
    {
      nthreads = 1;
    }
  }

  long t0, t1;

  ocall_clock(&t0);
  std::vector<size_t> M_matrix = MarkMatrixParallel(N, M, K, nthreads);

  std::vector<FrontierNode> F;
  size_t mk = 0;
  const bool frontier_overflow = MulOverflowSizeT(M, K, &mk);
  if (frontier_overflow || mk > N)
  {
    F = FRONTIER(0, K, N, M);
  }

  std::vector<uint8_t> C = CONTROLBITS_PARALLEL(M_matrix, F, N, M, K, nthreads);
  std::vector<size_t>().swap(M_matrix);
  ocall_clock(&t1);
  ret->gen_perm_time = static_cast<double>(t1 - t0) / 1000.0;

#ifdef COUNT_OSWAPS
  const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif

  std::vector<unsigned char> plain_result(result_bytes, 0);

  ocall_clock(&t0);
  CONTROLREAD_PARALLEL(decrypted_buffer,
                       C,
                       F,
                       N,
                       M,
                       K,
                       decrypted_block_size,
                       plain_result.data(),
                       total_blocks,
                       0,
                       nthreads);
  ocall_clock(&t1);
  ret->apply_perm_time = static_cast<double>(t1 - t0) / 1000.0;
  ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif

  encryptBuffer(plain_result.data(), static_cast<uint64_t>(total_blocks),
                decrypted_block_size, encrypt_result_buffer);

  if (threadpool_started)
  {
    threadpool_shutdown();
  }
  PRB_pool_shutdown();
  free(decrypted_buffer);
}
