struct ParallelReadContext {
  explicit ParallelReadContext(size_t k) : workspaces(ffos_c_detail::WorkspaceDepth(k) + 1) {}
  std::vector<ffos_c_detail::FfosCDataWorkspace> workspaces;
  ffos_c_detail::SelectedScratch selected;
};
struct ParallelReadState {
  ParallelPlan plan;
  std::vector<std::unique_ptr<ParallelReadContext>> contexts;
  std::vector<enc_ret> profiles;
  const FFOS_FRControls &controls;
  unsigned char *output;
  size_t m, width, root_k;
  bool profiling;
  ParallelReadState(size_t threads, const FFOS_FRControls &controls,
                    unsigned char *output, size_t m, size_t width, size_t k, bool profiling)
      : contexts(threads), profiles(threads), controls(controls), output(output),
        m(m), width(width), root_k(k), profiling(profiling) {}
  ParallelReadContext &Context() {
    auto &context = contexts[g_thread_id];
    if (!context) context.reset(new ParallelReadContext(root_k));
    return *context;
  }
  enc_ret *Profile() { return profiling ? &profiles[g_thread_id] : nullptr; }
};
void ReadParallelNode(ParallelReadState &state, size_t id,
                      const unsigned char *input, size_t depth, size_t budget) {
  const auto &node = state.plan.nodes[id];
  enc_ret *profile = state.Profile();
  double *copy_ms = profile ? &profile->online_copy_ms : nullptr;
  double *route_ms = profile ? &profile->online_route_ms : nullptr;
  unsigned char *destination = state.output + node.output * state.width;
  if (node.k == 1 && node.n == state.m) {
    online_profile::Track(copy_ms, [&] {
      if (destination != input) std::memmove(destination, input, state.m * state.width);
    });
    return;
  }
  if (ForkNode(node.n, state.m, node.k)) {
    online_profile::Track(copy_ms, [&] {
      if (destination != input) std::memmove(destination, input, node.n * state.width);
    });
    const auto &metadata = state.controls.nodes[node.metadata];
    const auto &shape = state.controls.shapes[metadata.shape_index];
    online_profile::Track(route_ms, [&] {
      const auto tape = state.controls.ofr.view(metadata.offset, metadata.count);
      if (metadata.postordered)
        ofr::OFRApplyPreparedPostOrderInPlace(destination, tape,
            shape.n, shape.n_left, shape.n_right, state.width, true);
      else ofr::OFRApplyPreparedInPlace(destination, tape,
            shape.n, shape.n_left, shape.n_right, state.width);
    });
    RunPlanGroups(state.plan, node.children, 0, node.children.size(), budget, node.n,
      [&](size_t index, size_t child_budget) {
        const size_t child = node.children[index];
        const size_t relative = state.plan.nodes[child].output - node.output;
        ReadParallelNode(state, child, destination + relative * state.width,
                         depth + 1, child_budget);
      });
    return;
  }
  RunPlanGroups(state.plan, node.children, 0, node.children.size(), budget, node.n,
    [&](size_t index, size_t child_budget) {
      const size_t child_id = node.children[index];
      const auto &child_node = state.plan.nodes[child_id];
      auto &context = state.Context();
      auto &child = context.workspaces[depth];
      enc_ret *branch_profile = state.Profile();
      online_profile::Track(branch_profile ? &branch_profile->online_copy_ms : nullptr, [&] {
        child.data_buf.reserve(node.n * state.width);
        child.ensure_capacity(node.n, state.width);
        std::memcpy(child.data_ptr(), input, node.n * state.width);
      });
      if (child_node.filter) {
        bool *selected = context.selected.acquire(node.n);
        if (!selected) throw std::bad_alloc();
        online_profile::Track(branch_profile ? &branch_profile->online_route_ms : nullptr, [&] {
          ffos_c_detail::UnpackControlBitsToBoolArray(state.controls.swo,
              child_node.filter_position, node.n, selected);
          TightCompact_v2(child.data_ptr(), node.n, state.width, selected);
        });
      }
      ReadParallelNode(state, child_id, child.data_ptr(), depth + 1, child_budget);
    });
}
FFOS_FRReadResult ReadParallel(const unsigned char *data,
    const FFOS_FRControls &controls, const std::vector<FrontierNode> &frontier,
    size_t n, size_t m, size_t k, size_t width, unsigned char *output,
    size_t blocks, enc_ret *profile, size_t threads) {
  // Raw DFS tapes remain compatible through the existing checked reader.
  if (controls.nodes.empty() && controls.ofr.size() != 0)
    return FFOS_FRControlRead(data, controls, frontier, n, m, k, width,
        output, blocks, {0, 0}, profile, FFOS_FRBackend::FourGateSSE2);
  CheckDimensions(n, m, k);
  CheckFrontier(frontier, n, m, k);
  size_t bytes = 0, required = 0, output_bytes = 0;
  if (!data || !output || !width || MulOverflowSizeT(n, width, &bytes) ||
      MulOverflowSizeT(m, k, &required) || MulOverflowSizeT(blocks, width, &output_bytes))
    throw std::invalid_argument("Invalid parallel FFOS_FR dimensions");
  if (blocks < required) throw std::length_error("Parallel FFOS_FR output capacity");
  std::vector<ShapeControlCount> shapes;
  size_t metadata = 0;
  const auto planned_end = ValidateReadPlanNode(controls, frontier, n, m, k,
                                               {0, 0}, metadata, shapes);
  if (metadata != controls.nodes.size())
    throw std::logic_error("Parallel FFOS_FR prepared node count mismatch");
  ParallelReadState state(threads, controls, output, m, width, k, profile != nullptr);
  FFOS_FRControlPositions end{0, 0};
  state.plan.Add(frontier, n, m, k, 0, end, nullptr);
  if (end.swo_bits != planned_end.swo_bits || end.ofr_words != planned_end.ofr_words)
    throw std::logic_error("Parallel FFOS_FR read plan mismatch");
  ReadParallelNode(state, 0, data, 0, threads);
  if (profile) for (const auto &source : state.profiles) {
    profile->online_copy_ms += source.online_copy_ms;
    profile->online_route_ms += source.online_route_ms;
    profile->online_shuffle_ms += source.online_shuffle_ms;
  }
  return {required, end};
}

struct ParallelBudget { size_t requested, available, threads; };
ParallelBudget ResolveParallelBudget(size_t requested, size_t n, size_t k,
                                     enc_ret *ret = nullptr) {
  size_t available = 0;
  ocall_ffos_fr_cpu_budget(&available);
  if (!available) available = 1;
  if (ret) {
    ret->requested_threads = requested;
    ret->available_cpus = available;
    ret->effective_threads = 0;
    ret->parallel_status = 0;
  }
  if (requested > available) {
    if (ret) ret->parallel_status = 1;
    throw std::invalid_argument("FFOS_FR thread budget exceeds available CPUs");
  }
  const size_t threads = n < FFOS_FR_PAR_MIN_BLOCKS ? 1 :
      std::max<size_t>(1, std::min(k, requested ? requested : available));
#ifndef BEFTS_MODE
  if (threads > SUPPLE_TCS_CAPACITY) {
    if (ret) ret->parallel_status = 2;
    throw std::runtime_error("FFOS_FR TCS capacity insufficient; rebuild and sign for this CPU budget");
  }
#endif
  if (ret) ret->effective_threads = threads;
  return {requested, available, threads};
}
