#include "OFRSupple.hpp"

#include "../OFR/OFR.hpp"
#include "../SWO/SuppleSWO.hpp"

#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include "../OnlineProfile.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{

using swo_detail::AddOverflowSizeT;
using swo_detail::MarkWords;
using swo_detail::MulOverflowSizeT;
using swo_detail::SwoNodeCapacity;

void CheckDimensions(size_t n, size_t m, size_t k)
{
  if (n == 0 || m == 0 || k == 0 || m > n)
    throw std::invalid_argument("Invalid OFRSupple dimensions");
}

void CheckFrontier(const std::vector<FrontierNode> &frontier,
                   size_t n, size_t m, size_t k)
{
  if (frontier.empty())
    return;
  size_t next = 0;
  for (const FrontierNode &node : frontier)
  {
    size_t capacity = 0;
    if (node.count == 0 || node.start != next ||
        MulOverflowSizeT(m, node.count, &capacity) || capacity > n ||
        AddOverflowSizeT(next, node.count, &next))
      throw std::invalid_argument("Invalid OFRSupple frontier");
  }
  if (next != k)
    throw std::invalid_argument("Invalid OFRSupple frontier coverage");
}

bool ForkNode(size_t n, size_t m, size_t k)
{
  size_t capacity = 0;
  return !MulOverflowSizeT(m, k, &capacity) && capacity == n;
}

std::vector<FrontierNode> ChildrenOrFrontier(
    const std::vector<FrontierNode> &frontier, size_t k)
{
  return frontier.empty() ? swo_detail::SwoChildren(k) : frontier;
}

uint8_t HasMembership(const size_t *row, size_t words,
                      size_t start, size_t count)
{
  size_t any = 0;
  const size_t word_bits = swo_detail::SwoWordBits();
  for (size_t offset = 0; offset < count;)
  {
    const size_t take = std::min(word_bits, count - offset);
    any |= swo_detail::ProjectMarkSliceOneWord(
        row, words, start + offset, take);
    offset += take;
  }
  return static_cast<uint8_t>(any != 0);
}

void CheckMembershipShape(const std::vector<size_t> &membership,
                          size_t n, size_t k)
{
  size_t required = 0;
  if (MulOverflowSizeT(n, MarkWords(k), &required) ||
      membership.size() != required)
    throw std::invalid_argument("OFRSupple membership dimensions mismatch");
}

OFRSuppleControlCounts CountNode(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k)
{
  if (k == 1)
    return {n == m ? 0 : n, 0};

  const size_t left_k = k / 2;
  const size_t right_k = k - left_k;
  OFRSuppleControlCounts result = {0, 0};
  if (ForkNode(n, m, k))
  {
    const size_t left_n = m * left_k;
    const size_t right_n = n - left_n;
    result.ofr_words = ofr::OFRControlCount(n, left_n, right_n);
    const OFRSuppleControlCounts left = CountNode({}, left_n, m, left_k);
    const OFRSuppleControlCounts right = CountNode({}, right_n, m, right_k);
    if (AddOverflowSizeT(result.swo_bits, left.swo_bits, &result.swo_bits) ||
        AddOverflowSizeT(result.swo_bits, right.swo_bits, &result.swo_bits) ||
        AddOverflowSizeT(result.ofr_words, left.ofr_words, &result.ofr_words) ||
        AddOverflowSizeT(result.ofr_words, right.ofr_words, &result.ofr_words))
      throw std::length_error("OFRSupple control count overflow");
    return result;
  }

  for (const FrontierNode &node : ChildrenOrFrontier(frontier, k))
  {
    const size_t child_n = SwoNodeCapacity(n, m, node.count);
    const OFRSuppleControlCounts child = CountNode({}, child_n, m, node.count);
    if ((child_n < n && AddOverflowSizeT(result.swo_bits, n,
                                         &result.swo_bits)) ||
        AddOverflowSizeT(result.swo_bits, child.swo_bits, &result.swo_bits) ||
        AddOverflowSizeT(result.ofr_words, child.ofr_words, &result.ofr_words))
      throw std::length_error("OFRSupple control count overflow");
  }
  return result;
}

OFRSuppleControlPositions WriteNode(
    const size_t *membership, size_t words, OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position)
{
  if (k == 1 && n == m)
    return position;

  if (k > 1 && ForkNode(n, m, k))
  {
    const size_t left_k = k / 2;
    const size_t right_k = k - left_k;
    const size_t left_n = m * left_k;
    const size_t right_n = n - left_n;
    std::vector<uint8_t> tags(n);
    for (size_t i = 0; i < n; ++i)
    {
      const size_t *row = membership + i * words;
      tags[i] = static_cast<uint8_t>(
          (HasMembership(row, words, 0, left_k) << 1U) |
          HasMembership(row, words, left_k, right_k));
    }
    std::vector<uint8_t> normalized =
        ofr::OFRNormalize(tags, n, left_n, right_n);
    const size_t start = position.ofr_words;
    position.ofr_words = ofr::OFRControlWrite(
        normalized, controls.ofr, n, left_n, right_n, start);

    size_t width = 0;
    if (MulOverflowSizeT(words, sizeof(size_t), &width))
      throw std::length_error("OFRSupple membership width overflow");
    const ofr::OFRControlReadResult routed = ofr::OFRControlRead(
        reinterpret_cast<const unsigned char *>(membership), controls.ofr,
        n, left_n, right_n, width, start);
    if (routed.next_pos != position.ofr_words)
      throw std::logic_error("OFRSupple OFR replay offset mismatch");

    std::vector<size_t> left_rows(left_n * words);
    std::vector<size_t> right_rows(right_n * words);
    std::memcpy(left_rows.data(), routed.left.data(), routed.left.size());
    std::memcpy(right_rows.data(), routed.right.data(), routed.right.size());
    swo_detail::SwoMarkWorkspace left;
    swo_detail::SwoMarkWorkspace right;
    swo_detail::ProjectMarkToWorkspace(
        left_rows.data(), left_n, words, 0, left_k, left);
    swo_detail::ProjectMarkToWorkspace(
        right_rows.data(), right_n, words, left_k, right_k, right);
    position = WriteNode(left.mark_ptr(), MarkWords(left_k), controls, {},
                         left_n, m, left_k, position);
    return WriteNode(right.mark_ptr(), MarkWords(right_k), controls, {},
                     right_n, m, right_k, position);
  }

  const std::vector<FrontierNode> nodes =
      k == 1 ? std::vector<FrontierNode>{{0, 1}}
             : ChildrenOrFrontier(frontier, k);
  for (const FrontierNode &node : nodes)
  {
    const size_t child_n = SwoNodeCapacity(n, m, node.count);
    swo_detail::SwoMarkWorkspace child;
    if (child_n < n)
    {
      swo_detail::SwoCheckControlSpan(controls.swo, position.swo_bits, n);
      bool *selected = swo_detail::AcquireSelectedScratch(n);
      if (selected == nullptr)
        throw std::bad_alloc();
      swo_detail::CompactMarkToWorkspaceAndControl(
          membership, n, words, selected, child_n, node.start, node.count,
          controls.swo, position.swo_bits, child);
      position.swo_bits += n;
    }
    else
    {
      swo_detail::ProjectMarkToWorkspace(
          membership, n, words, node.start, node.count, child);
    }
    if (node.count > 1)
      position = WriteNode(child.mark_ptr(), MarkWords(node.count), controls,
                           {}, child_n, m, node.count, position);
  }
  return position;
}

void PrepareNode(OFRSuppleControls &controls,
                 const std::vector<FrontierNode> &frontier,
                 size_t n, size_t m, size_t k, size_t &offset)
{
  if (k == 1)
    return;
  if (ForkNode(n, m, k))
  {
    const size_t left_k = k / 2;
    const size_t left_n = m * left_k;
    const size_t right_n = n - left_n;
    const size_t count = ofr::OFRControlCount(n, left_n, right_n);
    if (offset > controls.ofr.size() || count > controls.ofr.size() - offset)
      throw std::length_error("OFRSupple preparation exceeds control tape");

    size_t shape_index = 0;
    for (; shape_index < controls.shapes.size(); ++shape_index)
    {
      const OFRSuppleControls::Shape &shape = controls.shapes[shape_index];
      if (shape.n == n && shape.n_left == left_n &&
          shape.n_right == right_n)
        break;
    }
    if (shape_index == controls.shapes.size())
      controls.shapes.push_back({n, left_n, right_n});

    const bool postordered = n >= 2 && (n & (n - 1)) == 0 &&
                             left_n == n / 2 && right_n == n / 2;
    std::vector<uint8_t> tape(controls.ofr.begin() + offset,
                              controls.ofr.begin() + offset + count);
    if (postordered)
      tape = ofr::OFRPostOrderControls(tape, n, left_n, right_n);
    controls.nodes.push_back({offset, shape_index, postordered,
                              std::move(tape)});
    offset += count;
    PrepareNode(controls, {}, left_n, m, left_k, offset);
    PrepareNode(controls, {}, right_n, m, k - left_k, offset);
    return;
  }

  for (const FrontierNode &node : ChildrenOrFrontier(frontier, k))
    if (node.count > 1)
      PrepareNode(controls, {}, SwoNodeCapacity(n, m, node.count),
                  m, node.count, offset);
}

OFRSuppleReadResult ReadNode(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, unsigned char *samples, size_t out_capacity_blocks,
    OFRSuppleControlPositions position,
    std::vector<swo_detail::SwoDataWorkspace> &workspaces, size_t depth,
    size_t &node_index, enc_ret *profile)
{
  double *copy_ms = profile ? &profile->online_copy_ms : nullptr;
  double *route_ms = profile ? &profile->online_route_ms : nullptr;
  double *shuffle_ms = profile ? &profile->online_shuffle_ms : nullptr;
  if (k == 1 && n == m)
  {
    if (out_capacity_blocks < m)
      throw std::length_error("OFRSupple leaf output exceeds buffer");
    online_profile::Track(copy_ms, [&] {
      std::memcpy(samples, data, m * block_size);
    });
    online_profile::Track(shuffle_ms, [&] {
      RecursiveShuffle_M2(samples, m, block_size);
    });
    return {m, position};
  }

  if (k > 1 && ForkNode(n, m, k))
  {
    const size_t left_k = k / 2;
    const size_t left_n = m * left_k;
    const size_t right_k = k - left_k;
    const size_t right_n = n - left_n;
    if (controls.nodes.empty())
    {
      const ofr::OFRControlReadResult routed = ofr::OFRControlRead(
          data, controls.ofr, n, left_n, right_n, block_size,
          position.ofr_words);
      position.ofr_words = routed.next_pos;
      const OFRSuppleReadResult left = ReadNode(
          routed.left.data(), controls, {}, left_n, m, left_k, block_size,
          samples, out_capacity_blocks, position, workspaces, depth + 1,
          node_index, profile);
      const OFRSuppleReadResult right = ReadNode(
          routed.right.data(), controls, {}, right_n, m, right_k, block_size,
          samples + left.written_blocks * block_size,
          out_capacity_blocks - left.written_blocks, left.next,
          workspaces, depth + 1, node_index, profile);
      return {left.written_blocks + right.written_blocks, right.next};
    }

    if (node_index >= controls.nodes.size())
      throw std::length_error("OFRSupple prepared node is missing");
    const OFRSuppleControls::Node &plan = controls.nodes[node_index++];
    if (plan.offset != position.ofr_words ||
        plan.shape_index >= controls.shapes.size())
      throw std::logic_error("OFRSupple prepared node offset mismatch");
    const OFRSuppleControls::Shape &shape = controls.shapes[plan.shape_index];
    if (shape.n != n || shape.n_left != left_n || shape.n_right != right_n)
      throw std::logic_error("OFRSupple prepared node shape mismatch");
    swo_detail::SwoDataWorkspace &work = workspaces[depth];
    online_profile::Track(copy_ms, [&] {
      work.ensure_capacity(n, block_size);
      std::memcpy(work.data_ptr(), data, n * block_size);
    });
    online_profile::Track(route_ms, [&] {
      if (plan.postordered)
        ofr::OFRApplyPreparedPostOrderInPlace(
            work.data_ptr(), plan.tape, n, left_n, right_n, block_size);
      else
        ofr::OFRApplyPreparedInPlace(
            work.data_ptr(), plan.tape, n, left_n, right_n, block_size);
    });
    position.ofr_words += plan.tape.size();
    const OFRSuppleReadResult left = ReadNode(
        work.data_ptr(), controls, {}, left_n, m, left_k, block_size,
        samples, out_capacity_blocks, position, workspaces, depth + 1,
        node_index, profile);
    const OFRSuppleReadResult right = ReadNode(
        work.data_ptr() + left_n * block_size, controls, {}, right_n, m,
        right_k, block_size, samples + left.written_blocks * block_size,
        out_capacity_blocks - left.written_blocks, left.next,
        workspaces, depth + 1, node_index, profile);
    return {left.written_blocks + right.written_blocks, right.next};
  }

  const std::vector<FrontierNode> nodes =
      k == 1 ? std::vector<FrontierNode>{{0, 1}}
             : ChildrenOrFrontier(frontier, k);
  size_t written = 0;
  for (const FrontierNode &node : nodes)
  {
    const size_t child_n = SwoNodeCapacity(n, m, node.count);
    swo_detail::SwoDataWorkspace &child = workspaces[depth];
    online_profile::Track(copy_ms, [&] {
      child.ensure_capacity(n, block_size);
      std::memcpy(child.data_ptr(), data, n * block_size);
    });
    if (child_n < n)
    {
      swo_detail::SwoCheckControlSpan(controls.swo, position.swo_bits, n);
      bool *selected = swo_detail::AcquireSelectedScratch(n);
      if (selected == nullptr)
        throw std::bad_alloc();
      online_profile::Track(route_ms, [&] {
        swo_detail::UnpackControlBitsToBoolArray(
            controls.swo, position.swo_bits, n, selected);
        TightCompact_v2(child.data_ptr(), n, block_size, selected);
      });
      position.swo_bits += n;
    }
    if (node.count == 1)
    {
      if (child_n != m || m > out_capacity_blocks - written)
        throw std::length_error("OFRSupple leaf output exceeds buffer");
      unsigned char *sample = samples + written * block_size;
      online_profile::Track(copy_ms, [&] {
        std::memcpy(sample, child.data_ptr(), m * block_size);
      });
      online_profile::Track(shuffle_ms, [&] {
        RecursiveShuffle_M2(sample, m, block_size);
      });
      written += m;
    }
    else
    {
      const OFRSuppleReadResult result = ReadNode(
          child.data_ptr(), controls, {}, child_n, m, node.count, block_size,
          samples + written * block_size, out_capacity_blocks - written,
          position, workspaces, depth + 1, node_index, profile);
      written += result.written_blocks;
      position = result.next;
    }
  }
  return {written, position};
}

} // namespace

OFRSuppleControlCounts OFRSuppleControlCount(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  return CountNode(frontier, n, m, k);
}

OFRSuppleControlPositions OFRSuppleControlWrite(
    const std::vector<size_t> &membership, OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  CheckMembershipShape(membership, n, k);
  return WriteNode(membership.data(), MarkWords(k), controls, frontier,
                   n, m, k, position);
}

OFRSuppleControls OFRSuppleControl(
    const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k)
{
  const OFRSuppleControlCounts counts =
      OFRSuppleControlCount(frontier, n, m, k);
  OFRSuppleControls controls;
  controls.swo.resize(counts.swo_bits / 8 + (counts.swo_bits % 8 != 0), 0);
  controls.ofr.resize(counts.ofr_words, 0);
  const OFRSuppleControlPositions end = OFRSuppleControlWrite(
      membership, controls, frontier, n, m, k, {0, 0});
  if (end.swo_bits != counts.swo_bits || end.ofr_words != counts.ofr_words)
    throw std::logic_error("OFRSupple control write/count mismatch");
  size_t prepared_end = 0;
  PrepareNode(controls, frontier, n, m, k, prepared_end);
  if (prepared_end != counts.ofr_words)
    throw std::logic_error("OFRSupple preparation/count mismatch");
  return controls;
}

OFRSuppleReadResult OFRSuppleControlRead(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, unsigned char *samples, size_t out_capacity_blocks,
    OFRSuppleControlPositions position, enc_ret *profile)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  size_t data_bytes = 0, output_bytes = 0;
  if (data == nullptr || samples == nullptr || block_size == 0 ||
      MulOverflowSizeT(n, block_size, &data_bytes) ||
      MulOverflowSizeT(out_capacity_blocks, block_size, &output_bytes))
    throw std::invalid_argument("Invalid OFRSupple data dimensions");
  (void)data_bytes;
  (void)output_bytes;
  std::vector<swo_detail::SwoDataWorkspace> workspaces(
      swo_detail::WorkspaceDepth(k) + 1);
  size_t node_index = 0;
  const OFRSuppleReadResult read = ReadNode(
      data, controls, frontier, n, m, k, block_size,
      samples, out_capacity_blocks, position, workspaces, 0, node_index,
      profile);
  if (!controls.nodes.empty() && node_index != controls.nodes.size())
    throw std::logic_error("OFRSupple prepared node count mismatch");
  return read;
}

std::vector<unsigned char> OFRSuppleApply(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size)
{
  const OFRSuppleControlCounts counts =
      OFRSuppleControlCount(frontier, n, m, k);
  if (controls.swo.size() !=
          counts.swo_bits / 8 + (counts.swo_bits % 8 != 0) ||
      controls.ofr.size() != counts.ofr_words)
    throw std::length_error("OFRSupple control array length mismatch");
  size_t blocks = 0, bytes = 0;
  if (MulOverflowSizeT(m, k, &blocks) ||
      MulOverflowSizeT(blocks, block_size, &bytes))
    throw std::length_error("OFRSupple output size overflow");
  std::vector<unsigned char> result(bytes);
  const OFRSuppleReadResult read = OFRSuppleControlRead(
      data, controls, frontier, n, m, k, block_size, result.data(), blocks,
      {0, 0});
  if (read.written_blocks != blocks ||
      read.next.swo_bits != counts.swo_bits ||
      read.next.ofr_words != counts.ofr_words)
    throw std::logic_error("OFRSupple control consumption/output mismatch");
  return result;
}

std::vector<unsigned char> OFRSupple(
    const unsigned char *data, size_t n, size_t m, size_t k, size_t block_size)
{
  if (data == nullptr || n == 0 || m == 0 || k == 0 || block_size == 0)
    return {};
  m = std::min(m, n);
  size_t capacity = 0;
  const std::vector<FrontierNode> frontier =
      MulOverflowSizeT(m, k, &capacity) || capacity > n
          ? SWOFrontier(n, m, k) : std::vector<FrontierNode>{};
  const std::vector<size_t> membership = SWOMark(n, m, k);
  const OFRSuppleControls controls =
      OFRSuppleControl(membership, frontier, n, m, k);
  return OFRSuppleApply(data, controls, frontier, n, m, k, block_size);
}

extern "C" void DecOFRSupple(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret)
{
  unsigned char *decrypted = nullptr;
  const size_t width = decryptBuffer(encrypted_buffer,
      static_cast<uint64_t>(N), encrypted_block_size, &decrypted);
  if (ret == nullptr)
  {
    free(decrypted);
    return;
  }
  ret->ptime = ret->gen_perm_time = ret->apply_perm_time = 0.0;
  ret->online_route_ms = ret->online_reorder_ms = 0.0;
  ret->online_copy_ms = ret->online_shuffle_ms = 0.0;
#ifdef COUNT_OSWAPS
  ret->OSWAP_count = 0;
#endif
  if (decrypted == nullptr || width == static_cast<size_t>(-1) ||
      M == 0 || K == 0 || encrypt_result_buffer == nullptr)
  {
    free(decrypted);
    return;
  }
  M = std::min(M, N);
  size_t blocks = 0, bytes = 0;
  if (MulOverflowSizeT(M, K, &blocks) ||
      MulOverflowSizeT(blocks, width, &bytes))
  {
    free(decrypted);
    return;
  }
  PRB_pool_init(1);
  try
  {
    long start = 0, end = 0;
    ocall_clock(&start);
    const std::vector<size_t> membership = SWOMark(N, M, K);
    size_t capacity = 0;
    const std::vector<FrontierNode> frontier =
        MulOverflowSizeT(M, K, &capacity) || capacity > N
            ? SWOFrontier(N, M, K) : std::vector<FrontierNode>{};
    const OFRSuppleControls controls =
        OFRSuppleControl(membership, frontier, N, M, K);
    const OFRSuppleControlCounts counts =
        OFRSuppleControlCount(frontier, N, M, K);
    ocall_clock(&end);
    ret->gen_perm_time = static_cast<double>(end - start) / 1000.0;
#ifdef COUNT_OSWAPS
    const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif
    std::vector<unsigned char> plain(bytes);
    ocall_clock(&start);
    const OFRSuppleReadResult read = OFRSuppleControlRead(
        decrypted, controls, frontier, N, M, K, width,
        plain.data(), blocks, {0, 0},
        ret->collect_online_profile ? ret : nullptr);
    ocall_clock(&end);
    if (read.written_blocks != blocks ||
        read.next.swo_bits != counts.swo_bits ||
        read.next.ofr_words != counts.ofr_words)
      throw std::logic_error("OFRSupple control consumption/output mismatch");
    ret->apply_perm_time = static_cast<double>(end - start) / 1000.0;
    ret->ptime = ret->gen_perm_time + ret->apply_perm_time;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif
    encryptBuffer(const_cast<unsigned char *>(plain.data()),
                  static_cast<uint64_t>(blocks), width,
                  encrypt_result_buffer);
  }
  catch (const std::exception &)
  {
    ret->ptime = ret->gen_perm_time = ret->apply_perm_time = 0.0;
    ret->online_route_ms = ret->online_reorder_ms = 0.0;
    ret->online_copy_ms = ret->online_shuffle_ms = 0.0;
  }
  PRB_pool_shutdown();
  free(decrypted);
}
