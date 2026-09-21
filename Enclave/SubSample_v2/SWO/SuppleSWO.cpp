#include "SuppleSWO.hpp"
#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
#include <stdexcept>

using namespace swo_detail;

// SWOSample orchestrates the offline membership/control and online data phases.
std::vector<unsigned char> SWOSample(const unsigned char *D,size_t n, size_t m, size_t k,size_t block_size)
{
  if (D == nullptr || n == 0 || m == 0 || k == 0 || block_size == 0)
    return {};
  m = std::min(m, n); // Retain the existing C++ boundary behavior.
  std::vector<FrontierNode> F;
  size_t mk = 0;
  if (MulOverflowSizeT(m, k, &mk) || mk > n)
    F = SWOFrontier(n, m, k);
  const std::vector<size_t> M = SWOMark(n, m, k);
  const std::vector<uint8_t> C = SWOControl(M, F, n, m, k);
  return SWOApply(D, C, F, n, m, k, block_size);
}

// Each record carries k membership bits, stored in consecutive machine words.
std::vector<size_t> SWOMark(size_t n, size_t m, size_t k)
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

std::vector<FrontierNode> SWOFrontier(size_t n, size_t m, size_t k)
{
  if (k == 0)
    return {};
  std::vector<FrontierNode> F{{0, k}};
  for (size_t v = 0; v < F.size();)
  {
    const FrontierNode node = F[v];
    size_t capacity = 0;
    const bool overflow = MulOverflowSizeT(m, node.count, &capacity);
    if (node.count == 1 || (!overflow && capacity <= n))
    {
      ++v;
    }
    else
    {
      const size_t left = node.count / 2;
      F[v] = {node.start, left};
      F.insert(F.begin() + v + 1, {node.start + left, node.count - left});
    }
  }
  return F;
}

std::vector<uint8_t> SWOControl(const std::vector<size_t> &M,const std::vector<FrontierNode> &F,size_t n, size_t m, size_t k)
{
  const std::vector<FrontierNode> nodes = SwoRootNodes(F, k);
  const size_t bits = SWOControlCount(nodes, n, m);
  std::vector<uint8_t> C(bits / 8 + (bits % 8 != 0), 0);
  if (SWOControlWrite(M, C, nodes, n, m, 0) != bits)
    throw std::logic_error("SWO control write/count mismatch");
  return C;
}

std::vector<unsigned char> SWOApply(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k,
    size_t block_size)
{
  size_t blocks = 0, bytes = 0;
  if (MulOverflowSizeT(m, k, &blocks) || MulOverflowSizeT(blocks, block_size, &bytes))
    throw std::length_error("SWO output size overflow");
  const std::vector<FrontierNode> nodes = SwoRootNodes(F, k);
  const size_t bits = SWOControlCount(nodes, n, m);
  if (C.size() != bits / 8 + (bits % 8 != 0))
    throw std::length_error("SWO control array length mismatch");
  std::vector<unsigned char> S(bytes);
  const ControlReadResult result =
      SWOControlRead(D, C, nodes, n, m, block_size, S.data(), blocks, 0);
  if (result.next_pos != bits || result.written_blocks != blocks)
    throw std::logic_error("SWO control consumption/output count mismatch");
  return S;
}

size_t SWOControlCount(const std::vector<FrontierNode> &nodes, size_t n, size_t m)
{
  size_t bits = 0;
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = SwoNodeCapacity(n, m, node.count);
    const size_t edge_bits = nv < n ? n : 0;
    const size_t child_bits = node.count > 1
        ? SWOControlCount(SwoChildren(node.count), nv, m) : 0;
    if (AddOverflowSizeT(bits, edge_bits, &bits) ||
        AddOverflowSizeT(bits, child_bits, &bits))
      throw std::length_error("SWO control count overflow");
  }
  return bits;
}

// Internal overloads reuse one workspace per recursion depth. Public overloads
// allocate the workspace once; control offsets always count bits.
static size_t SWOControlWrite(const size_t *M, size_t mark_words,
    std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p,
    std::vector<SwoMarkWorkspace> &workspaces, size_t depth)
{
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = SwoNodeCapacity(n, m, node.count);
    SwoMarkWorkspace &child = workspaces[depth];
    if (nv < n)
    {
      SwoCheckControlSpan(C, p, n);
      bool *selected = AcquireSelectedScratch(n);
      if (selected == nullptr)
        throw std::bad_alloc();
      // Projecting before Compact saves metadata bandwidth. The permutation
      // depends only on the flags, so this commutes with the LaTeX projection.
      CompactMarkToWorkspaceAndControl(M, n, mark_words, selected, nv,
                                       node.start, node.count, C, p, child);
      p += n;
    }
    else
    {
      // No selection flags or Compact; membership still needs re-numbering.
      ProjectMarkToWorkspace(M, n, mark_words, node.start, node.count, child);
    }
    if (node.count > 1)
      p = SWOControlWrite(child.mark_ptr(), MarkWords(node.count), C,
                        SwoChildren(node.count), nv, m, p, workspaces, depth + 1);
  }
  return p;
}

size_t SWOControlWrite(const std::vector<size_t> &M, std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p)
{
  if (n == 0 || M.size() % n != 0 || M.empty())
    throw std::invalid_argument("SWO membership dimensions mismatch");
  std::vector<SwoMarkWorkspace> workspaces(SwoNodeDepth(nodes));
  return SWOControlWrite(M.data(), M.size() / n, C, nodes, n, m, p, workspaces, 0);
}

static ControlReadResult SWOControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p, std::vector<SwoDataWorkspace> &workspaces,
    size_t depth)
{
  size_t written = 0;
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = SwoNodeCapacity(n, m, node.count);
    SwoDataWorkspace &child = workspaces[depth];
    child.ensure_capacity(n, block_size);
    // Keep the parent unchanged: all Frontier targets refer to the same D.
    std::memcpy(child.data_ptr(), D, n * block_size);
    if (nv < n)
    {
      SwoCheckControlSpan(C, p, n);
      bool *selected = AcquireSelectedScratch(n);
      if (selected == nullptr)
        throw std::bad_alloc();
      UnpackControlBitsToBoolArray(C, p, n, selected);
      p += n;
      TightCompact_v2(child.data_ptr(), n, block_size, selected); //非稳定版本
    }
    if (node.count == 1)
    {
      if (nv != m || m > out_capacity_blocks - written)
        throw std::length_error("SWO leaf output capacity mismatch");
      unsigned char *sample = S + written * block_size;
      std::memcpy(sample, child.data_ptr(), m * block_size);
      RecursiveShuffle_M2(sample, m, block_size);
      written += m;
    }
    else
    {
      const ControlReadResult result =
          SWOControlRead(child.data_ptr(), C, SwoChildren(node.count), nv, m,
                        block_size, S + written * block_size,
                        out_capacity_blocks - written, p, workspaces, depth + 1);
      written += result.written_blocks;
      p = result.next_pos;
    }
  }
  return {written, p};
}

ControlReadResult SWOControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p)
{
  size_t bytes = 0;
  if (D == nullptr || S == nullptr || block_size == 0 ||
      MulOverflowSizeT(n, block_size, &bytes) ||
      MulOverflowSizeT(out_capacity_blocks, block_size, &bytes))
    throw std::invalid_argument("Invalid SWO data/output buffer dimensions");
  std::vector<SwoDataWorkspace> workspaces(SwoNodeDepth(nodes));
  return SWOControlRead(D, C, nodes, n, m, block_size, S, out_capacity_blocks,
                      p, workspaces, 0);
}

// ECALL adapter: encryption, PRB lifetime and timing wrap the serial algorithms.
extern "C" void DecSuppleSWO(unsigned char *encrypted_buffer,
    size_t N,
    size_t M,
    size_t K,
    size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer,
    enc_ret *ret)
{
  using namespace swo_detail;
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
  std::vector<size_t> M_matrix = SWOMark(N, M, K);

  std::vector<FrontierNode> F;
  size_t mk = 0;
  const bool frontier_overflow = MulOverflowSizeT(M, K, &mk);
  if (frontier_overflow || mk > N)
  {
    F = SWOFrontier(N, M, K);
  }

  const std::vector<FrontierNode> nodes = SwoRootNodes(F, K);
  const size_t control_bits = SWOControlCount(nodes, N, M);
  std::vector<uint8_t> C = SWOControl(M_matrix, F, N, M, K);
  std::vector<size_t>().swap(M_matrix);
  ocall_clock(&t1);
  ret->gen_perm_time = static_cast<double>(t1 - t0) / 1000.0;

#ifdef COUNT_OSWAPS
  const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif

  std::vector<unsigned char> plain_result(result_bytes, 0);

  ocall_clock(&t0);
  const ControlReadResult result =
      SWOControlRead(decrypted_buffer, C, nodes, N, M, decrypted_block_size,
                     plain_result.data(), total_blocks, 0);
  ocall_clock(&t1);
  ret->apply_perm_time = static_cast<double>(t1 - t0) / 1000.0;
  ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif

  if (result.next_pos == control_bits && result.written_blocks == total_blocks)
  {
    encryptBuffer(plain_result.data(), static_cast<uint64_t>(total_blocks),
                  decrypted_block_size, encrypt_result_buffer);
  }

  PRB_pool_shutdown();
  free(decrypted_buffer);
}
