#include "ThreadPool.hpp"
#include <pthread.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#ifndef BEFTS_MODE
#include "../CONFIG.h"
#endif
#if defined(USE_PRB) && !defined(BEFTS_MODE)
extern thread_local uint64_t PRB_rand_bits;
extern thread_local uint32_t PRB_rand_bits_remaining;
#endif
#ifdef COUNT_OSWAPS
extern thread_local uint64_t OSWAP_COUNTER;
#endif

thread_local threadid_t g_thread_id = 0;
namespace {
enum State { Starting, Idle, Pending, Working, Stopping };
struct Worker {
  pthread_t thread{};
  pthread_mutex_t mutex;
  pthread_cond_t work, done;
  State state = Starting;
  bool launched = false;
  bool mutex_ready = false, work_ready = false, done_ready = false;
  size_t id = 0;
  void *(*function)(void *) = nullptr;
  void *argument = nullptr, *result = nullptr;
  bool error = false;
  char message[256] = {};
  uint64_t switches = 0;
};
Worker *workers = nullptr;
size_t count = 0;
void *Loop(void *argument) {
  Worker &w = *static_cast<Worker *>(argument);
  g_thread_id = w.id;
#if defined(USE_PRB) && !defined(BEFTS_MODE)
  PRB_rand_bits = 0;
  PRB_rand_bits_remaining = 0;
#endif
  pthread_mutex_lock(&w.mutex);
  w.state = Idle;
  pthread_cond_signal(&w.done);
  for (;;) {
    while (w.state == Idle) pthread_cond_wait(&w.work, &w.mutex);
    if (w.state == Stopping) break;
    w.state = Working;
    pthread_mutex_unlock(&w.mutex);
    void *result = nullptr;
    bool error = false;
    char message[256] = {};
#ifdef COUNT_OSWAPS
    OSWAP_COUNTER = 0;
#endif
    try { result = w.function(w.argument); }
    catch (const std::exception &exception) {
      error = true;
      std::strncpy(message, exception.what(), sizeof(message) - 1);
    }
    catch (...) { error = true; std::memcpy(message, "Unknown FFOS_FR worker exception", sizeof("Unknown FFOS_FR worker exception")); }
    pthread_mutex_lock(&w.mutex);
    w.result = result;
    w.error = error;
    std::memcpy(w.message, message, sizeof(message));
#ifdef COUNT_OSWAPS
    w.switches = OSWAP_COUNTER;
    OSWAP_COUNTER = 0;
#endif
    w.state = Idle;
    pthread_cond_signal(&w.done);
  }
  pthread_mutex_unlock(&w.mutex);
  return nullptr;
}
Worker &Get(size_t id) {
  if (id == 0 || id >= count) throw std::out_of_range("Thread pool worker ID");
  return workers[id - 1];
}
void *RunFunction(void *argument) {
  (*static_cast<const std::function<void()> *>(argument))();
  return nullptr;
}
}

int threadpool_init(size_t threads) {
  if (threads == 0 || count != 0) return -1;
  g_thread_id = 0;
#if defined(USE_PRB) && !defined(BEFTS_MODE)
  PRB_rand_bits = 0;
  PRB_rand_bits_remaining = 0;
#endif
  if (threads == 1) { count = 1; return 0; }
  try { workers = new Worker[threads - 1]; }
  catch (...) { return -1; }
  count = threads;
  // Initialize every synchronization object before starting any worker, so
  // partial creation failure follows the same complete cleanup path.
  for (size_t i = 1; i < threads; ++i) {
    Worker &w = workers[i - 1];
    w.id = i;
    w.mutex_ready = pthread_mutex_init(&w.mutex, nullptr) == 0;
    if (w.mutex_ready) w.work_ready = pthread_cond_init(&w.work, nullptr) == 0;
    if (w.work_ready) w.done_ready = pthread_cond_init(&w.done, nullptr) == 0;
    if (!w.done_ready) { threadpool_shutdown(); return -1; }
  }
  for (size_t i = 1; i < threads; ++i) {
    Worker &w = workers[i - 1];
#ifdef SUPPLE_THREADPOOL_CREATE
    const int created = SUPPLE_THREADPOOL_CREATE(&w.thread, nullptr, Loop, &w);
#else
    const int created = pthread_create(&w.thread, nullptr, Loop, &w);
#endif
    if (created != 0) {
      threadpool_shutdown();
      return -1;
    }
    w.launched = true;
    pthread_mutex_lock(&w.mutex);
    while (w.state == Starting) pthread_cond_wait(&w.done, &w.mutex);
    pthread_mutex_unlock(&w.mutex);
  }
  return 0;
}
void threadpool_shutdown() {
  for (size_t i = 1; i < count; ++i) {
    Worker &w = workers[i - 1];
    if (w.launched) {
      pthread_mutex_lock(&w.mutex);
      while (w.state == Starting || w.state == Pending || w.state == Working)
        pthread_cond_wait(&w.done, &w.mutex);
      w.state = Stopping;
      pthread_cond_signal(&w.work);
      pthread_mutex_unlock(&w.mutex);
      pthread_join(w.thread, nullptr);
    }
    if (w.work_ready) pthread_cond_destroy(&w.work);
    if (w.done_ready) pthread_cond_destroy(&w.done);
    if (w.mutex_ready) pthread_mutex_destroy(&w.mutex);
  }
  delete[] workers;
  workers = nullptr;
  count = 0;
}
void threadpool_dispatch(size_t id, void *(*function)(void *), void *data) {
  Worker &w = Get(id);
#ifdef SUPPLE_THREADPOOL_DISPATCH_HOOK
  SUPPLE_THREADPOOL_DISPATCH_HOOK();
#endif
  pthread_mutex_lock(&w.mutex);
  if (w.state != Idle) {
    pthread_mutex_unlock(&w.mutex);
    throw std::logic_error("Thread pool worker is busy");
  }
  w.function = function;
  w.argument = data;
  w.error = false;
  w.state = Pending;
  pthread_cond_signal(&w.work);
  pthread_mutex_unlock(&w.mutex);
}
void threadpool_join(size_t id, void **result) {
  Worker &w = Get(id);
  pthread_mutex_lock(&w.mutex);
  while (w.state == Pending || w.state == Working)
    pthread_cond_wait(&w.done, &w.mutex);
  if (result) *result = w.result;
  const bool error = w.error;
  char message[sizeof(w.message)];
  std::memcpy(message, w.message, sizeof(message));
  w.error = false;
#ifdef COUNT_OSWAPS
  OSWAP_COUNTER += w.switches;
#endif
  w.switches = 0;
  pthread_mutex_unlock(&w.mutex);
  if (error) throw std::runtime_error(message);
}
namespace supple_parallel {
void Fork(size_t id, const std::function<void()> &left,
          const std::function<void()> &right) {
  threadpool_dispatch(id, RunFunction, const_cast<std::function<void()> *>(&right));
  try { left(); }
  catch (...) {
    // SGX's libc++ has no exception_ptr. Preserve the parent exception while
    // draining the child; worker failures are relayed as runtime_error.
    try { threadpool_join(id, nullptr); } catch (...) {}
    throw;
  }
  threadpool_join(id, nullptr);
}
Pool::Pool(size_t threads) : active_(false) {
  if (threadpool_init(threads) != 0)
    throw std::runtime_error("Cannot create FFOS_FR worker pool; check TCS capacity");
  active_ = true;
}
Pool::~Pool() { Close(); }
void Pool::Close() { if (active_) { threadpool_shutdown(); active_ = false; } }
}
