#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../Untrusted/OLib.hpp"

namespace {

enum PrimitiveKind : uint8_t {
  PRIMITIVE_OSWAP = 0,
  PRIMITIVE_OFORK = 1
};

struct Summary {
  double oswap_0_ns;
  double ofork_01_ns;
  double oswap_1_ns;
  double ofork_10_ns;
  double ofork_00_ns;
  double ofork_11_ns;
};

double mean(const std::vector<double> &values) {
  double total = 0.0;
  for (double value : values) {
    total += value;
  }
  return total / static_cast<double>(values.size());
}

void initialize_input(std::vector<unsigned char> &buffer, size_t block_size) {
  const size_t blocks = buffer.size() / block_size;
  for (size_t block = 0; block < blocks; block++) {
    unsigned char *ptr = buffer.data() + block * block_size;
    for (size_t offset = 0; offset < block_size; offset++) {
      ptr[offset] = static_cast<unsigned char>(
          (block * 131U + offset * 17U + 29U) & 0xffU);
    }
  }
}

double run_once(std::vector<unsigned char> &work,
                const std::vector<unsigned char> &initial,
                size_t pairs,
                size_t block_size,
                PrimitiveKind primitive,
                uint8_t flag0,
                uint8_t flag1) {
  // Restoring the buffer is intentionally outside the enclave-side timer.
  std::copy(initial.begin(), initial.end(), work.begin());

  const double elapsed_us = MeasureObliviousPrimitive(
      work.data(), pairs, block_size,
      static_cast<uint8_t>(primitive), flag0, flag1);

  if (elapsed_us < 0.0) {
    std::fprintf(stderr,
                 "MeasureObliviousPrimitive failed: block_size=%zu primitive=%u\n",
                 block_size, static_cast<unsigned>(primitive));
    std::exit(2);
  }

  return elapsed_us * 1000.0 / static_cast<double>(pairs);
}

void measure_pair(std::vector<unsigned char> &work,
                  const std::vector<unsigned char> &initial,
                  size_t pairs,
                  size_t block_size,
                  size_t repeats,
                  size_t warmups,
                  PrimitiveKind first_primitive,
                  uint8_t first_flag0,
                  uint8_t first_flag1,
                  PrimitiveKind second_primitive,
                  uint8_t second_flag0,
                  uint8_t second_flag1,
                  double *first_mean,
                  double *second_mean) {
  std::vector<double> first_samples;
  std::vector<double> second_samples;
  first_samples.reserve(repeats);
  second_samples.reserve(repeats);

  const size_t total = warmups + repeats;
  for (size_t round = 0; round < total; round++) {
    double first;
    double second;

    // Alternate execution order to reduce cache, frequency and temperature bias.
    if ((round & 1U) == 0U) {
      first = run_once(work, initial, pairs, block_size,
                       first_primitive, first_flag0, first_flag1);
      second = run_once(work, initial, pairs, block_size,
                        second_primitive, second_flag0, second_flag1);
    } else {
      second = run_once(work, initial, pairs, block_size,
                        second_primitive, second_flag0, second_flag1);
      first = run_once(work, initial, pairs, block_size,
                       first_primitive, first_flag0, first_flag1);
    }

    if (round >= warmups) {
      first_samples.push_back(first);
      second_samples.push_back(second);
    }
  }

  *first_mean = mean(first_samples);
  *second_mean = mean(second_samples);
}

bool verify_result(const std::vector<unsigned char> &actual,
                   const std::vector<unsigned char> &original,
                   size_t block_size,
                   PrimitiveKind primitive,
                   uint8_t flag0,
                   uint8_t flag1) {
  const unsigned char *x = original.data();
  const unsigned char *y = original.data() + block_size;
  const unsigned char *actual0 = actual.data();
  const unsigned char *actual1 = actual.data() + block_size;

  const unsigned char *expected0;
  const unsigned char *expected1;

  if (primitive == PRIMITIVE_OSWAP) {
    expected0 = flag0 ? y : x;
    expected1 = flag0 ? x : y;
  } else {
    expected0 = flag0 ? y : x;
    expected1 = flag1 ? y : x;
  }

  return std::memcmp(actual0, expected0, block_size) == 0 &&
         std::memcmp(actual1, expected1, block_size) == 0;
}

bool check_correctness(size_t block_size) {
  std::vector<unsigned char> original(2 * block_size);
  std::vector<unsigned char> work(2 * block_size);
  initialize_input(original, block_size);

  const struct {
    PrimitiveKind primitive;
    uint8_t flag0;
    uint8_t flag1;
  } cases[] = {
      {PRIMITIVE_OSWAP, 0, 0},
      {PRIMITIVE_OSWAP, 1, 0},
      {PRIMITIVE_OFORK, 0, 0},
      {PRIMITIVE_OFORK, 0, 1},
      {PRIMITIVE_OFORK, 1, 0},
      {PRIMITIVE_OFORK, 1, 1},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    work = original;
    const double elapsed = MeasureObliviousPrimitive(
        work.data(), 1, block_size,
        static_cast<uint8_t>(cases[i].primitive),
        cases[i].flag0, cases[i].flag1);

    if (elapsed < 0.0 ||
        !verify_result(work, original, block_size,
                       cases[i].primitive,
                       cases[i].flag0,
                       cases[i].flag1)) {
      return false;
    }
  }
  return true;
}

bool supported_block_size(size_t block_size) {
  return block_size == 4 ||
         block_size == 8 ||
         block_size == 12 ||
         (block_size >= 16 && block_size % 16 == 0) ||
         (block_size >= 24 && block_size % 16 == 8);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr,
                 "Usage: %s <pairs> <block_size> <repeats> <warmups>\n",
                 argv[0]);
    return 1;
  }

  const size_t pairs = static_cast<size_t>(std::strtoull(argv[1], NULL, 10));
  const size_t block_size =
      static_cast<size_t>(std::strtoull(argv[2], NULL, 10));
  const size_t repeats = static_cast<size_t>(std::strtoull(argv[3], NULL, 10));
  const size_t warmups = static_cast<size_t>(std::strtoull(argv[4], NULL, 10));

  if (pairs == 0 || repeats == 0 || !supported_block_size(block_size)) {
    std::fprintf(stderr,
                 "Invalid arguments. Supported block sizes are 4, 8, 12, "
                 "16*n, and 8+16*n (n>=1).\n");
    return 1;
  }

  if (pairs > static_cast<size_t>(-1) / (2 * block_size)) {
    std::fprintf(stderr, "Requested buffer is too large.\n");
    return 1;
  }

  if (!OLib_initialize()) {
    return 2;
  }

  if (!check_correctness(block_size)) {
    std::fprintf(stderr,
                 "Correctness check failed for block_size=%zu.\n",
                 block_size);
    return 2;
  }

  const size_t bytes = 2 * pairs * block_size;
  std::vector<unsigned char> initial(bytes);
  std::vector<unsigned char> work(bytes);
  initialize_input(initial, block_size);

  Summary result = {};

  // Semantically equivalent identity operations:
  //   OSwap(0) == OFork(0,1) == (x,y)
  measure_pair(work, initial, pairs, block_size, repeats, warmups,
               PRIMITIVE_OSWAP, 0, 0,
               PRIMITIVE_OFORK, 0, 1,
               &result.oswap_0_ns, &result.ofork_01_ns);

  // Semantically equivalent swap operations:
  //   OSwap(1) == OFork(1,0) == (y,x)
  measure_pair(work, initial, pairs, block_size, repeats, warmups,
               PRIMITIVE_OSWAP, 1, 0,
               PRIMITIVE_OFORK, 1, 0,
               &result.oswap_1_ns, &result.ofork_10_ns);

  // OFork-specific duplication modes.
  measure_pair(work, initial, pairs, block_size, repeats, warmups,
               PRIMITIVE_OFORK, 0, 0,
               PRIMITIVE_OFORK, 1, 1,
               &result.ofork_00_ns, &result.ofork_11_ns);

  std::printf(
      "RESULT,%zu,%zu,%zu,"
      "%.9f,%.9f,%.9f,"
      "%.9f,%.9f,%.9f,"
      "%.9f,%.9f\n",
      block_size, pairs, repeats,
      result.oswap_0_ns,
      result.ofork_01_ns,
      result.ofork_01_ns / result.oswap_0_ns,
      result.oswap_1_ns,
      result.ofork_10_ns,
      result.ofork_10_ns / result.oswap_1_ns,
      result.ofork_00_ns,
      result.ofork_11_ns);

  return 0;
}
