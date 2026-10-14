#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main legacy_c_test_main
#include "../ffos_c/serial_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "../../Enclave/SubSample_v2/FFOS_C_Opt/FFOS_C_Opt.cpp"
thread_local uint64_t OSWAP_COUNTER=0;

static size_t opt_cases=0;
static void VerifyOpt(size_t n,size_t m,const Samples &samples,size_t width) {
  const size_t k=samples.size(), words=MarkWords(k);
  std::vector<size_t> membership(n*words,0);
  for (size_t j=0;j<k;++j) for (size_t id:samples[j])
    membership[id*words+j/FfosCWordBits()] |= size_t(1)<<(j%FfosCWordBits());
  const auto saved_membership=membership;
  const auto frontier=m*k>n ? FFOS_CFrontier(n,m,k) : std::vector<FrontierNode>{};
  compact_calls=compact_items=0;
  OSWAP_COUNTER=0;
  const auto legacy=FFOS_CControl(membership,frontier,n,m,k);
  const size_t offline_calls=compact_calls.load(), offline_items=compact_items.load();
  const uint64_t offline_gates=OSWAP_COUNTER;
  compact_calls=compact_items=0;
  OSWAP_COUNTER=0;
  const auto controls=FFOS_C_OptControl(membership,frontier,n,m,k);
  assert(controls.bits==legacy && membership==saved_membership);
  assert(compact_calls==offline_calls && compact_items==offline_items && OSWAP_COUNTER==offline_gates);
  auto owned=membership;
  const auto internal=ffos_c_opt_detail::Build(owned,frontier,n,m,k,nullptr,owned.data());
  assert(internal.bits==legacy);
  const auto data=records(n,width), saved=data;
  compact_calls=compact_items=0;
  OSWAP_COUNTER=0;
  const auto expected=FFOS_CApply(data.data(),legacy,frontier,n,m,k,width);
  const size_t calls=compact_calls.load(), items=compact_items.load();
  const uint64_t gates=OSWAP_COUNTER;
  compact_calls=compact_items=0;
  OSWAP_COUNTER=0;
  const auto actual=FFOS_C_OptApply(data.data(),controls,n,m,k,width);
  assert(actual==expected && data==saved);
  assert(compact_calls==calls && compact_items==items && OSWAP_COUNTER==gates);
  check_samples(actual,data,samples,m,width);
  // Guard both ends and unused output capacity; all validation precedes writes.
  std::vector<unsigned char> guarded(actual.size()+2*width,0xa5);
  auto result=FFOS_C_OptControlRead(data.data(),controls,n,m,k,width,
                                   guarded.data()+width,m*k+1);
  assert(result.next_pos==controls.bit_count && result.written_blocks==m*k);
  assert(std::equal(actual.begin(),actual.end(),guarded.begin()+width));
  for (size_t i=0;i<width;++i) {
    assert(guarded[i]==0xa5 && guarded[width+actual.size()+i]==0xa5);
  }
  auto bad=controls;
  bad.nodes.back().control_offset++;
  const auto before=guarded;
  bool rejected=false;
  try { FFOS_C_OptControlRead(data.data(),bad,n,m,k,width,
                             guarded.data()+width,m*k+1); }
  catch (const std::exception &) { rejected=true; }
  assert(rejected && guarded==before);
  // Incomplete plans, truncated tapes and undersized/overlapping buffers.
  for (int invalid=0;invalid<4;++invalid) {
    bad=controls;
    if (invalid==0) bad.nodes.pop_back();
    if (invalid==1) { if (!bad.bits.empty()) bad.bits.pop_back(); else ++bad.bit_count; }
    rejected=false;
    try {
      FFOS_C_OptControlRead(data.data(),bad,n,m,k,width,
          invalid==3 ? const_cast<unsigned char *>(data.data()) : guarded.data()+width,
          invalid==2 ? m*k-1 : m*k+1);
    } catch (const std::exception &) { rejected=true; }
    assert(rejected && guarded==before && data==saved);
  }
  ++opt_cases;
}

int main() {
  for (size_t n=1;n<=5;++n) for (size_t m=1;m<=n;++m) {
    Samples choices;
    for (size_t mask=0;mask<(size_t(1)<<n);++mask) {
      std::vector<size_t> sample;
      for (size_t i=0;i<n;++i) if ((mask>>i)&1) sample.push_back(i);
      if (sample.size()==m) choices.push_back(sample);
    }
    for (const auto &a:choices) for (const auto &b:choices) for (const auto &c:choices)
      VerifyOpt(n,m,{a,b,c},8);
  }
  for (size_t n:{size_t(9),size_t(32),size_t(65),size_t(128)})
    for (size_t k:{size_t(1),size_t(2),size_t(3),size_t(8),size_t(17),size_t(63),
                   size_t(64),size_t(65),size_t(127),size_t(129),size_t(257)})
      for (size_t width:{size_t(4),size_t(8),size_t(12),size_t(16),size_t(24),size_t(64)})
        for (size_t m:{size_t(1),std::max(size_t(1),n/k),std::max(size_t(1),n/4),n}) {
          Samples samples(k);
          for (auto &sample:samples) {
            for (size_t i=0;i<n;++i) sample.push_back(i);
            std::shuffle(sample.begin(),sample.end(),rng);
            sample.resize(m);
          }
          VerifyOpt(n,m,samples,width);
        }
  for (size_t k:{size_t(1),size_t(3),size_t(64),size_t(129)}) {
    const size_t n=128,m=2,width=16;
    auto data=records(n,width);
    rng.seed(718);
    const auto expected=FFOS_C(data.data(),n,m,k,width);
    rng.seed(718);
    assert(FFOS_C_Opt(data.data(),n,m,k,width)==expected);
    std::vector<unsigned char> output(expected.size());
    enc_ret ret{};
    ret.collect_online_profile=ret.collect_offline_profile=1;
    rng.seed(718);
    DecFFOS_C_Opt(data.data(),n,m,k,width,output.data(),&ret);
    assert(output==expected);
  }
  {
    const auto membership=FFOS_CMark(2,1,1);
    const auto controls=FFOS_C_OptControl(membership,{},2,1,1);
    unsigned char input[16]{}, output[16]{};
    for (int invalid=0;invalid<3;++invalid) {
      bool rejected=false;
      try {
        FFOS_C_OptControlRead(invalid==0 ? nullptr : input,controls,2,1,1,
            invalid==1 ? 0 : invalid==2 ? SIZE_MAX : 8,output,1);
      } catch (const std::exception &) { rejected=true; }
      assert(rejected);
      for (unsigned char byte:output) assert(byte==0);
    }
    bool rejected=false;
    try { FFOS_C_OptControl({}, {},2,1,1); }
    catch (const std::exception &) { rejected=true; }
    assert(rejected);
  }
  std::printf("PASS: %zu optimized C cases: exact tape/output/gates, owned roots, guards and preflight\n",opt_cases);
}
