#ifndef SUPPLE_OFFLINE_PROFILE_HPP
#define SUPPLE_OFFLINE_PROFILE_HPP

#include "../../Globals.hpp"
#include <cstddef>

#ifndef BEFTS_MODE
// The SGX trusted C runtime updates this on every heap growth (sbrk).
extern "C" size_t g_peak_heap_used;
#endif

namespace offline_profile
{

inline void Reset(enc_ret *ret)
{
  ret->offline_mark_ms = ret->offline_count_ms = 0.0;
  ret->offline_swo_write_ms = ret->offline_tags_ms = 0.0;
  ret->offline_normalize_ms = ret->offline_ofr_write_ms = 0.0;
  ret->offline_replay_ms = ret->offline_project_ms = 0.0;
  ret->offline_prepare_ms = 0.0;
  ret->offline_heap_peak_bytes = ret->total_heap_peak_bytes = 0;
}

inline size_t HeapPeakBytes()
{
#ifdef BEFTS_MODE
  return 0;
#else
  return g_peak_heap_used;
#endif
}

} // namespace offline_profile

#endif
