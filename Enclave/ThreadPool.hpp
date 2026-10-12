#ifndef SUPPLE_THREAD_POOL_HPP
#define SUPPLE_THREAD_POOL_HPP
#include <cstddef>
#include <functional>
using threadid_t = size_t;
extern thread_local threadid_t g_thread_id;
int threadpool_init(threadid_t numthreads);
void threadpool_shutdown();
void threadpool_dispatch(threadid_t id, void *(*function)(void *), void *data);
void threadpool_join(threadid_t id, void **result);

namespace supple_parallel {
// A synchronous fork always joins its child, including when either branch
// throws. Thread ranges belong exclusively to their parent until join.
void Fork(size_t right_id, const std::function<void()> &left,
          const std::function<void()> &right);
class Pool {
 public:
  explicit Pool(size_t threads);
  ~Pool();
  void Close();
  Pool(const Pool &) = delete;
  Pool &operator=(const Pool &) = delete;
 private:
  bool active_;
};
}
#endif
