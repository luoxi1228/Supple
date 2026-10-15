#include "MemoryProfile.hpp"

#ifdef SUPPLE_MEMORY_TRACKING
#include <algorithm>
#include <cstdint>
#include <limits>
#include <pthread.h>

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
struct Entry { void *pointer; size_t bytes; bool offline; };
// BSS storage: no metadata heap allocation, no changes to user allocations.
// All algorithm workers share this ledger under tracker_mutex.
struct Tracker {
  Entry entries[kSlots];
  size_t current, peak, count;
  size_t offline_current, offline_peak, online_peak;
  bool active, broken, offline_active, online_active;

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
  void Observe(size_t bytes, bool offline) {
    if (bytes > std::numeric_limits<size_t>::max() - current) {
      broken = true;
      return;
    }
    const size_t total = current + bytes;
    if (total > peak) peak = total;
    if (online_active) online_peak = std::max(online_peak, total);
    if (offline && offline_active)
      offline_peak = std::max(offline_peak, offline_current + bytes);
  }
  void Add(void *pointer, size_t bytes, bool offline) {
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
    entries[slot] = {pointer, bytes, offline};
    ++count;
    current += bytes;
    if (current > peak) peak = current;
    if (online_active) online_peak = std::max(online_peak, current);
    if (offline) {
      offline_current += bytes;
      if (offline_active) offline_peak = std::max(offline_peak, offline_current);
    }
  }
  void Remove(void *pointer) {
    if (!pointer) return;
    size_t hole = Find(pointer);
    if (!entries[hole].pointer) return; // allocated outside an algorithm scope
    current -= entries[hole].bytes;
    if (entries[hole].offline) offline_current -= entries[hole].bytes;
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
    entries[hole] = {nullptr, 0, false};
  }
};
Tracker tracker{};
// The scope owner is shared; allocator recursion belongs to the current thread.
// Serialize complete wrapped operations so realloc overlap and the live ledger
// have a consistent order across workers. The guard prevents nested locking.
Tracker *const owner = &tracker;
pthread_mutex_t tracker_mutex = PTHREAD_MUTEX_INITIALIZER;
thread_local bool inside_allocator = false;
struct AllocatorCall {
  AllocatorCall() { inside_allocator = true; pthread_mutex_lock(&tracker_mutex); }
  ~AllocatorCall() { pthread_mutex_unlock(&tracker_mutex); inside_allocator = false; }
};

void Record(Tracker *state, void *pointer, size_t bytes) {
  if (!pointer && bytes == 0) return; // a permitted zero-sized allocation
  if (state->active) state->Add(pointer, bytes, state->offline_active);
}
} // namespace

namespace memory_profile {
bool Begin() {
  AllocatorCall call;
  if (tracker.active) { tracker.broken = true; return false; }
  tracker.peak = tracker.current; // persistent scratch is part of this round
  tracker.offline_peak = 0;
  tracker.online_peak = 0;
  tracker.active = true;
  return true;
}
void BeginOffline(enc_ret *ret) {
  AllocatorCall call;
  ret->control_memory_profile_status = MEMORY_PROFILE_INVALID;
  if (!tracker.active || tracker.offline_active || tracker.online_active) {
    tracker.broken = true;
    return;
  }
  // Only allocations tagged as control-generation storage belong here.
  // Do not subtract a baseline: preexisting buffers may be freed mid-phase.
  tracker.offline_peak = tracker.offline_current;
  tracker.offline_active = true;
}
void EndOffline(enc_ret *ret) {
  AllocatorCall call;
  if (!tracker.active || !tracker.offline_active) { tracker.broken = true; return; }
  ret->control_heap_peak_bytes = tracker.offline_peak;
  ret->control_memory_profile_status = tracker.broken
      ? MEMORY_PROFILE_INVALID : MEMORY_PROFILE_VALID;
  tracker.offline_active = false;
}
void BeginOnline(enc_ret *ret) {
  AllocatorCall call;
  ret->online_memory_profile_status = MEMORY_PROFILE_INVALID;
  if (!tracker.active || tracker.offline_active || tracker.online_active) {
    tracker.broken = true;
    return;
  }
  // Payload and the retained controls already live at this boundary count
  // together with output allocation and later data-movement workspaces.
  tracker.online_peak = tracker.current;
  tracker.online_active = true;
}
void EndOnline(enc_ret *ret) {
  AllocatorCall call;
  if (!tracker.active || !tracker.online_active) { tracker.broken = true; return; }
  ret->online_heap_peak_bytes = tracker.online_peak;
  ret->online_memory_profile_status = tracker.broken
      ? MEMORY_PROFILE_INVALID : MEMORY_PROFILE_VALID;
  tracker.online_active = false;
}
void Fail() {
  AllocatorCall call;
  if (owner->active) owner->broken = true;
}
void Finish(enc_ret *ret, bool completed) {
  AllocatorCall call;
  ret->algorithm_heap_peak_bytes = tracker.peak;
  ret->memory_profile_status = completed && !tracker.broken &&
      !tracker.offline_active && !tracker.online_active
      ? MEMORY_PROFILE_VALID : MEMORY_PROFILE_INVALID;
  if (ret->memory_profile_status != MEMORY_PROFILE_VALID &&
      ret->control_memory_profile_status != MEMORY_PROFILE_UNAVAILABLE)
    ret->control_memory_profile_status = MEMORY_PROFILE_INVALID;
  if (ret->memory_profile_status != MEMORY_PROFILE_VALID &&
      ret->online_memory_profile_status != MEMORY_PROFILE_UNAVAILABLE)
    ret->online_memory_profile_status = MEMORY_PROFILE_INVALID;
  tracker.active = false;
  tracker.offline_active = false;
  tracker.online_active = false;
  // Keep ownership and live allocations across rounds, including frees that
  // happen between scopes. A broken ledger stays invalid for this enclave.
}
} // namespace memory_profile

extern "C" void *__wrap_malloc(size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_malloc(bytes);
  AllocatorCall call;
  void *result = __real_malloc(bytes);
  Record(state, result, bytes);
  return result;
}
extern "C" void *__wrap_calloc(size_t count, size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_calloc(count, bytes);
  AllocatorCall call;
  void *result = __real_calloc(count, bytes);
  if (state->active) {
    if (bytes && count > std::numeric_limits<size_t>::max() / bytes)
      state->broken = true;
    else Record(state, result, count * bytes);
  }
  return result;
}
extern "C" void __wrap_free(void *pointer) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) { __real_free(pointer); return; }
  AllocatorCall call;
  state->Remove(pointer);
  __real_free(pointer);
}
extern "C" void *__wrap_realloc(void *pointer, size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_realloc(pointer, bytes);
  AllocatorCall call;
  const size_t slot = pointer ? state->Find(pointer) : 0;
  const bool owned = pointer && state->entries[slot].pointer;
  const bool offline = state->offline_active || (owned && state->entries[slot].offline);
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
    state->Observe(bytes, offline);
  }
  if (owned) state->Remove(pointer);
  if (state->active || owned) state->Add(result, bytes, offline);
  return result;
}
extern "C" void *__wrap_memalign(size_t alignment, size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_memalign(alignment, bytes);
  AllocatorCall call;
  void *result = __real_memalign(alignment, bytes);
  Record(state, result, bytes);
  return result;
}
extern "C" int __wrap_posix_memalign(void **pointer, size_t alignment, size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_posix_memalign(pointer, alignment, bytes);
  AllocatorCall call;
  const int status = __real_posix_memalign(pointer, alignment, bytes);
  if (status == 0) Record(state, *pointer, bytes);
  else if (state->active) state->broken = true;
  return status;
}
extern "C" void *__wrap_aligned_alloc(size_t alignment, size_t bytes) {
  Tracker *state = inside_allocator ? nullptr : owner;
  if (!state) return __real_aligned_alloc(alignment, bytes);
  AllocatorCall call;
  void *result = __real_aligned_alloc(alignment, bytes);
  Record(state, result, bytes);
  return result;
}
#endif
