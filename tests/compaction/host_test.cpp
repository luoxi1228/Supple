#define COUNT_OSWAPS
#define ocall_clock UnusedHostClock
#define decryptBuffer UnusedHostDecrypt
#define encryptBuffer UnusedHostEncrypt
#define PRB_pool_init UnusedHostPoolInit
#define PRB_pool_shutdown UnusedHostPoolShutdown
#include "../ffos_c/host_support.hpp"
#undef ocall_clock
#undef decryptBuffer
#undef encryptBuffer
#undef PRB_pool_init
#undef PRB_pool_shutdown
#include <sgx_tcrypto.h>
#include "mark_gen.hpp"

thread_local uint64_t OSWAP_COUNTER = 0;
static size_t clock_calls = 0, encrypted_calls = 0, pool_depth = 0;
static int fail_array_after = -1;
static size_t live_arrays = 0;
static bool bad_decrypt = false, bad_encrypt = false, bad_pool = false;
static bool observe = false;
static unsigned char *current_data = nullptr;
static size_t current_width = 0;
static bool *first_flags = nullptr;
static std::vector<std::vector<uint32_t>> expected;

// Inject failure into the flag allocation or real compaction prefix counts.
void *operator new[](size_t bytes) {
  if (fail_array_after == 0) {
    fail_array_after = -1;
    throw std::bad_alloc();
  }
  if (fail_array_after > 0) --fail_array_after;
  void *p = std::malloc(bytes ? bytes : 1);
  if (!p) throw std::bad_alloc();
  ++live_arrays;
  return p;
}
void operator delete[](void *p) noexcept {
  if (p) { assert(live_arrays != 0); --live_arrays; }
  std::free(p);
}

static void ocall_clock(long *ticks) { *ticks = 1000 * ++clock_calls; }
static void PRB_pool_init(int) {
  if (bad_pool) throw std::bad_alloc();
  ++pool_depth;
}
static void PRB_pool_shutdown() { assert(pool_depth == 1); --pool_depth; }
static size_t decryptBuffer(unsigned char *src, uint64_t n, size_t width,
                             unsigned char **dst) {
  width -= 28;
  *dst = static_cast<unsigned char *>(std::malloc(n * width));
  assert(*dst);
  std::memcpy(*dst, src, n * width);
  return bad_decrypt ? SIZE_MAX : width;
}
static size_t encryptBuffer(unsigned char *src, uint64_t n, size_t width,
                             unsigned char *dst) {
  ++encrypted_calls;
  if (bad_encrypt) return SIZE_MAX;
  std::memcpy(dst, src, n * width);
  return width + 28;
}

void markGen(size_t n, size_t m, bool *flags) {
  ProductionMarkGen(n, m, flags);
  size_t count = 0;
  for (size_t i = 0; i < n; ++i) count += flags[i];
  assert(count == m);
  if (!observe) return;
  if (!first_flags) first_flags = flags;
  assert(flags == first_flags); // Reuse one allocation throughout K rounds.
  std::vector<uint32_t> ids;
  for (size_t i = 0; i < n; ++i) {
    if (!flags[i]) continue;
    uint32_t id;
    std::memcpy(&id, current_data + i * current_width, sizeof(id));
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  expected.push_back(ids);
}

#include "../../Enclave/Baseline/Compaction_based/CompactionBasedSWO.cpp"

static std::vector<unsigned char> Records(size_t n, size_t width) {
  std::vector<unsigned char> records(n * width);
  for (size_t i = 0; i < n; ++i) {
    for (size_t b = 0; b < width; ++b)
      records[i * width + b] = static_cast<unsigned char>(i * 31 + b);
    const uint32_t id = i;
    std::memcpy(records.data() + i * width, &id, sizeof(id));
  }
  return records;
}

static void Verify(size_t n, size_t m, size_t k, size_t width) {
  auto data = Records(n, width);
  const auto original = data;
  std::vector<unsigned char> output(m * k * width);
  current_data = data.data();
  current_width = width;
  first_flags = nullptr;
  expected.clear();
  observe = true;
  clock_calls = compact_calls = shuffle_calls = 0;
  OSWAP_COUNTER = 7;
  enc_ret ret{};
  assert(CompactionBasedSWO(data.data(), n, m, k, width, output.data(), &ret));
  observe = false;
  assert(clock_calls == 2 * k + 1 && compact_calls == k && shuffle_calls == 0);
  assert(ret.gen_perm_time == k && ret.apply_perm_time == k && ret.ptime == 2 * k);
  assert(ret.OSWAP_count == OSWAP_COUNTER - 7);
  for (size_t sample = 0; sample < k; ++sample) {
    std::vector<uint32_t> ids;
    for (size_t i = 0; i < m; ++i) {
      const auto row = output.data() + (sample * m + i) * width;
      uint32_t id;
      std::memcpy(&id, row, sizeof(id));
      assert(id < n && std::memcmp(row, original.data() + id * width, width) == 0);
      ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    assert(ids == expected[sample]);
    assert(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  }
  std::vector<uint32_t> remaining;
  for (size_t i = 0; i < n; ++i) {
    uint32_t id;
    std::memcpy(&id, data.data() + i * width, sizeof(id));
    assert(id < n && std::memcmp(data.data() + i * width, original.data() + id * width, width) == 0);
    remaining.push_back(id);
  }
  std::sort(remaining.begin(), remaining.end());
  for (size_t i = 0; i < n; ++i) assert(remaining[i] == i);
}

int main() {
  size_t cases = 0;
  for (size_t n : {1, 2, 3, 7, 8, 9, 31, 32, 33, 65, 2049})
    for (size_t m : {size_t(1), std::max(size_t(1), n / 4), n})
      for (size_t k : {1, 2, 4, 8})
        for (size_t width : {4, 8, 12, 16, 24, 32, 40, 64}) {
          Verify(n, m, k, width);
          ++cases;
        }
  auto data = Records(8, 16);
  std::vector<unsigned char> output(4 * 3 * 16);
  enc_ret ret{};
  assert(!CompactionBasedSWO(nullptr, 8, 4, 3, 16, output.data(), &ret));
  assert(!CompactionBasedSWO(data.data(), 8, 4, 3, 16, nullptr, &ret));
  assert(!CompactionBasedSWO(data.data(), 8, 4, 3, 16, output.data(), nullptr));
  for (const auto dims : {std::array<size_t,4>{{0,1,1,16}}, {{8,0,1,16}},
                         {{8,9,1,16}}, {{8,4,0,16}}, {{8,4,1,0}}, {{8,4,1,7}},
                         {{size_t(UINT32_MAX)+1,1,1,16}}, {{8,4,SIZE_MAX,16}},
                         {{8,4,1,SIZE_MAX-7}}})
    assert(!CompactionBasedSWO(data.data(), dims[0], dims[1], dims[2], dims[3], output.data(), &ret));
  assert(!CompactionBasedSWO(data.data(), 1, 1, 1, size_t(UINT32_MAX) + 1, output.data(), &ret));
  for (int allocation : {0, 1, 2, 3}) {
    fail_array_after = allocation;
    assert(!CompactionBasedSWO(data.data(), 8, 4, 3, 16, output.data(), &ret));
    assert(ret.ptime == 0);
    fail_array_after = allocation;
    encrypted_calls = 0;
    DecCompactionBasedSWO(data.data(), 8, 4, 3, 44, output.data(), &ret);
    assert(pool_depth == 0 && encrypted_calls == 0);
  }
  for (int failure = 0; failure < 3; ++failure) {
    bad_decrypt = failure == 0;
    bad_pool = failure == 1;
    bad_encrypt = failure == 2;
    encrypted_calls = 0;
    DecCompactionBasedSWO(data.data(), 8, 4, 3, 44, output.data(), &ret);
    assert(pool_depth == 0 && encrypted_calls == (bad_encrypt ? 1 : 0));
  }
  bad_decrypt = bad_pool = bad_encrypt = false;
  encrypted_calls = 0;
  DecCompactionBasedSWO(data.data(), 8, 4, 3, 44, output.data(), &ret);
  assert(encrypted_calls == 1 && pool_depth == 0);
  assert(live_arrays == 0);
  std::printf("PASS: %zu compaction cases, allocation failures and adapter cleanup\n", cases);
}
