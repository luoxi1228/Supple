#pragma once
// Host regression test of the production serial FFOS_C sources and TightCompact.
// SGX encryption/RNG are replaced by deterministic host adapters. Shuffle is a
// reversing spy: these tests verify membership and invocation, not randomness.
#define BEFTS_MODE
#define FOAV_ENABLE 0
#define TC_PRECOMPUTE_COUNTS 1
#define TC_OPT_SWAP_FLAG 1
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <thread>
#include <array>
#include "../../Untrusted/CpuBudget.hpp"
#include "../../Globals.hpp"
#include "../../Enclave/foav.h"
#include "../../Enclave/oasm_lib.h"

static thread_local std::mt19937 rng(20260921);
struct PRB_buffer
{
  void getRandomBytes(unsigned char *dst, size_t bytes)
  {
    for (size_t i = 0; i < bytes; i += sizeof(uint32_t))
    {
      const uint32_t value = rng();
      std::memcpy(dst + i, &value, std::min(sizeof(value), bytes - i));
    }
  }
};
static std::vector<PRB_buffer> pool(1);
static PRB_buffer *PRB_pool = pool.data();
static void PRB_pool_init(int threads) { pool.resize(std::max(1, threads)); PRB_pool = pool.data(); }
static void PRB_pool_shutdown() {}
static void ocall_clock(long *t) { *t = 0; }
static size_t decryptBuffer(unsigned char *src, uint64_t n, size_t width,
                            unsigned char **dst)
{
  *dst = static_cast<unsigned char *>(std::malloc(n * width));
  std::memcpy(*dst, src, n * width);
  return width;
}
static size_t encryptBuffer(unsigned char *src, uint64_t n, size_t width,
                            unsigned char *dst)
{
  std::memcpy(dst, src, n * width);
  return width;
}
static uint64_t pow2_lt(uint64_t n)
{
  uint64_t power = 1;
  while (power <= (n - 1) / 2) power *= 2;
  return power;
}
// Exercise the production pthread pool, not a spawn-per-job replacement.
static std::atomic<size_t> dispatched{0};
static int fail_worker_create_after = -1;
static int HostWorkerCreate(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*start)(void *), void *arg) {
  if (fail_worker_create_after == 0) return EAGAIN;
  if (fail_worker_create_after > 0) --fail_worker_create_after;
  return pthread_create(thread, attr, start, arg);
}
#define SUPPLE_THREADPOOL_CREATE HostWorkerCreate
#define SUPPLE_THREADPOOL_DISPATCH_HOOK() (++dispatched)
#include "../../Enclave/ThreadPool.cpp"
#undef SUPPLE_THREADPOOL_DISPATCH_HOOK
#undef SUPPLE_THREADPOOL_CREATE
static size_t host_cpu_budget = 0;
static void ocall_ffos_fr_cpu_budget(size_t *budget) {
  *budget = host_cpu_budget ? host_cpu_budget : supple_cpu::Available();
}
static unsigned long printf_with_rtclock(const char *, ...) { return 0; }
static unsigned long printf_with_rtclock_diff(unsigned long, const char *, ...) { return 0; }

#include "../../Enclave/ORCompaction/TightCompaction_v2.hpp"
#define TightCompact_v2 HostTightCompact
#define TightCompact_v2_parallel HostTightCompactParallel
#include "../../Enclave/ObliviousPrimitives.cpp"
#undef TightCompact_v2
#undef TightCompact_v2_parallel

static std::atomic<bool> throw_compact_on_worker{false};
static std::atomic<size_t> compact_calls{0}, compact_items{0}, shuffle_calls{0};
void TightCompact_v2(unsigned char *data, size_t n, size_t width, bool *flags)
{
  if (throw_compact_on_worker && g_thread_id != 0) throw std::runtime_error("Injected Compact worker exception");
  ++compact_calls;
  compact_items += n;
  HostTightCompact(data, n, width, flags);
}
void RecursiveShuffle_M2(unsigned char *data, uint64_t n, size_t width)
{
  ++shuffle_calls;
  for (size_t i = 0; i < n / 2; ++i)
    for (size_t b = 0; b < width; ++b)
      std::swap(data[i * width + b], data[(n - 1 - i) * width + b]);
}
