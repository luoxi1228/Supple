#include <cstdlib>
#include <new>
static long fail_after=-1;
#include "../ffos_c/host_support.hpp"
#include "../../Enclave/SubSample_v2/FFOS_C/helper.cpp"
#include "../../Enclave/SubSample_v2/FFOS_C/FFOS_C.cpp"
#include "../../Enclave/SubSample_v2/FFOS_C_Opt/FFOS_C_Opt.cpp"
#include "../../Enclave/SubSample_v2/FFOS_FR/FFOS_FR.cpp"
#include "../../Enclave/MemoryProfile.cpp"
thread_local uint64_t OSWAP_COUNTER=0;
void *operator new(size_t n) {
  if (fail_after==0) throw std::bad_alloc();
  if (fail_after>0) --fail_after;
  void *p=__wrap_malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void *p) noexcept { __wrap_free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }

static enc_ret Run(int mode,const std::vector<unsigned char> &input,
    std::vector<unsigned char> &output,size_t n,size_t m,size_t k,size_t width) {
  enc_ret ret{};
  ret.collect_memory_profile=1;
  rng.seed(718);
  if (mode==4) DecFFOS_C(const_cast<unsigned char *>(input.data()),n,m,k,width,output.data(),&ret);
  if (mode==5) DecFFOS_FR(const_cast<unsigned char *>(input.data()),n,m,k,width,output.data(),&ret);
  if (mode==7) DecFFOS_C_Opt(const_cast<unsigned char *>(input.data()),n,m,k,width,output.data(),&ret);
  return ret;
}
int main() {
  std::setbuf(stdout,nullptr);
  std::puts("n,m,k,width,mode,peak_bytes,gates");
  for (size_t n:{size_t(65536),size_t(1048576)}) {
    const size_t k=64,m=n/k,width=16;
    std::vector<unsigned char> input(n*width),output(m*k*width),expected;
    size_t legacy_peak=0,opt_peak=0;
    uint64_t legacy_gates=0;
    for (int mode:{4,5,7}) {
      size_t first_peak=0;
      uint64_t last_gates=0;
      for (int round=0;round<2;++round) {
        auto ret=Run(mode,input,output,n,m,k,width);
        assert(ret.memory_profile_status==MEMORY_PROFILE_VALID && tracker.current==0);
        if (round) assert(ret.algorithm_heap_peak_bytes==first_peak);
        first_peak=ret.algorithm_heap_peak_bytes;
        last_gates=ret.OSWAP_count;
        if (mode==4) { expected=output; legacy_peak=first_peak; legacy_gates=ret.OSWAP_count; }
        if (mode==7) { assert(output==expected && ret.OSWAP_count==legacy_gates); opt_peak=first_peak; }
      }
      std::printf("%zu,%zu,%zu,%zu,%d,%zu,%llu\n",n,m,k,width,mode,first_peak,
                  static_cast<unsigned long long>(last_gates));
    }
    // Detect accidental restoration of per-depth payload allocations.
    assert(opt_peak<legacy_peak && opt_peak<3*n*width+7*n);
  }
  // Every throwing STL/scratch allocation must unwind the ECALL cleanly.
  const size_t n=128,m=2,k=64,width=16;
  std::vector<unsigned char> input(n*width),output(m*k*width);
  size_t failures=0;
  for (long point=0;point<1024;++point) {
    fail_after=point;
    auto ret=Run(7,input,output,n,m,k,width);
    fail_after=-1;
    assert(tracker.current==0);
    if (ret.memory_profile_status==MEMORY_PROFILE_VALID) break;
    assert(ret.memory_profile_status==MEMORY_PROFILE_INVALID);
    ++failures;
  }
  assert(failures>10 && failures<1024);
  assert(Run(7,input,output,n,m,k,width).memory_profile_status==MEMORY_PROFILE_VALID);
  std::printf("PASS: exact repeated peaks; %zu allocation-failure unwind points\n",failures);
}
