#include "FFOS_C.hpp"
#include "../../MemoryProfile.hpp"
#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include "../OnlineProfile.hpp"
#include "../OfflineProfile.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
#include <stdexcept>

using namespace ffos_c_detail;

// FFOS_C orchestrates the offline membership/control and online data phases.
std::vector<unsigned char> FFOS_C(const unsigned char *D,size_t n, size_t m, size_t k,size_t block_size)
{
  if (D == nullptr || n == 0 || m == 0 || k == 0 || block_size == 0)
    return {};
  m = std::min(m, n); // Retain the existing C++ boundary behavior.
  std::vector<FrontierNode> F;
  size_t mk = 0;
  if (MulOverflowSizeT(m, k, &mk) || mk > n)
    F = FFOS_CFrontier(n, m, k);
  const std::vector<size_t> M = FFOS_CMark(n, m, k);
  const std::vector<uint8_t> C = FFOS_CControl(M, F, n, m, k);
  return FFOS_CApply(D, C, F, n, m, k, block_size);
}

// Each record carries k membership bits, stored in consecutive machine words.
std::vector<size_t> FFOS_CMark(size_t n, size_t m, size_t k)
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
      const size_t begin = w * FfosCWordBits();
      const size_t end = std::min(begin + FfosCWordBits(), k);

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

std::vector<FrontierNode> FFOS_CFrontier(size_t n, size_t m, size_t k)
{
  if (k == 0)
    return {};
  if (m == 0 || m > n)
    throw std::invalid_argument("Invalid FFOS_C frontier dimensions");

  const size_t max_samples_per_node = n / m;
  const size_t node_count = k / max_samples_per_node +
                            (k % max_samples_per_node != 0);
  const size_t base_count = k / node_count;
  const size_t larger_nodes = k % node_count;
  std::vector<FrontierNode> F;
  F.reserve(node_count);
  size_t start = 0;
  for (size_t v = 0; v < node_count; ++v)
  {
    const size_t count = base_count + (v < larger_nodes);
    F.push_back({start, count});
    start += count;
  }
  return F;
}

std::vector<uint8_t> FFOS_CControl(const std::vector<size_t> &M,const std::vector<FrontierNode> &F,size_t n, size_t m, size_t k)
{
  const std::vector<FrontierNode> nodes = FfosCRootNodes(F, k);
  const size_t bits = FFOS_CControlCount(nodes, n, m);
  std::vector<uint8_t> C(bits / 8 + (bits % 8 != 0), 0);
  if (FFOS_CControlWrite(M, C, nodes, n, m, 0) != bits)
    throw std::logic_error("FFOS_C control write/count mismatch");
  return C;
}

std::vector<unsigned char> FFOS_CApply(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &F,
    size_t n, size_t m, size_t k,
    size_t block_size)
{
  size_t blocks = 0, bytes = 0;
  if (MulOverflowSizeT(m, k, &blocks) || MulOverflowSizeT(blocks, block_size, &bytes))
    throw std::length_error("FFOS_C output size overflow");
  const std::vector<FrontierNode> nodes = FfosCRootNodes(F, k);
  const size_t bits = FFOS_CControlCount(nodes, n, m);
  if (C.size() != bits / 8 + (bits % 8 != 0))
    throw std::length_error("FFOS_C control array length mismatch");
  std::vector<unsigned char> S(bytes);
  const ControlReadResult result =
      FFOS_CControlRead(D, C, nodes, n, m, block_size, S.data(), blocks, 0);
  if (result.next_pos != bits || result.written_blocks != blocks)
    throw std::logic_error("FFOS_C control consumption/output count mismatch");
  return S;
}

size_t FFOS_CControlCount(const std::vector<FrontierNode> &nodes, size_t n, size_t m)
{
  size_t bits = 0;
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = FfosCNodeCapacity(n, m, node.count);
    const size_t edge_bits = nv < n ? n : 0;
    const size_t child_bits = node.count > 1
        ? FFOS_CControlCount(FfosCChildren(node.count), nv, m) : 0;
    if (AddOverflowSizeT(bits, edge_bits, &bits) ||
        AddOverflowSizeT(bits, child_bits, &bits))
      throw std::length_error("FFOS_C control count overflow");
  }
  return bits;
}

// Internal overloads reuse one workspace per recursion depth. Public overloads
// allocate the workspace once; control offsets always count bits.
static size_t FFOS_CControlWrite(const size_t *M, size_t mark_words,
    std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p,
    std::vector<FfosCMarkWorkspace> &workspaces,
    SelectedScratch &selected_scratch, size_t depth)
{
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = FfosCNodeCapacity(n, m, node.count);
    FfosCMarkWorkspace &child = workspaces[depth];
    if (nv < n)
    {
      FfosCCheckControlSpan(C, p, n);
      bool *selected = selected_scratch.acquire(n);
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
      p = FFOS_CControlWrite(child.mark_ptr(), MarkWords(node.count), C,
                        FfosCChildren(node.count), nv, m, p, workspaces,
                        selected_scratch, depth + 1);
  }
  return p;
}

size_t FFOS_CControlWrite(const std::vector<size_t> &M, std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t p)
{
  if (n == 0 || M.size() % n != 0 || M.empty())
    throw std::invalid_argument("FFOS_C membership dimensions mismatch");
  std::vector<FfosCMarkWorkspace> workspaces(FfosCNodeDepth(nodes));
  SelectedScratch selected_scratch;
  return FFOS_CControlWrite(M.data(), M.size() / n, C, nodes, n, m, p, workspaces,
                         selected_scratch, 0);
}

static ControlReadResult FFOS_CControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p, std::vector<FfosCDataWorkspace> &workspaces,
    SelectedScratch &selected_scratch, size_t depth, enc_ret *profile)
{
  double *copy_ms = profile ? &profile->online_copy_ms : nullptr;
  double *route_ms = profile ? &profile->online_route_ms : nullptr;
  double *shuffle_ms = profile ? &profile->online_shuffle_ms : nullptr;
  size_t written = 0;
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = FfosCNodeCapacity(n, m, node.count);
    FfosCDataWorkspace &child = workspaces[depth];
    online_profile::Track(copy_ms, [&] {
      child.ensure_capacity(n, block_size);
      // Keep the parent unchanged: all Frontier targets refer to the same D.
      std::memcpy(child.data_ptr(), D, n * block_size);
    });
    if (nv < n)
    {
      FfosCCheckControlSpan(C, p, n);
      bool *selected = selected_scratch.acquire(n);
      if (selected == nullptr)
        throw std::bad_alloc();
      online_profile::Track(route_ms, [&] {
        UnpackControlBitsToBoolArray(C, p, n, selected);
        TightCompact_v2(child.data_ptr(), n, block_size, selected);
      });
      p += n;
    }
    if (node.count == 1)
    {
      if (nv != m || m > out_capacity_blocks - written)
        throw std::length_error("FFOS_C leaf output capacity mismatch");
      unsigned char *sample = S + written * block_size;
      online_profile::Track(copy_ms, [&] {
        std::memcpy(sample, child.data_ptr(), m * block_size);
      });
      online_profile::Track(shuffle_ms, [&] {
        // RecursiveShuffle_M2(sample, m, block_size);
      });
      written += m;
    }
    else
    {
      const ControlReadResult result =
          FFOS_CControlRead(child.data_ptr(), C, FfosCChildren(node.count), nv, m,
                        block_size, S + written * block_size,
                        out_capacity_blocks - written, p, workspaces,
                        selected_scratch, depth + 1, profile);
      written += result.written_blocks;
      p = result.next_pos;
    }
  }
  return {written, p};
}

ControlReadResult FFOS_CControlRead(const unsigned char *D,
    const std::vector<uint8_t> &C,
    const std::vector<FrontierNode> &nodes,
    size_t n, size_t m, size_t block_size,
    unsigned char *S, size_t out_capacity_blocks,
    size_t p, enc_ret *profile)
{
  size_t bytes = 0;
  if (D == nullptr || S == nullptr || block_size == 0 ||
      MulOverflowSizeT(n, block_size, &bytes) ||
      MulOverflowSizeT(out_capacity_blocks, block_size, &bytes))
    throw std::invalid_argument("Invalid FFOS_C data/output buffer dimensions");
  std::vector<FfosCDataWorkspace> workspaces(FfosCNodeDepth(nodes));
  SelectedScratch selected_scratch;
  return FFOS_CControlRead(D, C, nodes, n, m, block_size, S, out_capacity_blocks,
                      p, workspaces, selected_scratch, 0, profile);
}

// ECALL adapter: encryption, PRB lifetime and timing wrap the serial algorithms.
extern "C" void DecFFOS_C(unsigned char *encrypted_buffer,
    size_t N,
    size_t M,
    size_t K,
    size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer,
    enc_ret *ret)
{
  using namespace ffos_c_detail;
  if (ret == nullptr) return;
  memory_profile::Scope memory(ret);
  if (encrypted_buffer == nullptr || N == 0) return;
  unsigned char *decrypted_buffer = NULL;
  const size_t decrypted_block_size =
      decryptBuffer(encrypted_buffer, static_cast<uint64_t>(N),
                    encrypted_block_size, &decrypted_buffer);

  ret->ptime = 0.0;
  ret->gen_perm_time = 0.0;
  ret->apply_perm_time = 0.0;
  ret->online_route_ms = ret->online_reorder_ms = 0.0;
  ret->online_copy_ms = ret->online_shuffle_ms = 0.0;
  offline_profile::Reset(ret);
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
  std::vector<size_t> M_matrix;
  online_profile::Track(ret->collect_offline_profile
                            ? &ret->offline_mark_ms : nullptr, [&] {
    M_matrix = FFOS_CMark(N, M, K);
  });

  std::vector<FrontierNode> F;
  size_t mk = 0;
  std::vector<FrontierNode> nodes;
  size_t control_bits = 0;
  online_profile::Track(ret->collect_offline_profile
                            ? &ret->offline_count_ms : nullptr, [&] {
    const bool frontier_overflow = MulOverflowSizeT(M, K, &mk);
    if (frontier_overflow || mk > N)
      F = FFOS_CFrontier(N, M, K);
    nodes = FfosCRootNodes(F, K);
    control_bits = FFOS_CControlCount(nodes, N, M);
  });
  std::vector<uint8_t> C;
  online_profile::Track(ret->collect_offline_profile
                            ? &ret->offline_swo_write_ms : nullptr, [&] {
    C = FFOS_CControl(M_matrix, F, N, M, K);
  });
  std::vector<size_t>().swap(M_matrix);
  ocall_clock(&t1);
  ret->gen_perm_time = static_cast<double>(t1 - t0) / 1000.0;
  if (ret->collect_offline_profile)
    ret->offline_heap_peak_bytes = offline_profile::HeapPeakBytes();

#ifdef COUNT_OSWAPS
  const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif

  std::vector<unsigned char> plain_result(result_bytes, 0);

  ocall_clock(&t0);
  const ControlReadResult result =
      FFOS_CControlRead(decrypted_buffer, C, nodes, N, M, decrypted_block_size,
                     plain_result.data(), total_blocks, 0,
                     ret->collect_online_profile ? ret : nullptr);
  ocall_clock(&t1);
  ret->apply_perm_time = static_cast<double>(t1 - t0) / 1000.0;
  if (ret->collect_offline_profile)
    ret->total_heap_peak_bytes = offline_profile::HeapPeakBytes();
  ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif

  if (result.next_pos == control_bits && result.written_blocks == total_blocks)
  {
    if (encryptBuffer(plain_result.data(), static_cast<uint64_t>(total_blocks),
                      decrypted_block_size, encrypt_result_buffer) != SIZE_MAX)
      memory.Complete();
  }

  PRB_pool_shutdown();
  free(decrypted_buffer);
}
