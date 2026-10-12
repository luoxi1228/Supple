#define FFOS_FR_PAR_MIN_BLOCKS 1
#define main serial_regression_main
#include "ffos_fr_test.cpp"
#undef main
#include <atomic>
#include <thread>
#ifdef COUNT_OSWAPS
thread_local uint64_t OSWAP_COUNTER = 0;
#endif

static void ParallelCase(size_t n, size_t m, size_t k, size_t width) {
  auto membership = FFOS_CMark(n, m, k);
  auto saved = membership;
  auto frontier = m * k > n ? FFOS_CFrontier(n, m, k) : std::vector<FrontierNode>{};
  auto serial = FFOS_FRControl(membership, frontier, n, m, k);
  auto data = Records(n, width);
  auto expected = FFOS_FRApply(data.data(), serial, frontier, n, m, k, width);
  uint64_t baseline_swaps = 0;
  for (size_t threads : {1U, 2U, 3U, 9U, 17U, 32U, 64U, 0U}) {
    auto actual = FFOS_FR_ParControl(membership, frontier, n, m, k, nullptr, threads);
    assert(membership == saved);
    assert(actual.swo == serial.swo && actual.ofr.size() == serial.ofr.size());
    for (size_t i = 0; i < serial.ofr.size(); ++i) assert(actual.ofr[i] == serial.ofr[i]);
    assert(actual.nodes.size() == serial.nodes.size() && actual.shapes.size() == serial.shapes.size());
    for (size_t i = 0; i < actual.nodes.size(); ++i) {
      auto a = actual.nodes[i], b = serial.nodes[i];
      assert(a.offset == b.offset && a.count == b.count && a.shape_index == b.shape_index && a.postordered == b.postordered);
      auto sa=actual.shapes[a.shape_index], sb=serial.shapes[b.shape_index];
      assert(sa.n==sb.n && sa.n_left==sb.n_left && sa.n_right==sb.n_right);
    }
#ifdef COUNT_OSWAPS
    OSWAP_COUNTER = 0;
#endif
    assert(FFOS_FR_ParApply(data.data(), actual, frontier, n, m, k, width, threads) == expected);
#ifdef COUNT_OSWAPS
    if (threads == 1) baseline_swaps = OSWAP_COUNTER;
    else assert(OSWAP_COUNTER == baseline_swaps);
#endif
  }
  if (!serial.nodes.empty()) {
    serial.nodes.back().offset = SIZE_MAX;
    std::vector<unsigned char> output(m * k * width, 0xA5), before = output;
    supple_parallel::Pool pool(3);
    bool rejected = false;
    try { ReadParallel(data.data(), serial, frontier, n, m, k, width, output.data(), m*k, nullptr, 3); }
    catch (const std::exception &) { rejected = true; }
    assert(rejected && output == before);
  }
}
int main() {
  host_cpu_budget = 64;
  for (size_t width : {4U,8U,16U,24U,40U})
    for (const auto shape : {std::array<size_t,3>{{31,3,1}}, {{31,31,3}}, {{31,3,7}}, {{21,3,7}}, {{17,3,7}}, {{195,3,65}}, {{129,3,65}}, {{11,1,65}}})
      ParallelCase(shape[0], shape[1], shape[2], width);
  assert(dispatched > 0);
  { supple_parallel::Pool pool(3); std::atomic<bool> joined{false}; bool caught = false;
    try { supple_parallel::Fork(1, []{throw std::runtime_error("parent");}, [&]{std::this_thread::yield(); joined = true;}); }
    catch (...) { caught = true; } assert(caught && joined);
    caught = false;
    try { supple_parallel::Fork(1, []{}, []{throw std::runtime_error("worker");}); }
    catch (...) { caught = true; } assert(caught);
    supple_parallel::Fork(2, []{}, []{});
  }
  for (int successful : {0,1,5}) {
    fail_worker_create_after = successful;
    bool failed = false;
    try { supple_parallel::Pool pool(8); } catch (...) { failed = true; }
    assert(failed);
    fail_worker_create_after = -1;
    supple_parallel::Pool pool(8);
    supple_parallel::Fork(7, []{}, []{});
  }
  {
    auto membership = FFOS_CMark(31,3,7), saved = membership;
    auto data = Records(31,16);
    auto controls = FFOS_FRControl(membership,{},31,3,7);
    throw_compact_on_worker = true;
    bool failed = false;
    try { FFOS_FR_ParControl(membership,{},31,3,7,nullptr,3); } catch (...) { failed = true; }
    assert(failed && membership == saved);
    failed = false;
    try { FFOS_FR_ParApply(data.data(),controls,{},31,3,7,16,3); } catch (...) { failed = true; }
    assert(failed);
    throw_compact_on_worker = false;
    assert(FFOS_FR_ParApply(data.data(),controls,{},31,3,7,16,3) == FFOS_FRApply(data.data(),controls,{},31,3,7,16));
    std::vector<unsigned char> output(3*7*16,0xA5), before=output;
    enc_ret ret{};
    fail_worker_create_after = 0;
    DecFFOS_FR_Par(data.data(),31,3,7,16,output.data(),3,&ret);
    fail_worker_create_after = -1;
    assert(ret.parallel_status == 3 && output == before);
    DecFFOS_FR_Par(data.data(),31,3,7,16,output.data(),3,&ret);
    assert(ret.parallel_status == 0);
  }
  bool rejected = false;
  try { ResolveParallelBudget(65, 8192, 100); } catch (...) { rejected = true; }
  assert(rejected);
  puts("PASS: parallel tapes, metadata, complete output, 1/2/3/9/17/32/64/auto budgets, preflight and exception joins");
}
