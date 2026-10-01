// Test the production tracker with deterministic allocator adapters. Explicit
// new/delete adapters cover STL capacity changes without relying on host DSO
// interposition (the production SGX C++ library is statically linked).
#include "../../Enclave/MemoryProfile.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

extern "C" {
void *__wrap_malloc(size_t);
void *__wrap_calloc(size_t, size_t);
void *__wrap_realloc(void *, size_t);
void __wrap_free(void *);
void *__wrap_memalign(size_t, size_t);
int __wrap_posix_memalign(void **, size_t, size_t);
void *__wrap_aligned_alloc(size_t, size_t);
}
static bool fail_next = false, move_next = false, inplace_next = false, tiny_next = false;
static size_t old_realloc_bytes = 0;
extern "C" void *__real_malloc(size_t n) {
  if (fail_next) { fail_next = false; return nullptr; }
  if (tiny_next) { tiny_next = false; return std::malloc(1); }
  return std::malloc(n);
}
extern "C" void __real_free(void *p) { std::free(p); }
extern "C" void *__real_calloc(size_t n, size_t width) {
  if (width && n > SIZE_MAX / width) return nullptr;
  // Exercise nested allocator APIs: only the outer request must be counted.
  void *p = __wrap_malloc(n * width);
  if (p) std::memset(p, 0, n * width);
  return p;
}
extern "C" void *__real_realloc(void *p, size_t n) {
  if (fail_next) { fail_next = false; return nullptr; }
  if (p && n == 0) { std::free(p); return nullptr; }
  if (inplace_next) { inplace_next = false; return p; }
  if (move_next) {
    move_next = false;
    void *q = std::malloc(n);
    assert(q);
    std::memcpy(q, p, old_realloc_bytes < n ? old_realloc_bytes : n);
    std::free(p);
    return q;
  }
  return std::realloc(p, n);
}
extern "C" int __real_posix_memalign(void **p, size_t alignment, size_t n) {
  return ::posix_memalign(p, alignment, n);
}
extern "C" void *__real_memalign(size_t alignment, size_t n) {
  void *p = nullptr;
  if (::posix_memalign(&p, alignment, n) != 0) return nullptr;
  return p;
}
extern "C" void *__real_aligned_alloc(size_t alignment, size_t n) {
  return __real_memalign(alignment, n);
}
void *operator new(size_t n) {
  void *p = __wrap_malloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void *p) noexcept { __wrap_free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }

template<class Work> static enc_ret Measure(Work work, bool complete = true) {
  enc_ret ret{};
  ret.collect_memory_profile = 1;
  {
    memory_profile::Scope scope(&ret);
    work();
    if (complete) scope.Complete();
  }
  return ret;
}
static void Expect(const enc_ret &ret, size_t peak) {
  assert(ret.memory_profile_status == MEMORY_PROFILE_VALID);
  assert(ret.algorithm_heap_peak_bytes == peak);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const char *test = argv[1];
  if (std::strcmp(test, "core") == 0) {
    void *setup = __wrap_malloc(5000); // enclave initialization is excluded
    Expect(Measure([&] {
      __wrap_free(setup);
      void *a = __wrap_malloc(100), *b = __wrap_calloc(3, 50);
      __wrap_free(a);
      void *c = __wrap_malloc(200);
      __wrap_free(b); __wrap_free(c); __wrap_free(nullptr);
    }), 350);
    Expect(Measure([] {
      std::vector<unsigned char> v;
      v.reserve(100);
      v.reserve(200); // the new buffer exists before the old one is freed
      v.clear();     // retained capacity still consumes 200 bytes
      void *p = __wrap_malloc(50);
      __wrap_free(p);
    }), 300);
    void *cache = nullptr;
    Expect(Measure([&] { cache = __wrap_malloc(100); }), 100);
    Expect(Measure([] {}), 100); // preheated round inherits persistent scratch
    Expect(Measure([&] {
      void *p = __wrap_malloc(40); __wrap_free(p);
    }), 140);
    __wrap_free(cache); // a free between rounds must update the ledger
    Expect(Measure([] {}), 0);
    Expect(Measure([] {
      void *a = __wrap_memalign(64, 37), *b = nullptr;
      assert(__wrap_posix_memalign(&b, 128, 41) == 0);
      void *c = __wrap_aligned_alloc(64, 64);
      __wrap_free(a); __wrap_free(b); __wrap_free(c);
    }), 142);
    Expect(Measure([] {
      void *p = __wrap_malloc(100);
      move_next = true; old_realloc_bytes = 100;
      void *q = __wrap_realloc(p, 200);
      assert(q != p);
      __wrap_free(q);
    }), 300);
    Expect(Measure([] {
      void *p = __wrap_realloc(nullptr, 30);
      assert(__wrap_realloc(p, 0) == nullptr);
    }), 30);
    Expect(Measure([] {
      void *p = __wrap_malloc(100);
      inplace_next = true;
      assert(__wrap_realloc(p, 50) == p);
      void *q = __wrap_malloc(40);
      __wrap_free(q); __wrap_free(p);
    }), 100);
    Expect(Measure([] {
      void *p = __wrap_calloc(0, 100); __wrap_free(p);
    }), 0);
    // Repeated arbitrary deletions exercise collision clusters and slot reuse.
    Expect(Measure([] {
      void *blocks[257]{};
      for (size_t round = 0; round < 100; ++round) {
        for (size_t i = 0; i < 257; ++i) blocks[i] = __wrap_malloc(i + 1);
        for (size_t i = 0; i < 257; ++i) __wrap_free(blocks[(i * 73) % 257]);
      }
    }), 257 * 258 / 2);
    enc_ret unwound{};
    unwound.collect_memory_profile = 1;
    try {
      memory_profile::Scope scope(&unwound);
      std::vector<char> data(100);
      throw 42;
    } catch (int) {}
    assert(unwound.memory_profile_status == MEMORY_PROFILE_INVALID);
    assert(unwound.algorithm_heap_peak_bytes == 100);
    Expect(Measure([] {}), 0); // destructors ran before the scope finished
  } else if (std::strcmp(test, "failure") == 0) {
    auto ret = Measure([] { fail_next = true; assert(!__wrap_malloc(100)); });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
    assert(Measure([] {}).memory_profile_status == MEMORY_PROFILE_INVALID);
  } else if (std::strcmp(test, "realloc_failure") == 0) {
    auto ret = Measure([] {
      void *p = __wrap_malloc(100);
      fail_next = true;
      assert(!__wrap_realloc(p, 200));
      std::memset(p, 1, 100); // failed realloc preserves the original block
      __wrap_free(p);
    });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
    assert(ret.algorithm_heap_peak_bytes == 100);
  } else if (std::strcmp(test, "overflow") == 0) {
    auto ret = Measure([] { assert(!__wrap_calloc(SIZE_MAX, 2)); });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
  } else if (std::strcmp(test, "capacity") == 0) {
    auto ret = Measure([] {
      void *blocks[8193];
      for (auto &p : blocks) p = __wrap_malloc(1);
      for (auto p : blocks) __wrap_free(p);
    });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
  } else if (std::strcmp(test, "counter_overflow") == 0) {
    auto ret = Measure([] {
      tiny_next = true; // model a successful enormous allocation without RAM
      void *p = __wrap_malloc(SIZE_MAX - 64), *q = __wrap_malloc(128);
      __wrap_free(p); __wrap_free(q);
    });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
  } else if (std::strcmp(test, "alignment_failure") == 0) {
    auto ret = Measure([] {
      void *p = reinterpret_cast<void *>(123);
      assert(__wrap_posix_memalign(&p, 3, 100) == EINVAL);
      assert(p == reinterpret_cast<void *>(123));
    });
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
  } else assert(false);
}
