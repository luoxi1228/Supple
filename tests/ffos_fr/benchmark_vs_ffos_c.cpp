#include <chrono>
#define ocall_clock host_stub_ocall_clock
#include "../ffos_c/host_support.hpp"
#undef ocall_clock
static void ocall_clock(long *t) {
  *t = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
#ifdef FFOS_LEGACY_SOURCE
#include "../../Enclave/SubSample_v2/SWO/helper.cpp"
#include "../../Enclave/SubSample_v2/SWO/SuppleSWO.cpp"
#include "../../Enclave/SubSample_v2/OFRSupple/OFRSupple.cpp"
#include "legacy_source.hpp"
#else
#include "../../Enclave/SubSample_v2/FFOS_C/helper.cpp"
#include "../../Enclave/SubSample_v2/FFOS_C/FFOS_C.cpp"
#include "../../Enclave/SubSample_v2/FFOS_FR/FFOS_FR.cpp"
#include "../../Enclave/SubSample_v2/FFOS_FR_Par/FFOS_FR_Par.cpp"
#include "../../Enclave/SubSample_v2/FFOS_C_Opt/FFOS_C_Opt.cpp"
#endif
#include <string>

thread_local uint64_t OSWAP_COUNTER = 0;
#ifdef SUPPLE_MEMORY_TRACKING
extern "C" void *__wrap_malloc(size_t);
extern "C" void __wrap_free(void *);
void *operator new(size_t n) {
  void *p = __wrap_malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void *p) noexcept { __wrap_free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
#endif

namespace {
#ifdef FFOS_LEGACY_SOURCE
constexpr size_t kMethodCount = 2;
#else
constexpr size_t kMethodCount = 4;
#endif
const char *MethodName(size_t method) {
  static const char *names[] = {"FFOS_C", "FFOS_FR", "FFOS_FR_Par", "FFOS_C_Opt"};
  return names[method];
}
volatile size_t benchmark_sink = 0;
size_t requested_threads = 0;
template <class Tape> auto TapeBytes(const Tape &t) -> decltype(t.byte_size()) {
  return t.byte_size();
}
size_t TapeBytes(const std::vector<uint8_t> &t) { return t.size(); }
template <class F> double TimeMs(F fn) {
  const auto begin = std::chrono::steady_clock::now();
  fn();
  return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
}
double Median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const size_t half = values.size()/2;
  return values.size()%2 ? values[half] : (values[half-1]+values[half])/2;
}
struct Result {
  double control = 0, apply = 0, total = 0;
  size_t tape_bytes = 0, heap = 0;
  enc_ret phases{};
};

Result Measure(size_t method, const std::vector<size_t> &membership,
               const std::vector<unsigned char> &data,
               const std::vector<FrontierNode> &frontier,
               size_t n, size_t m, size_t k, size_t width,
               bool memory, bool profile) {
  Result result;
  enc_ret ret{};
  ret.collect_memory_profile = memory;
  {
    memory_profile::Scope scope(&ret);
#ifndef FFOS_LEGACY_SOURCE
    if (method == 3) {
      FFOS_C_OptControls controls;
      result.control = TimeMs([&] {
        controls = FFOS_C_OptControl(membership, frontier, n, m, k,
                                    profile ? &result.phases : nullptr);
      });
      result.tape_bytes = controls.bits.size();
      result.apply = TimeMs([&] {
        const auto output = FFOS_C_OptApply(data.data(), controls, n, m, k, width);
        benchmark_sink ^= output[output.size()/3];
      });
    } else
#endif
    if (method != 0) {
      FFOS_FRControls controls;
#ifndef FFOS_LEGACY_SOURCE
      std::unique_ptr<supple_parallel::Pool> parallel_pool;
      size_t threads = 0;
#endif
      result.control = TimeMs([&] {
#ifndef FFOS_LEGACY_SOURCE
        if (method == 2) {
          threads = ResolveParallelBudget(requested_threads, n, k, &result.phases).threads;
          parallel_pool.reset(new supple_parallel::Pool(threads));
          controls = BuildControls(membership, frontier, n, m, k,
              profile ? &result.phases : nullptr, nullptr, nullptr, threads);
        } else
#endif
        controls = FFOS_FRControl(membership, frontier, n, m, k,
                                   profile ? &result.phases : nullptr);
      });
      result.tape_bytes = controls.swo.size() + TapeBytes(controls.ofr);
      result.apply = TimeMs([&] {
        const auto output =
#ifndef FFOS_LEGACY_SOURCE
          method == 2 ? ApplyKnownControls(data.data(), controls, frontier, n, m, k, width,
              FFOS_FRControlCount(frontier, n, m, k), FFOS_FRBackend::FourGateSSE2, threads) :
#endif
          FFOS_FRApply(data.data(), controls, frontier, n, m, k, width);
        benchmark_sink ^= output[output.size()/3];
#ifndef FFOS_LEGACY_SOURCE
        if (parallel_pool) parallel_pool->Close();
#endif
      });
    } else {
      std::vector<uint8_t> controls;
      result.control = TimeMs([&] {
        controls = FFOS_CControl(membership, frontier, n, m, k);
      });
      result.tape_bytes = controls.size();
      result.apply = TimeMs([&] {
        const auto output = FFOS_CApply(data.data(), controls, frontier,
                                     n, m, k, width);
        benchmark_sink ^= output[output.size()/3];
      });
    }
    scope.Complete();
  }
  if (memory && ret.memory_profile_status != MEMORY_PROFILE_VALID)
    throw std::runtime_error("Native heap measurement invalid");
  result.heap = ret.algorithm_heap_peak_bytes;
  result.total = result.control + result.apply;
  return result;
}

void Print(const char *kind, const char *name, size_t n, size_t m, size_t k,
           size_t width, const char *method, size_t round, bool memory,
           bool profile, const Result &r) {
  std::printf("%s,%s,%zu,%zu,%zu,%zu,%s,%zu,%d,%d,%.6f,%.6f,%.6f,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%zu,%zu,%zu\n",
      kind,name,n,m,k,width,method,round,int(memory),int(profile),r.control,
      r.apply,r.total,r.tape_bytes,r.heap,
      r.phases.offline_tags_ms,r.phases.offline_normalize_ms,
      r.phases.offline_ofr_write_ms,r.phases.offline_replay_ms,
      r.phases.offline_project_ms,r.phases.offline_prepare_ms,
      r.phases.requested_threads,r.phases.available_cpus,r.phases.effective_threads);
  std::fflush(stdout);
}
void Run(const char *name, size_t n, size_t m, size_t k, size_t width,
         size_t rounds, size_t warmup, bool memory, bool profile) {
  if (n == 0 || m == 0 || m > n || k == 0 || width < sizeof(uint32_t) ||
      k > SIZE_MAX/m || n > SIZE_MAX/width)
    throw std::invalid_argument("Invalid benchmark dimensions");
  rng.seed(20260927);
  const std::vector<size_t> membership = FFOS_CMark(n,m,k);
  const std::vector<FrontierNode> frontier = m*k > n ?
      FFOS_CFrontier(n,m,k) : std::vector<FrontierNode>{};
  std::vector<unsigned char> data(n*width);
  for (size_t i=0;i<data.size();++i) data[i] = uint8_t(i*37+13);
  std::vector<double> control[kMethodCount], apply[kMethodCount], total[kMethodCount];
  Result latest[kMethodCount];
  size_t peaks[kMethodCount] = {};
  for (size_t round=0;round<warmup+rounds;++round) {
    for (size_t j=0;j<kMethodCount;++j) {
      const size_t method = (round+j)%kMethodCount;
      Result r = Measure(method,membership,data,frontier,n,m,k,width,
                         memory,profile);
      if (round < warmup) continue;
      latest[method] = r;
      peaks[method] = std::max(peaks[method],r.heap);
      control[method].push_back(r.control);
      apply[method].push_back(r.apply);
      total[method].push_back(r.control+r.apply);
      Print("round",name,n,m,k,width,MethodName(method),
            round-warmup,memory,profile,r);
    }
  }
  for (size_t method=0;method<kMethodCount;++method) {
    Result r = latest[method];
    r.control=Median(control[method]);
    r.apply=Median(apply[method]);
    r.heap=peaks[method];
    // Each median row reports the median per-round total.
    r.total=Median(total[method]);
    Print("median",name,n,m,k,width,MethodName(method),
          rounds,memory,profile,r);
  }
}
} // namespace

int main(int argc,char **argv) {
  try {
    size_t rounds=7,warmup=2,n=0,m=0,k=0,width=0;
    bool matrix=false,memory=false,profile=false;
    for (int arg=1;arg<argc;++arg) {
      const std::string token(argv[arg]);
      if (token=="--matrix") matrix=true;
      else if (token=="--memory") memory=true;
      else if (token=="--profile") profile=true;
      else if (token=="--threads" && arg+1<argc) { const std::string value(argv[++arg]); requested_threads = value == "auto" ? 0 : std::stoull(value); }
      else if (token=="--warmup" && arg+1<argc) warmup=std::stoull(argv[++arg]);
      else if (token=="--case" && arg+4<argc) {
        n=std::stoull(argv[++arg]);m=std::stoull(argv[++arg]);
        k=std::stoull(argv[++arg]);width=std::stoull(argv[++arg]);
      } else if (!token.empty() && token[0]!='-') rounds=std::stoull(token);
      else throw std::invalid_argument("Usage: benchmark [rounds] [--warmup N] [--matrix | --case N M K WIDTH] [--memory] [--profile]");
    }
    if (rounds==0 || warmup>SIZE_MAX-rounds)
      throw std::invalid_argument("Invalid round count");
    std::puts("kind,case,n,m,k,width,method,round,memory_tracking,profile,control_ms,apply_ms,total_ms,control_bytes,control_apply_heap_peak_bytes,tags_work_ms,normalize_work_ms,write_work_ms,replay_work_ms,project_work_ms,prepare_work_ms,requested_threads,available_cpus,effective_threads");
    if (n) Run("custom",n,m,k,width,rounds,warmup,memory,profile);
    else if (matrix) {
      for (size_t size : {size_t(1)<<16,size_t(1)<<18,size_t(1)<<20,
                          size_t(1)<<22,size_t(1)<<24})
        Run("equal_k16",size,size/16,16,8,rounds,warmup,memory,profile);
      for (size_t size : {size_t(1)<<20,size_t(1)<<22})
        Run("equal_k64",size,size/64,64,16,rounds,warmup,memory,profile);
    } else {
      Run("compact",4096,16,16,16,rounds,warmup,memory,profile);
      Run("equal",4096,64,64,16,rounds,warmup,memory,profile);
      Run("frontier",4096,64,128,16,rounds,warmup,memory,profile);
      Run("mixed",4096,48,128,16,rounds,warmup,memory,profile);
      Run("equal_wide",4096,64,64,64,rounds,warmup,memory,profile);
      Run("equal_256",4096,64,64,256,rounds,warmup,memory,profile);
      Run("mixed_256",4096,48,128,256,rounds,warmup,memory,profile);
    }
    return 0;
  } catch(const std::exception &error) {
    std::fprintf(stderr,"%s\n",error.what());return 1;
  }
}
