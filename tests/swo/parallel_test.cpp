#include "host_support.hpp"
#include "../../Enclave/SubSample_v2_Par/helper.cpp"
#include "../../Enclave/SubSample_v2_Par/SuppleSWO_parallel.cpp"

using namespace swo_parallel;
using namespace swo_parallel::detail;

static uint64_t digest = 1469598103934665603ULL;
static void hash_bytes(const unsigned char *data, size_t size)
{
  for (size_t i = 0; i < size; ++i)
    digest = (digest ^ data[i]) * 1099511628211ULL;
}

static void verify_parallel(size_t n, size_t m, size_t k, size_t width,
                            size_t threads)
{
  std::mt19937 fixture(7193);
  const size_t words = MarkWords(k);
  std::vector<size_t> M(n * words, 0);
  std::vector<std::vector<uint32_t>> expected(k);
  for (size_t j = 0; j < k; ++j)
  {
    std::vector<uint32_t> ids(n);
    for (size_t i = 0; i < n; ++i) ids[i] = i;
    std::shuffle(ids.begin(), ids.end(), fixture);
    ids.resize(m);
    std::sort(ids.begin(), ids.end());
    expected[j] = ids;
    for (uint32_t id : ids)
      M[id * words + j / SwoWordBits()] |= size_t(1) << (j % SwoWordBits());
  }
  const auto saved = M;
  const auto F = m * k > n ? BuildFrontier(0, k, n, m) : std::vector<FrontierNode>{};
  const size_t bits = CountControlBits(F, n, m, k);
  const auto controls = CONTROLBITS_PARALLEL(M, F, n, m, k, threads);
  assert(controls.size() == (bits + 7) / 8 && M == saved);
  // The existing bit layout must agree for one and multiple worker threads.
  const auto serial = CONTROLBITS_PARALLEL(M, F, n, m, k, 1);
  assert(controls == serial);
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    std::fill(data.begin() + i * width, data.begin() + (i + 1) * width,
              static_cast<unsigned char>(i * 19));
    const uint32_t id = i;
    std::memcpy(data.data() + i * width, &id, sizeof(id));
  }
  const auto original = data;
  std::vector<unsigned char> result(k * m * width);
  const auto read = CONTROLREAD_PARALLEL(data.data(), controls, F, n, m, k,
      width, result.data(), k * m, 0, threads);
  assert(read.next_pos == bits && read.written_blocks == k * m);
  for (size_t j = 0; j < k; ++j)
  {
    std::vector<uint32_t> actual;
    for (size_t i = 0; i < m; ++i)
    {
      const auto row = result.data() + (j * m + i) * width;
      uint32_t id;
      std::memcpy(&id, row, sizeof(id));
      assert(id < n && std::memcmp(row, original.data() + id * width, width) == 0);
      actual.push_back(id);
    }
    std::sort(actual.begin(), actual.end());
    assert(actual == expected[j]);
  }
  hash_bytes(controls.data(), controls.size());
  hash_bytes(result.data(), result.size());
}

int main()
{
  for (size_t threads : {size_t(1), size_t(2), size_t(4)})
  {
    // Small-node fallback, equality Frontier, wide membership, and byte tails.
    verify_parallel(9, 2, 3, 24, threads);
    verify_parallel(8, 2, 8, 8, threads);
    verify_parallel(65, 8, 129, 16, threads);
    // Reach the actual 4096-item parallel threshold and multi-node Frontier.
    verify_parallel(4096, 1024, 4, 8, threads);
    verify_parallel(4096, 1024, 16, 24, threads);
    verify_parallel(4097, 512, 9, 12, threads);
  }
  assert(dispatched > 0);
  // Recorded from the same production implementation before the directory split.
  assert(digest == 0xa994cd2e903d06c2ULL);
  std::printf("PASS: 18 parallel/fallback cases; digest=%016llx; dispatched=%zu\n",
              static_cast<unsigned long long>(digest), dispatched.load());
}
