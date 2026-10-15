#ifndef SUPPLE_MEMORY_PROFILE_HPP
#define SUPPLE_MEMORY_PROFILE_HPP

#include "../Globals.hpp"
#include <cstddef>

namespace memory_profile {

#ifdef SUPPLE_MEMORY_TRACKING
bool Begin();
void BeginOffline(enc_ret *ret);
void EndOffline(enc_ret *ret);
void BeginOnline(enc_ret *ret);
void EndOnline(enc_ret *ret);
void Finish(enc_ret *ret, bool completed);
void Fail();
#else
inline bool Begin() { return false; }
inline void BeginOffline(enc_ret *) {}
inline void EndOffline(enc_ret *) {}
inline void BeginOnline(enc_ret *) {}
inline void EndOnline(enc_ret *) {}
inline void Finish(enc_ret *, bool) {}
inline void Fail() {}
#endif

// Declare before every algorithm-owned allocation: destruction runs after
// local containers, so cleanup and exception unwinding remain in the scope.
class Scope {
 public:
  explicit Scope(enc_ret *ret) : ret_(ret), started_(false), completed_(false) {
    if (ret_ == nullptr || !ret_->collect_memory_profile) return;
    ret_->algorithm_heap_peak_bytes = 0;
    ret_->memory_profile_status = MEMORY_PROFILE_INVALID;
    ret_->control_heap_peak_bytes = 0;
    ret_->control_memory_profile_status = MEMORY_PROFILE_UNAVAILABLE;
    ret_->online_heap_peak_bytes = 0;
    ret_->online_memory_profile_status = MEMORY_PROFILE_UNAVAILABLE;
    started_ = Begin();
  }
  ~Scope() {
    if (started_) Finish(ret_, completed_);
  }
  void Complete() { completed_ = true; }
  void BeginOffline() { if (started_) memory_profile::BeginOffline(ret_); }
  void EndOffline() { if (started_) memory_profile::EndOffline(ret_); }
  void BeginOnline() { if (started_) memory_profile::BeginOnline(ret_); }
  void EndOnline() { if (started_) memory_profile::EndOnline(ret_); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

 private:
  enc_ret *ret_;
  bool started_;
  bool completed_;
};

} // namespace memory_profile
#endif
