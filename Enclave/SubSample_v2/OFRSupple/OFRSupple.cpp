#include "OFRSupple.hpp"
#include "../../MemoryProfile.hpp"

#include "../OFR/OFR.hpp"
#include "../SWO/SuppleSWO.hpp"

#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include "../OnlineProfile.hpp"
#include "../OfflineProfile.hpp"

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

#if defined(__SSE2__) && defined(__x86_64__) && !defined(__ILP32__)
// The enclave uses -nostdinc, so express SSE2 through GNU vectors and compiler
// builtins rather than depending on host intrinsic headers.
typedef int TagVec32 __attribute__((vector_size(16)));
typedef short TagVec16 __attribute__((vector_size(16)));
typedef char TagVec8 __attribute__((vector_size(16)));
typedef long long TagVec64 __attribute__((vector_size(16)));
typedef unsigned long long TagUVec64 __attribute__((vector_size(16)));

inline TagUVec64 OneWordTagPair(const size_t *rows,
                               TagUVec64 left_mask, TagUVec64 right_mask,
                               TagUVec64 &left_count, TagUVec64 &right_count)
{
  TagUVec64 data;
  std::memcpy(&data, rows, sizeof(data));
  const TagVec32 zero = {0, 0, 0, 0};
  const TagUVec64 one = {1, 1};
  const TagVec32 left_zero = (TagVec32)(data & left_mask) == zero;
  const TagVec32 right_zero = (TagVec32)(data & right_mask) == zero;
  // SSE2 compares 32-bit lanes. A whole 64-bit lane is zero exactly when
  // both adjacent comparison lanes are true; this uses no secret branches.
  const TagUVec64 left = (TagUVec64)~(
      left_zero & __builtin_ia32_pshufd(left_zero, 0xb1)) & one;
  const TagUVec64 right = (TagUVec64)~(
      right_zero & __builtin_ia32_pshufd(right_zero, 0xb1)) & one;
  left_count += left;
  right_count += right;
  return (left << 1) | right;
}
#endif

void OneWordTags(const size_t *membership, uint8_t *tags, size_t n,
                 size_t left_k, size_t right_k,
                 size_t &left_weight, size_t &right_weight)
{
  // Fork nodes have k > 1. Both shifts are below the word width even for
  // k == SwoWordBits(), and masks depend only on the public node shape.
  const size_t left_mask = (size_t(1) << left_k) - 1;
  const size_t right_mask = ((size_t(1) << right_k) - 1) << left_k;
  size_t i = 0;
#if defined(__SSE2__) && defined(__x86_64__) && !defined(__ILP32__)
  const TagUVec64 left_masks = {left_mask, left_mask};
  const TagUVec64 right_masks = {right_mask, right_mask};
  TagUVec64 left_count = {0, 0};
  TagUVec64 right_count = {0, 0};
  for (; n - i >= 8; i += 8)
  {
    const TagUVec64 first = OneWordTagPair(membership + i, left_masks, right_masks,
                                        left_count, right_count);
    const TagUVec64 second = OneWordTagPair(membership + i + 2, left_masks, right_masks,
                                         left_count, right_count);
    const TagUVec64 third = OneWordTagPair(membership + i + 4, left_masks, right_masks,
                                        left_count, right_count);
    const TagUVec64 fourth = OneWordTagPair(membership + i + 6, left_masks, right_masks,
                                         left_count, right_count);
    const TagVec32 low = (TagVec32)__builtin_ia32_punpcklqdq128(
        (TagVec64)__builtin_ia32_pshufd((TagVec32)first, 0x88),
        (TagVec64)__builtin_ia32_pshufd((TagVec32)second, 0x88));
    const TagVec32 high = (TagVec32)__builtin_ia32_punpcklqdq128(
        (TagVec64)__builtin_ia32_pshufd((TagVec32)third, 0x88),
        (TagVec64)__builtin_ia32_pshufd((TagVec32)fourth, 0x88));
    const TagVec16 zero16 = {0, 0, 0, 0, 0, 0, 0, 0};
    const TagVec16 tag_words = __builtin_ia32_packssdw128(low, high);
    const TagVec8 tag_bytes = __builtin_ia32_packuswb128(tag_words, zero16);
    std::memcpy(tags + i, &tag_bytes, 8);
  }
  left_weight += static_cast<size_t>(left_count[0]) + static_cast<size_t>(left_count[1]);
  right_weight += static_cast<size_t>(right_count[0]) + static_cast<size_t>(right_count[1]);
#endif
  for (; i < n; ++i)
  {
    const size_t row = membership[i];
    const uint8_t has_left = static_cast<uint8_t>((row & left_mask) != 0);
    const uint8_t has_right = static_cast<uint8_t>((row & right_mask) != 0);
    tags[i] = static_cast<uint8_t>((has_left << 1U) | has_right);
    left_weight += has_left;
    right_weight += has_right;
  }
}

void CheckMembershipShape(const std::vector<size_t> &membership,
                          size_t n, size_t k)
{
  size_t required = 0;
  if (MulOverflowSizeT(n, MarkWords(k), &required) ||
      membership.size() != required)
    throw std::invalid_argument("OFRSupple membership dimensions mismatch");
}

struct ShapeControlCount
{
  size_t n, left_n, right_n, count;
};

struct ControlWriteWorkspace
{
  std::vector<uint8_t> tags;
  std::vector<uint8_t> features;
  swo_detail::SwoMarkWorkspace routed;
  swo_detail::SwoMarkWorkspace left;
  swo_detail::SwoMarkWorkspace right;
  swo_detail::SwoMarkWorkspace child;
};

struct ControlWriteContext
{
  explicit ControlWriteContext(size_t k)
      : workspaces(swo_detail::WorkspaceDepth(k) + 1) {}
  std::vector<ControlWriteWorkspace> workspaces;
  std::vector<ShapeControlCount> shapes;
};

size_t ShapeCount(std::vector<ShapeControlCount> &shapes,
                  size_t n, size_t left_n, size_t right_n)
{
  for (const ShapeControlCount &shape : shapes)
    if (shape.n == n && shape.left_n == left_n && shape.right_n == right_n)
      return shape.count;
  const size_t count = ofr::OFRControlCount(n, left_n, right_n);
  shapes.push_back({n, left_n, right_n, count});
  return count;
}

void ReserveMarks(swo_detail::SwoMarkWorkspace &workspace,
                  size_t items, size_t words)
{
  size_t required = 0;
  if (MulOverflowSizeT(items, words, &required))
    throw std::length_error("OFRSupple membership size overflow");
  // Reserve exactly the public maximum so sibling growth does not double
  // capacity beyond the workspace estimate.
  workspace.mark_buf.reserve(required);
}

OFRSuppleControlCounts CountNode(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    std::vector<ShapeControlCount> &shapes)
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
    result.ofr_words = ShapeCount(shapes, n, left_n, right_n);
    const OFRSuppleControlCounts left = CountNode({}, left_n, m, left_k, shapes);
    const OFRSuppleControlCounts right = CountNode({}, right_n, m, right_k, shapes);
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
    const OFRSuppleControlCounts child = CountNode({}, child_n, m, node.count,
                                                 shapes);
    if ((child_n < n && AddOverflowSizeT(result.swo_bits, n,
                                         &result.swo_bits)) ||
        AddOverflowSizeT(result.swo_bits, child.swo_bits, &result.swo_bits) ||
        AddOverflowSizeT(result.ofr_words, child.ofr_words, &result.ofr_words))
      throw std::length_error("OFRSupple control count overflow");
  }
  return result;
}

bool PostOrderedNode(size_t n, size_t left_n, size_t right_n)
{
  return n >= 2 && (n & (n - 1)) == 0 &&
         left_n == n / 2 && right_n == n / 2;
}

void RecordPreparedNode(OFRSuppleControls &controls, size_t offset,
                        size_t count, size_t n, size_t left_n,
                        size_t right_n, bool postordered)
{
  size_t shape_index = 0;
  for (; shape_index < controls.shapes.size(); ++shape_index)
  {
    const OFRSuppleControls::Shape &shape = controls.shapes[shape_index];
    if (shape.n == n && shape.n_left == left_n && shape.n_right == right_n)
      break;
  }
  if (shape_index == controls.shapes.size())
    controls.shapes.push_back({n, left_n, right_n});
  controls.nodes.push_back({offset, shape_index, postordered, count});
}

OFRSuppleControlPositions WriteNode(
    const size_t *membership, size_t *owned_membership, size_t words,
    OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position, enc_ret *profile, bool prepared,
    ControlWriteContext &context, size_t depth)
{
  double *tags_ms = profile ? &profile->offline_tags_ms : nullptr;
  double *normalize_ms = profile ? &profile->offline_normalize_ms : nullptr;
  double *ofr_write_ms = profile ? &profile->offline_ofr_write_ms : nullptr;
  double *replay_ms = profile ? &profile->offline_replay_ms : nullptr;
  double *project_ms = profile ? &profile->offline_project_ms : nullptr;
  double *swo_write_ms = profile ? &profile->offline_swo_write_ms : nullptr;
  if (k == 1 && n == m)
    return position;
  ControlWriteWorkspace &workspace = context.workspaces[depth];

  if (k > 1 && ForkNode(n, m, k))
  {
    const size_t left_k = k / 2;
    const size_t right_k = k - left_k;
    const size_t left_n = m * left_k;
    const size_t right_n = n - left_n;
    const bool postordered = prepared && PostOrderedNode(n, left_n, right_n);
    swo_detail::SwoMarkWorkspace &left = workspace.left;
    swo_detail::SwoMarkWorkspace &right = workspace.right;
    {
      const size_t start = position.ofr_words;
      const size_t control_count = ShapeCount(context.shapes, n, left_n, right_n);
      size_t *routed = owned_membership;
      if (routed == nullptr)
        online_profile::Track(replay_ms, [&] {
          // Only the externally owned root needs a copy. Projected children
          // belong to this writer and can be routed in place.
          ReserveMarks(workspace.routed, n, words);
          workspace.routed.ensure_capacity(n, words);
          routed = workspace.routed.mark_ptr();
          std::copy(membership, membership + n * words, routed);
        });
      {
        std::vector<uint8_t> &tags = workspace.tags;
        size_t left_weight = 0, right_weight = 0;
        online_profile::Track(tags_ms, [&] {
          tags.reserve(n);
          tags.resize(n);
          if (words == 1 && k <= swo_detail::SwoWordBits())
            OneWordTags(membership, tags.data(), n, left_k, right_k,
                        left_weight, right_weight);
          else
            for (size_t i = 0; i < n; ++i)
            {
              const size_t *row = membership + i * words;
              const uint8_t has_left = HasMembership(row, words, 0, left_k);
              const uint8_t has_right = HasMembership(row, words, left_k, right_k);
              tags[i] = static_cast<uint8_t>((has_left << 1U) | has_right);
              left_weight += has_left;
              right_weight += has_right;
            }
        });
        online_profile::Track(normalize_ms, [&] {
          ofr::detail::OFRNormalizeOwnedInPlace(
              tags, n, left_n, right_n, left_weight, right_weight);
        });
        online_profile::Track(ofr_write_ms, [&] {
          if (postordered)
            position.ofr_words = ofr::detail::OFRControlWritePostOrderAndRouteNormalized(
                tags, controls.ofr, routed, words,
                n, left_n, right_n, start, control_count);
          else
          {
            workspace.features.reserve(n / 2);
            position.ofr_words = ofr::detail::OFRControlWriteAndRouteNormalized(
                tags, controls.ofr, routed, words,
                n, left_n, right_n, start, control_count, &workspace.features);
          }
        });
      }
      if (position.ofr_words - start != control_count)
        throw std::logic_error("OFRSupple OFR replay offset mismatch");
      if (prepared)
        online_profile::Track(profile ? &profile->offline_prepare_ms : nullptr,
                              [&] {
          RecordPreparedNode(controls, start, position.ofr_words - start,
                             n, left_n, right_n, postordered);
        });

      online_profile::Track(project_ms, [&] {
        // A fork leaf has n == m, so WriteNode returns before inspecting its
        // membership. Only internal children need an offline projection.
        if (left_k > 1)
        {
          ReserveMarks(left, left_n, MarkWords(left_k));
          if (words == 1)
          {
            left.ensure_capacity(left_n, 1);
            const size_t mask = (size_t(1) << left_k) - 1;
            size_t *destination = left.mark_ptr();
            for (size_t i = 0; i < left_n; ++i)
              destination[i] = routed[i] & mask;
          }
          else
            swo_detail::ProjectMarkToWorkspace(
                routed, left_n, words, 0, left_k, left);
        }
        if (right_k > 1)
        {
          ReserveMarks(right, right_n, MarkWords(right_k));
          if (words == 1)
          {
            right.ensure_capacity(right_n, 1);
            const size_t mask = (size_t(1) << right_k) - 1;
            size_t *destination = right.mark_ptr();
            for (size_t i = 0; i < right_n; ++i)
              destination[i] = (routed[left_n + i] >> left_k) & mask;
          }
          else
            swo_detail::ProjectMarkToWorkspace(
                routed + left_n * words, right_n, words,
                left_k, right_k, right);
        }
      });
    }
    position = WriteNode(left.mark_ptr(), left.mark_ptr(), MarkWords(left_k),
                         controls, {}, left_n, m, left_k, position, profile,
                         prepared, context, depth + 1);
    return WriteNode(right.mark_ptr(), right.mark_ptr(), MarkWords(right_k),
                     controls, {}, right_n, m, right_k, position, profile,
                     prepared, context, depth + 1);
  }

  const std::vector<FrontierNode> nodes =
      k == 1 ? std::vector<FrontierNode>{{0, 1}}
             : ChildrenOrFrontier(frontier, k);
  for (const FrontierNode &node : nodes)
  {
    const size_t child_n = SwoNodeCapacity(n, m, node.count);
    swo_detail::SwoMarkWorkspace &child = workspace.child;
    if (child_n < n)
      online_profile::Track(swo_write_ms, [&] {
        swo_detail::SwoCheckControlSpan(controls.swo, position.swo_bits, n);
        bool *selected = swo_detail::AcquireSelectedScratch(n);
        if (selected == nullptr)
          throw std::bad_alloc();
        ReserveMarks(child, n, MarkWords(node.count));
        swo_detail::CompactMarkToWorkspaceAndControl(
            membership, n, words, selected, child_n, node.start, node.count,
            controls.swo, position.swo_bits, child);
        position.swo_bits += n;
      });
    else
      online_profile::Track(project_ms, [&] {
        ReserveMarks(child, n, MarkWords(node.count));
        swo_detail::ProjectMarkToWorkspace(
            membership, n, words, node.start, node.count, child);
      });
    if (node.count > 1)
      position = WriteNode(child.mark_ptr(), child.mark_ptr(),
                           MarkWords(node.count), controls, {}, child_n, m,
                           node.count, position, profile, prepared, context,
                           depth + 1);
  }
  return position;
}

OFRSuppleControlPositions ValidateReadPlanNode(
    const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position, size_t &node_index,
    std::vector<ShapeControlCount> &shape_counts)
{
  if (k == 1 && n == m)
    return position;
  if (k > 1 && ForkNode(n, m, k))
  {
    const size_t left_k = k / 2;
    const size_t right_k = k - left_k;
    const size_t left_n = m * left_k;
    const size_t right_n = n - left_n;
    const size_t count = ShapeCount(shape_counts, n, left_n, right_n);
    if (position.ofr_words > controls.ofr.size() ||
        count > controls.ofr.size() - position.ofr_words)
      throw std::length_error("OFRSupple prepared node exceeds control tape");
    if (!controls.nodes.empty())
    {
      if (node_index >= controls.nodes.size())
        throw std::length_error("OFRSupple prepared node is missing");
      const OFRSuppleControls::Node &plan = controls.nodes[node_index++];
      if (plan.offset != position.ofr_words ||
          plan.shape_index >= controls.shapes.size())
        throw std::logic_error("OFRSupple prepared node offset mismatch");
      if (plan.count != count)
        throw std::logic_error("OFRSupple prepared node length mismatch");
      const OFRSuppleControls::Shape &shape = controls.shapes[plan.shape_index];
      if (shape.n != n || shape.n_left != left_n || shape.n_right != right_n)
        throw std::logic_error("OFRSupple prepared node shape mismatch");
      if (plan.postordered && !PostOrderedNode(n, left_n, right_n))
        throw std::logic_error("OFRSupple postorder node is not balanced");
    }
    position.ofr_words += count;
    position = ValidateReadPlanNode(controls, {}, left_n, m, left_k,
                                    position, node_index, shape_counts);
    return ValidateReadPlanNode(controls, {}, right_n, m, right_k,
                                position, node_index, shape_counts);
  }

  const std::vector<FrontierNode> nodes =
      k == 1 ? std::vector<FrontierNode>{{0, 1}}
             : ChildrenOrFrontier(frontier, k);
  for (const FrontierNode &node : nodes)
  {
    const size_t child_n = SwoNodeCapacity(n, m, node.count);
    if (child_n < n)
    {
      swo_detail::SwoCheckControlSpan(controls.swo, position.swo_bits, n);
      position.swo_bits += n;
    }
    if (node.count > 1)
      position = ValidateReadPlanNode(controls, {}, child_n, m, node.count,
                                      position, node_index, shape_counts);
  }
  return position;
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
      // RecursiveShuffle_M2(samples, m, block_size);
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
    if (plan.offset > controls.ofr.size() ||
        plan.count > controls.ofr.size() - plan.offset)
      throw std::length_error("OFRSupple prepared node exceeds control tape");
    // The public read preflight has checked canonical counts and layouts for
    // the entire plan before any output writes; retain cheap local checks.
    const OFRSuppleControls::Shape &shape = controls.shapes[plan.shape_index];
    if (shape.n != n || shape.n_left != left_n || shape.n_right != right_n)
      throw std::logic_error("OFRSupple prepared node shape mismatch");
    swo_detail::SwoDataWorkspace &work = workspaces[depth];
    online_profile::Track(copy_ms, [&] {
      work.data_buf.reserve(n * block_size);
      work.ensure_capacity(n, block_size);
      std::memcpy(work.data_ptr(), data, n * block_size);
    });
    online_profile::Track(route_ms, [&] {
      const ofr::PackedControlView tape =
          controls.ofr.view(plan.offset, plan.count);
      if (plan.postordered)
        ofr::OFRApplyPreparedPostOrderInPlace(
            work.data_ptr(), tape, n, left_n, right_n, block_size);
      else
        ofr::OFRApplyPreparedInPlace(
            work.data_ptr(), tape, n, left_n, right_n, block_size);
    });
    position.ofr_words += plan.count;
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
      child.data_buf.reserve(n * block_size);
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
        // RecursiveShuffle_M2(sample, m, block_size);
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

OFRSuppleControls BuildControls(
    const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile, OFRSuppleControlCounts *counts_out,
    size_t *owned_root = nullptr)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  CheckMembershipShape(membership, n, k);
  ControlWriteContext context(k);
  OFRSuppleControlCounts counts;
  online_profile::Track(profile ? &profile->offline_count_ms : nullptr, [&] {
    counts = CountNode(frontier, n, m, k, context.shapes);
  });
  OFRSuppleControls controls;
  controls.swo.resize(counts.swo_bits / 8 + (counts.swo_bits % 8 != 0), 0);
  controls.ofr.resize(counts.ofr_words, 0);
  const OFRSuppleControlPositions end = WriteNode(
      membership.data(), owned_root, MarkWords(k), controls, frontier, n, m, k,
      {0, 0}, profile, true, context, 0);
  if (end.swo_bits != counts.swo_bits || end.ofr_words != counts.ofr_words)
    throw std::logic_error("OFRSupple control write/count mismatch");
  if (counts_out != nullptr)
    *counts_out = counts;
  return controls;
}

std::vector<unsigned char> ApplyKnownControls(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size, const OFRSuppleControlCounts &counts)
{
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

} // namespace

OFRSuppleControlCounts OFRSuppleControlCount(
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  std::vector<ShapeControlCount> shapes;
  return CountNode(frontier, n, m, k, shapes);
}

OFRSuppleControlPositions OFRSuppleControlWrite(
    const std::vector<size_t> &membership, OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    OFRSuppleControlPositions position, enc_ret *profile)
{
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  CheckMembershipShape(membership, n, k);
  ControlWriteContext context(k);
  return WriteNode(membership.data(), nullptr, MarkWords(k), controls, frontier,
                   n, m, k, position, profile, false, context, 0);
}

OFRSuppleControls OFRSuppleControl(
    const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile)
{
  return BuildControls(membership, frontier, n, m, k, profile, nullptr);
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
  size_t required_blocks = 0;
  if (MulOverflowSizeT(m, k, &required_blocks) ||
      out_capacity_blocks < required_blocks)
    throw std::length_error("OFRSupple output exceeds buffer");
  std::vector<ShapeControlCount> shape_counts;
  size_t planned_nodes = 0;
  const OFRSuppleControlPositions planned_end = ValidateReadPlanNode(
      controls, frontier, n, m, k, position, planned_nodes, shape_counts);
  if (!controls.nodes.empty() && planned_nodes != controls.nodes.size())
    throw std::logic_error("OFRSupple prepared node count mismatch");
  std::vector<swo_detail::SwoDataWorkspace> workspaces(
      swo_detail::WorkspaceDepth(k) + 1);
  size_t node_index = 0;
  const OFRSuppleReadResult read = ReadNode(
      data, controls, frontier, n, m, k, block_size,
      samples, out_capacity_blocks, position, workspaces, 0, node_index,
      profile);
  if (!controls.nodes.empty() && node_index != controls.nodes.size())
    throw std::logic_error("OFRSupple prepared node count mismatch");
  if (read.written_blocks != required_blocks ||
      read.next.swo_bits != planned_end.swo_bits ||
      read.next.ofr_words != planned_end.ofr_words)
    throw std::logic_error("OFRSupple read/preflight mismatch");
  return read;
}

std::vector<unsigned char> OFRSuppleApply(
    const unsigned char *data, const OFRSuppleControls &controls,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    size_t block_size)
{
  const OFRSuppleControlCounts counts =
      OFRSuppleControlCount(frontier, n, m, k);
  return ApplyKnownControls(data, controls, frontier, n, m, k, block_size,
                           counts);
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
  std::vector<size_t> membership = SWOMark(n, m, k);
  OFRSuppleControlCounts counts;
  const OFRSuppleControls controls =
      BuildControls(membership, frontier, n, m, k, nullptr, &counts,
                    membership.data());
  std::vector<size_t>().swap(membership);
  return ApplyKnownControls(data, controls, frontier, n, m, k, block_size,
                           counts);
}

extern "C" void DecOFRSupple(unsigned char *encrypted_buffer,
    size_t N, size_t M, size_t K, size_t encrypted_block_size,
    unsigned char *encrypt_result_buffer, enc_ret *ret)
{
  if (ret == nullptr) return;
  memory_profile::Scope memory(ret);
  if (encrypted_buffer == nullptr || N == 0) return;
  unsigned char *decrypted = nullptr;
  const size_t width = decryptBuffer(encrypted_buffer,
      static_cast<uint64_t>(N), encrypted_block_size, &decrypted);
  ret->ptime = ret->gen_perm_time = ret->apply_perm_time = 0.0;
  ret->online_route_ms = ret->online_reorder_ms = 0.0;
  ret->online_copy_ms = ret->online_shuffle_ms = 0.0;
  offline_profile::Reset(ret);
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
    std::vector<size_t> membership;
    online_profile::Track(ret->collect_offline_profile
                              ? &ret->offline_mark_ms : nullptr, [&] {
      membership = SWOMark(N, M, K);
    });
    size_t capacity = 0;
    std::vector<FrontierNode> frontier;
    online_profile::Track(ret->collect_offline_profile
                              ? &ret->offline_count_ms : nullptr, [&] {
      frontier = MulOverflowSizeT(M, K, &capacity) || capacity > N
          ? SWOFrontier(N, M, K) : std::vector<FrontierNode>{};
    });
    OFRSuppleControlCounts counts;
    const OFRSuppleControls controls = BuildControls(
        membership, frontier, N, M, K,
        ret->collect_offline_profile ? ret : nullptr, &counts,
        membership.data());
    // Include workspace destruction and releasing the root in real offline
    // time; online no longer retains membership that it never reads.
    std::vector<size_t>().swap(membership);
    ocall_clock(&end);
    ret->gen_perm_time = static_cast<double>(end - start) / 1000.0;
    if (ret->collect_offline_profile)
      ret->offline_heap_peak_bytes = offline_profile::HeapPeakBytes();
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
    if (ret->collect_offline_profile)
      ret->total_heap_peak_bytes = offline_profile::HeapPeakBytes();
    ret->ptime = ret->gen_perm_time + ret->apply_perm_time;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif
    if (encryptBuffer(const_cast<unsigned char *>(plain.data()),
                  static_cast<uint64_t>(blocks), width,
                  encrypt_result_buffer) != SIZE_MAX)
      memory.Complete();
  }
  catch (const std::exception &)
  {
    ret->ptime = ret->gen_perm_time = ret->apply_perm_time = 0.0;
    ret->online_route_ms = ret->online_reorder_ms = 0.0;
    ret->online_copy_ms = ret->online_shuffle_ms = 0.0;
    offline_profile::Reset(ret);
  }
  PRB_pool_shutdown();
  free(decrypted);
}
