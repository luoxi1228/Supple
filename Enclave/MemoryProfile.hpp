#ifndef SUPPLE_MEMORY_PROFILE_HPP
#define SUPPLE_MEMORY_PROFILE_HPP

#include "../Globals.hpp"
#include <cstddef>

namespace memory_profile {

#ifdef SUPPLE_MEMORY_TRACKING
bool Begin();
void Finish(enc_ret *ret, bool completed);
void Fail();
#else
inline bool Begin() { return false; }
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
    started_ = Begin();
  }
  ~Scope() {
    if (started_) Finish(ret_, completed_);
  }
  void Complete() { completed_ = true; }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

 private:
  enc_ret *ret_;
  bool started_;
  bool completed_;
};

} // namespace memory_profile
#endif
