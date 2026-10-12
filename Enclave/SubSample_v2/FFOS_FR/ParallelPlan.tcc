// Included inside FFOS_FR.cpp's implementation namespace. All plan geometry
// depends only on public dimensions; execution never selects jobs by tags.
#ifndef FFOS_FR_PAR_MIN_BLOCKS
#define FFOS_FR_PAR_MIN_BLOCKS 4096
#endif
struct ParallelPlanNode {
  size_t n = 0, k = 0, output = 0, metadata = 0;
  size_t slice = 0, filter_position = 0, parent_n = 0;
  bool filter = false;
  FFOS_FRControlPositions begin{0, 0}, end{0, 0};
  std::vector<size_t> children;
  supple_parallel::PackedBoundary ofr_boundary, swo_boundary;
};
struct ParallelPlan {
  std::vector<ParallelPlanNode> nodes;
  std::vector<ShapeControlCount> shapes;
  size_t metadata_count = 0;
  size_t Add(const std::vector<FrontierNode> &frontier, size_t n, size_t m,
             size_t k, size_t output, FFOS_FRControlPositions &position,
             FFOS_FRControls *controls) {
    if (nodes.empty()) {
      if (k > nodes.max_size() / 2) throw std::length_error("Parallel FFOS_FR plan size overflow");
      nodes.reserve(2 * k);
      if (controls) controls->nodes.reserve(k);
    }
    const size_t id = nodes.size();
    nodes.emplace_back();
    nodes[id].n = n; nodes[id].k = k; nodes[id].output = output;
    nodes[id].begin = position;
    if (k > 1 && ForkNode(n, m, k)) {
      const size_t left_k = k / 2, left_n = m * left_k;
      const size_t count = ShapeCount(shapes, n, left_n, n - left_n);
      nodes[id].metadata = metadata_count++;
      if (controls) RecordPreparedNode(*controls, position.ofr_words, count,
          n, left_n, n - left_n, PostOrderedNode(n, left_n, n - left_n));
      // CountNode has already checked total lengths. Avoid converting the
      // complete gate offset to bits: a valid packed tape may exceed SIZE_MAX/2.
      position.ofr_words += count;
      const size_t left = Add({}, left_n, m, left_k, output, position, controls);
      const size_t right = Add({}, n - left_n, m, k - left_k,
                               output + left_n, position, controls);
      nodes[id].children = {left, right};
      // Boundary indices are absolute bytes, with only modulo-four gate
      // alignment converted into bit positions.
      const size_t offset = nodes[id].begin.ofr_words;
      auto &boundary = nodes[id].ofr_boundary;
      boundary.Init((offset % 4) * 2, count * 2);
      boundary.first += offset / 4;
      boundary.last += offset / 4;
    } else if (!(k == 1 && n == m)) {
      const auto children = k == 1 ? std::vector<FrontierNode>{{0, 1}} :
                                    ChildrenOrFrontier(frontier, k);
      for (const FrontierNode &child : children) {
        const size_t child_n = FfosCNodeCapacity(n, m, child.count);
        const size_t filter_position = position.swo_bits;
        if (child_n < n) position.swo_bits += n;
        const size_t next = Add({}, child_n, m, child.count,
                               output + m * child.start, position, controls);
        nodes[next].slice = child.start;
        nodes[next].parent_n = n;
        nodes[next].filter = child_n < n;
        nodes[next].filter_position = filter_position;
        if (child_n < n) nodes[next].swo_boundary.Init(filter_position, n);
        nodes[id].children.push_back(next);
      }
    }
    nodes[id].end = position;
    return id;
  }
};

size_t ThreadShare(size_t budget, size_t left, size_t total) {
  // Public arithmetic; compute floor(budget*left/total) without overflowing.
  size_t share = 0, remainder = 0;
  for (size_t i = 0; i < budget; ++i) {
    if (remainder >= total - left) { remainder -= total - left; ++share; }
    else remainder += left;
  }
  return std::max<size_t>(1, std::min(budget - 1, share));
}
template<class Operation>
void RunPlanGroups(const ParallelPlan &plan, const std::vector<size_t> &ids,
                   size_t first, size_t end, size_t budget, size_t n,
                   const Operation &operation) {
  if (first == end) return;
  if (end - first == 1) { operation(first, budget); return; }
  if (budget <= 1 || n < FFOS_FR_PAR_MIN_BLOCKS) {
    for (size_t i = first; i < end; ++i) operation(i, 1);
    return;
  }
  const size_t middle = first + (end - first) / 2;
  size_t left = 0, total = 0;
  for (size_t i = first; i < end; ++i) {
    total += plan.nodes[ids[i]].k;
    if (i < middle) left += plan.nodes[ids[i]].k;
  }
  const size_t left_threads = ThreadShare(budget, left, total);
  const size_t right_id = g_thread_id + left_threads;
  supple_parallel::Fork(right_id,
      [&] { RunPlanGroups(plan, ids, first, middle, left_threads, n, operation); },
      [&] { RunPlanGroups(plan, ids, middle, end, budget - left_threads, n, operation); });
}
struct ParallelWriteState {
  ParallelPlan plan;
  std::vector<std::unique_ptr<ControlWriteContext>> contexts;
  std::vector<enc_ret> profiles;
  bool profiling;
  size_t root_k;
  ParallelWriteState(size_t threads, bool profiling, size_t k)
      : contexts(threads), profiles(threads), profiling(profiling), root_k(k) {}
  ControlWriteContext &Context(size_t k) {
    auto &context = contexts[g_thread_id];
    if (!context) context.reset(new ControlWriteContext(root_k));
    return *context;
  }
  enc_ret *Profile() { return profiling ? &profiles[g_thread_id] : nullptr; }
};
void MergeOfflineProfile(enc_ret *destination, const std::vector<enc_ret> &sources) {
  if (!destination) return;
  for (const auto &source : sources) {
    destination->offline_tags_ms += source.offline_tags_ms;
    destination->offline_normalize_ms += source.offline_normalize_ms;
    destination->offline_ofr_write_ms += source.offline_ofr_write_ms;
    destination->offline_replay_ms += source.offline_replay_ms;
    destination->offline_project_ms += source.offline_project_ms;
    destination->offline_swo_write_ms += source.offline_swo_write_ms;
    destination->offline_prepare_ms += source.offline_prepare_ms;
  }
}
