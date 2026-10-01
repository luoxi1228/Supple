#include "MemoryProfile.hpp"

#ifdef SUPPLE_MEMORY_TRACKING
#include <cstdint>
#include <limits>

extern "C" {
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__real_memalign(size_t, size_t);
int __real_posix_memalign(void **, size_t, size_t);
void *__real_aligned_alloc(size_t, size_t);
}

namespace {
constexpr size_t kSlots = 16384;
constexpr size_t kMask = kSlots - 1;
struct Entry { void *pointer; size_t bytes; };
// BSS storage: no metadata heap allocation, no changes to user allocations.
// The four supported experiment modes execute serially on one enclave thread.
struct Tracker {
  Entry entries[kSlots];
  size_t current, peak, count;
  bool active, broken;

  static size_t Hash(void *pointer) {
    uint64_t key = reinterpret_cast<uintptr_t>(pointer) >> 4;
    key ^= key >> 33;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33;
    return static_cast<size_t>(key) & kMask;
  }
  size_t Find(void *pointer) const {
    size_t slot = Hash(pointer);
    while (entries[slot].pointer && entries[slot].pointer != pointer)
      slot = (slot + 1) & kMask;
    return slot;
  }
  void Observe(size_t bytes) {
    if (bytes > std::numeric_limits<size_t>::max() - current) {
      broken = true;
      return;
    }
    const size_t total = current + bytes;
    if (total > peak) peak = total;
  }
  void Add(void *pointer, size_t bytes) {
    if (!pointer) { broken = true; return; }
    // Bound probing cost and always leave empty slots. Never silently drop an
    // allocation and present an underestimated peak as a valid measurement.
    if (count >= kSlots / 2 ||
        bytes > std::numeric_limits<size_t>::max() - current) {
      broken = true;
      return;
    }
    const size_t slot = Find(pointer);
    if (entries[slot].pointer) { broken = true; return; }
    entries[slot] = {pointer, bytes};
    ++count;
    current += bytes;
    if (current > peak) peak = current;
  }
  void Remove(void *pointer) {
    if (!pointer) return;
    size_t hole = Find(pointer);
    if (!entries[hole].pointer) return; // allocated outside an algorithm scope
    current -= entries[hole].bytes;
    --count;
    // Backward-shift deletion prevents tombstones accumulating across millions
    // of SDK allocation/free pairs and across repeated experiment rounds.
    size_t next = (hole + 1) & kMask;
    while (entries[next].pointer) {
      const size_t home = Hash(entries[next].pointer);
      if (((next - home) & kMask) >= ((next - hole) & kMask)) {
        entries[hole] = entries[next];
        hole = next;
      }
      next = (next + 1) & kMask;
    }
    entries[hole] = {nullptr, 0};
  }
};
Tracker tracker{};
// These modes use one serial ECALL and no worker threads. A global scope
// pointer avoids a simulator TLS lookup for every allocation/free event.
// It is also suspended while the allocator runs, excluding allocator-internal
// bookkeeping and preventing nested API calls from being counted twice.
Tracker *owner = nullptr;
struct AllocatorCall {
  Tracker **slot;
  Tracker *state;
  AllocatorCall(Tracker **slot, Tracker *state) : slot(slot), state(state) {
    *slot = nullptr;
  }
  ~AllocatorCall() { *slot = state; }
};

void Record(Tracker *state, void *pointer, size_t bytes) {
  if (!pointer && bytes == 0) return; // a permitted zero-sized allocation
  if (state->active) state->Add(pointer, bytes);
}
} // namespace

namespace memory_profile {
bool Begin() {
  if (tracker.active) { tracker.broken = true; return false; }
  owner = &tracker;
  tracker.peak = tracker.current; // persistent scratch is part of this round
  tracker.active = true;
  return true;
}
void Fail() {
  if (owner && owner->active) owner->broken = true;
}
void Finish(enc_ret *ret, bool completed) {
  ret->algorithm_heap_peak_bytes = tracker.peak;
  ret->memory_profile_status = completed && !tracker.broken
      ? MEMORY_PROFILE_VALID : MEMORY_PROFILE_INVALID;
  tracker.active = false;
  // Keep ownership and live allocations across rounds, including frees that
  // happen between scopes. A broken ledger stays invalid for this enclave.
}
} // namespace memory_profile

extern "C" void *__wrap_malloc(size_t bytes) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) return __real_malloc(bytes);
  AllocatorCall call(slot, state);
  void *result = __real_malloc(bytes);
  Record(state, result, bytes);
  return result;
}
extern "C" void *__wrap_calloc(size_t count, size_t bytes) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) return __real_calloc(count, bytes);
  AllocatorCall call(slot, state);
  void *result = __real_calloc(count, bytes);
  if (state->active) {
    if (bytes && count > std::numeric_limits<size_t>::max() / bytes)
      state->broken = true;
    else Record(state, result, count * bytes);
  }
  return result;
}
extern "C" void __wrap_free(void *pointer) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) { __real_free(pointer); return; }
  AllocatorCall call(slot, state);
  state->Remove(pointer);
  __real_free(pointer);
}
extern "C" void *__wrap_realloc(void *pointer, size_t bytes) {
  Tracker **owner_slot = &owner;
  Tracker *state = *owner_slot;
  if (!state) return __real_realloc(pointer, bytes);
  AllocatorCall call(owner_slot, state);
  const size_t slot = pointer ? state->Find(pointer) : 0;
  const bool owned = pointer && state->entries[slot].pointer;
  void *result = __real_realloc(pointer, bytes);
  if (!result) {
    // SGX dlrealloc and the host test allocator free a non-null block for n=0.
    if (pointer && bytes == 0) state->Remove(pointer);
    else if (state->active) state->broken = true;
    return result;
  }
  if (owned && result != pointer && state->active) {
    // dlrealloc allocates the destination before releasing the old block.
    // Include that temporary overlap, which otherwise vanishes at return.
    state->Observe(bytes);
  }
  if (owned) state->Remove(pointer);
  if (state->active || owned) state->Add(result, bytes);
  return result;
}
extern "C" void *__wrap_memalign(size_t alignment, size_t bytes) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) return __real_memalign(alignment, bytes);
  AllocatorCall call(slot, state);
  void *result = __real_memalign(alignment, bytes);
  Record(state, result, bytes);
  return result;
}
extern "C" int __wrap_posix_memalign(void **pointer, size_t alignment, size_t bytes) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) return __real_posix_memalign(pointer, alignment, bytes);
  AllocatorCall call(slot, state);
  const int status = __real_posix_memalign(pointer, alignment, bytes);
  if (status == 0) Record(state, *pointer, bytes);
  else if (state->active) state->broken = true;
  return status;
}
extern "C" void *__wrap_aligned_alloc(size_t alignment, size_t bytes) {
  Tracker **slot = &owner;
  Tracker *state = *slot;
  if (!state) return __real_aligned_alloc(alignment, bytes);
  AllocatorCall call(slot, state);
  void *result = __real_aligned_alloc(alignment, bytes);
  Record(state, result, bytes);
  return result;
}
#endif
