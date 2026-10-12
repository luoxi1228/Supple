#define BEFTS_MODE
#define COUNT_OSWAPS
#include <cstdint>
thread_local uint64_t OSWAP_COUNTER = 0;
#include "../../Enclave/ThreadPool.cpp"
#include <cassert>
int main() {
  for (size_t round=0;round<5;++round) {
    supple_parallel::Pool pool(17);
    OSWAP_COUNTER=0;
    supple_parallel::Fork(9, []{OSWAP_COUNTER+=2;}, [] {
      OSWAP_COUNTER+=3;
      supple_parallel::Fork(16, []{OSWAP_COUNTER+=5;}, []{OSWAP_COUNTER+=7;});
    });
    assert(OSWAP_COUNTER==17);
    supple_parallel::Fork(9, []{}, []{OSWAP_COUNTER+=11;});
    assert(OSWAP_COUNTER==28);
  }
  puts("PASS: worker TLS counter merge, nested joins and repeated pools");
}
