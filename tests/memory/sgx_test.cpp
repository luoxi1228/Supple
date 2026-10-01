// Run against a fresh enclave per case/setting, so disabled profiling cannot
// preallocate untracked scratch for a subsequently enabled measurement.
#include <sgx_urts.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../Untrusted/Enclave_u.h"
#include "../../Application/gcm.h"

struct Enclave {
  sgx_enclave_id_t id = 0;
  unsigned char input_key[16] = {1}, output_key[16] = {2};
  explicit Enclave(const char *path) {
    sgx_launch_token_t token{};
    int updated = 0;
    const auto status = sgx_create_enclave(path, SGX_DEBUG_FLAG, &token, &updated, &id, nullptr);
    if (status != SGX_SUCCESS) std::fprintf(stderr, "SGX creation failed: 0x%x\n", status);
    assert(status == SGX_SUCCESS);
    assert(Enclave_loadTestKeys(id, input_key, output_key) == SGX_SUCCESS);
  }
  ~Enclave() { assert(sgx_destroy_enclave(id) == SGX_SUCCESS); }
};

static double Run(Enclave &enclave, int mode, size_t n, size_t m, size_t k,
                  size_t width, bool tracking, size_t *peak, double *ptime) {
  const size_t encrypted_width = width + 28;
  const size_t output_blocks = mode == 2 ? n : m * k;
  std::vector<unsigned char> input(n * encrypted_width), output(output_blocks * encrypted_width);
  std::vector<unsigned char> record(width);
  for (size_t i = 0; i < n; ++i) {
    std::fill(record.begin(), record.end(), static_cast<unsigned char>(i * 19));
    const uint32_t record_id = i;
    const uint64_t id = i;
    std::memcpy(record.data(), &record_id, sizeof(record_id));
    unsigned char *row = input.data() + i * encrypted_width;
    std::memcpy(row, &id, sizeof(id));
    assert(gcm_encrypt(record.data(), width, nullptr, 0, enclave.input_key, row, 12,
                       row + 12, row + 12 + width) == static_cast<int>(width));
  }
  enc_ret ret{};
  ret.collect_memory_profile = tracking;
  const auto start = std::chrono::steady_clock::now();
  sgx_status_t status = SGX_ERROR_UNEXPECTED;
  switch (mode) {
    case 2: status = DecPSQF_SWO(enclave.id, input.data(), n, m, encrypted_width, output.data(), &ret); break;
    case 4: status = DecSuppleSWO(enclave.id, input.data(), n, m, k, encrypted_width, output.data(), &ret); break;
    case 5: status = DecOFRSupple(enclave.id, input.data(), n, m, k, encrypted_width, output.data(), &ret); break;
    case 1: status = DecShuffleBasedSWO(enclave.id, input.data(), n, m, k, encrypted_width, output.data(), &ret); break;
    case 3: status = DecCompactionBasedSWO(enclave.id, input.data(), n, m, k, encrypted_width, output.data(), &ret); break;
  }
  const double elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  assert(status == SGX_SUCCESS);
  if (tracking) {
    if (ret.memory_profile_status != MEMORY_PROFILE_VALID)
      std::fprintf(stderr, "invalid tracker mode=%d n=%zu m=%zu k=%zu\n", mode, n, m, k);
    assert(ret.memory_profile_status == MEMORY_PROFILE_VALID);
    const size_t floor = n * width + (mode == 2 ? 0 : m * k * width);
    assert(ret.algorithm_heap_peak_bytes >= floor);
  } else assert(ret.memory_profile_status == MEMORY_PROFILE_UNAVAILABLE);
  *peak = ret.algorithm_heap_peak_bytes;
  *ptime = ret.ptime;
  const size_t sample_size = mode == 2 ? (n % m == 0 ? m : n) : m;
  for (size_t start_block = 0; start_block < output_blocks; start_block += sample_size) {
    std::vector<uint64_t> ids;
    for (size_t i = 0; i < sample_size; ++i) {
      unsigned char *row = output.data() + (start_block + i) * encrypted_width;
      assert(gcm_decrypt(row + 12, width, nullptr, 0, row + 12 + width,
                         enclave.output_key, row, 12, record.data()) == static_cast<int>(width));
      uint32_t id = n;
      std::memcpy(&id, record.data(), sizeof(id));
      assert(id < n);
      for (size_t b = sizeof(id); b < width; ++b)
        assert(record[b] == static_cast<unsigned char>(id * 19));
      ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    assert(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  }
  return elapsed;
}

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3 || argc == 4);
  const bool compare = argc == 4 && std::strcmp(argv[2], "--compare") == 0;
  const bool baseline = argc == 3 && std::strcmp(argv[2], "--baseline") == 0;
  const bool bench = compare || baseline || (argc == 3 && std::strcmp(argv[2], "--benchmark") == 0);
  std::setbuf(stdout, nullptr);
  if (bench) {
    std::puts("mode,trial,tracking,round,ecall_ms,ptime_ms,peak_bytes");
    for (int mode : {1, 2, 3, 4, 5}) {
      for (int trial = 0; trial < 5; ++trial) {
        for (int setting = 0; setting < (compare ? 3 : baseline ? 1 : 2); ++setting) {
          // For the three-build comparison, rotate -1=unwrapped, 0=disabled,
          // 1=enabled settings to control drift in CPU/load between trials.
          const int kind = compare ? (setting + trial) % 3 - 1
              : baseline ? -1 : (trial % 2 ? 1 - setting : setting);
          const bool tracking = kind == 1;
          Enclave enclave(compare && kind == -1 ? argv[3] : argv[1]);
          for (int round = 0; round < 7; ++round) {
            size_t peak = 0;
            double ptime = 0;
            const double elapsed = Run(enclave, mode, 16384, 256, 64, 16, tracking, &peak, &ptime);
            if (round >= 2)
              std::printf("%d,%d,%d,%d,%.6f,%.6f,%zu\n", mode, trial, kind, round - 2, elapsed, ptime, peak);
          }
        }
      }
    }
    return 0;
  }
  for (int mode : {1, 2, 3, 4, 5}) {
    for (size_t k : {size_t(1), size_t(2), size_t(4), size_t(8)}) {
      Enclave enclave(argv[1]);
      for (int round = 0; round < 3; ++round) {
        size_t peak = 0;
        double ptime = 0;
        Run(enclave, mode, 64, 16, k, 24, true, &peak, &ptime);
        std::printf("PASS mode=%d n=64 m=16 k=%zu round=%d peak=%zu\n", mode, k, round, peak);
      }
    }
    // Non-power-of-two input and PSQF's n % m != 0 fallback.
    Enclave enclave(argv[1]);
    size_t peak = 0;
    double ptime = 0;
    Run(enclave, mode, 65, 8, 3, 24, true, &peak, &ptime);
    // An invalid request must leave the measurement invalid, even if ECALL
    // itself returned SGX_SUCCESS. Exercise each adapter.
    enc_ret bad{};
    bad.collect_memory_profile = 1;
    unsigned char buffer[64]{};
    switch (mode) {
      case 2: DecPSQF_SWO(enclave.id, buffer, 0, 0, 52, buffer, &bad); break;
      case 4: DecSuppleSWO(enclave.id, buffer, 0, 0, 1, 52, buffer, &bad); break;
      case 5: DecOFRSupple(enclave.id, buffer, 0, 0, 1, 52, buffer, &bad); break;
      case 1: DecShuffleBasedSWO(enclave.id, buffer, 0, 0, 1, 52, buffer, &bad); break;
      case 3: DecCompactionBasedSWO(enclave.id, buffer, 0, 0, 1, 52, buffer, &bad); break;
    }
    assert(bad.memory_profile_status == MEMORY_PROFILE_INVALID);
    bad = enc_ret{};
    bad.collect_memory_profile = 1;
    // A bad AES-GCM tag is an algorithm failure, not a successful measurement.
    switch (mode) {
      case 2: DecPSQF_SWO(enclave.id, buffer, 1, 1, 52, buffer, &bad); break;
      case 4: DecSuppleSWO(enclave.id, buffer, 1, 1, 1, 52, buffer, &bad); break;
      case 5: DecOFRSupple(enclave.id, buffer, 1, 1, 1, 52, buffer, &bad); break;
      case 1: DecShuffleBasedSWO(enclave.id, buffer, 1, 1, 1, 52, buffer, &bad); break;
      case 3: DecCompactionBasedSWO(enclave.id, buffer, 1, 1, 1, 52, buffer, &bad); break;
    }
    assert(bad.memory_profile_status == MEMORY_PROFILE_INVALID);
  }
  // Specialized and generic compaction dispatch widths, tiny inputs and
  // both extreme sample sizes. Use fresh enclaves after failures.
  for (size_t width : {4, 8, 12, 16, 24, 32, 40, 64}) {
    Enclave enclave(argv[1]);
    size_t peak;
    double ptime;
    Run(enclave, 3, 1, 1, 1, width, true, &peak, &ptime);
    Run(enclave, 3, 65, 1, 8, width, true, &peak, &ptime);
    Run(enclave, 3, 65, 65, 2, width, true, &peak, &ptime);
  }
  for (int failure = 0; failure < 4; ++failure) {
    Enclave enclave(argv[1]);
    unsigned char input[44]{}, output[44]{};
    unsigned char record[16]{};
    assert(gcm_encrypt(record, 16, nullptr, 0, enclave.input_key, input, 12,
                       input + 12, input + 28) == 16);
    enc_ret ret{};
    ret.collect_memory_profile = 1;
    const size_t n = failure == 0 ? size_t(UINT32_MAX) + 1 : 1;
    const size_t m = failure == 1 ? 2 : 1;
    const size_t k = failure == 2 ? SIZE_MAX : failure == 3 ? size_t(1) << 28 : 1;
    // Final case requests 4 GiB output in a 64 MiB enclave: allocation fails
    // after valid decryption and must leave the output completely untouched.
    assert(DecCompactionBasedSWO(enclave.id, input, n, m, k, 44, output, &ret) == SGX_SUCCESS);
    assert(ret.memory_profile_status == MEMORY_PROFILE_INVALID);
    for (unsigned char byte : output) assert(byte == 0);
  }
  std::puts("PASS: compaction dispatch widths, edge dimensions and allocation failure");
}
