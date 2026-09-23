#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "../Untrusted/FMSCompact.hpp"
#include "../Untrusted/OLib.hpp"

namespace
{

enum RoutingTag : uint8_t
{
  TAG_ZERO = 0,
  TAG_RIGHT = 1,
  TAG_LEFT = 2,
  TAG_BOTH = 3
};

bool SupportedBlockSize(size_t block_size)
{
  return block_size <= std::numeric_limits<uint32_t>::max() &&
         (block_size == 4 ||
         block_size == 8 ||
         block_size == 12 ||
         (block_size >= 16 && block_size % 16 == 0) ||
         (block_size >= 24 && block_size % 16 == 8));
}

bool ParseSize(const char *text, size_t *value)
{
  if (text == NULL || value == NULL || text[0] == '\0' || text[0] == '-')
  {
    return false;
  }
  errno = 0;
  char *end = NULL;
  const unsigned long long parsed = std::strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' ||
      parsed > std::numeric_limits<size_t>::max())
  {
    return false;
  }
  *value = static_cast<size_t>(parsed);
  return true;
}

bool ParseDouble(const char *text, double *value)
{
  if (text == NULL || value == NULL || text[0] == '\0')
  {
    return false;
  }
  errno = 0;
  char *end = NULL;
  const double parsed = std::strtod(text, &end);
  if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed))
  {
    return false;
  }
  *value = parsed;
  return true;
}

bool BufferBytes(size_t n, size_t block_size, size_t *bytes)
{
  if (bytes == NULL ||
      (n != 0 && block_size > std::numeric_limits<size_t>::max() / n))
  {
    return false;
  }
  *bytes = n * block_size;
  return true;
}

double Mean(const std::vector<double> &values)
{
  double total = 0.0;
  for (double value : values)
  {
    total += value;
  }
  return total / static_cast<double>(values.size());
}

void InitializeInput(std::vector<unsigned char> *buffer,
                     size_t n,
                     size_t block_size)
{
  for (size_t index = 0; index < n; ++index)
  {
    unsigned char *block = buffer->data() + index * block_size;
    for (size_t byte = 0; byte < block_size; ++byte)
    {
      block[byte] = static_cast<unsigned char>(
          (index * 131U + byte * 17U + 29U) & 0xffU);
    }

    if (block_size == 4)
    {
      const uint32_t identifier = static_cast<uint32_t>(index);
      std::memcpy(block, &identifier, sizeof(identifier));
    }
    else
    {
      const uint64_t identifier = static_cast<uint64_t>(index);
      std::memcpy(block, &identifier, sizeof(identifier));
    }
  }
}

size_t ReadIdentifier(const unsigned char *block, size_t block_size)
{
  if (block_size == 4)
  {
    uint32_t identifier = 0;
    std::memcpy(&identifier, block, sizeof(identifier));
    return static_cast<size_t>(identifier);
  }

  uint64_t identifier = 0;
  std::memcpy(&identifier, block, sizeof(identifier));
  return static_cast<size_t>(identifier);
}

std::vector<uint8_t> MakeRoutingTags(size_t n,
                                     size_t n_left,
                                     size_t n_right,
                                     double requested_fork_ratio,
                                     uint64_t seed,
                                     size_t *fork_count)
{
  size_t overlap = static_cast<size_t>(
      std::floor(requested_fork_ratio * static_cast<double>(n) + 0.5));
  overlap = std::min(overlap, std::min(n_left, n_right));

  std::vector<uint8_t> tags;
  tags.reserve(n);
  tags.insert(tags.end(), overlap, TAG_ZERO);
  tags.insert(tags.end(), n_left - overlap, TAG_LEFT);
  tags.insert(tags.end(), n_right - overlap, TAG_RIGHT);
  tags.insert(tags.end(), overlap, TAG_BOTH);

  std::mt19937_64 generator(seed);
  std::shuffle(tags.begin(), tags.end(), generator);
  *fork_count = overlap;
  return tags;
}

bool VerifySide(const unsigned char *blocks,
                size_t block_offset,
                size_t output_count,
                size_t block_size,
                const std::vector<uint8_t> &tags,
                uint8_t responsibility_bit)
{
  std::vector<size_t> expected;
  std::vector<size_t> actual;
  expected.reserve(output_count);
  actual.reserve(output_count);

  for (size_t index = 0; index < tags.size(); ++index)
  {
    if ((tags[index] & responsibility_bit) != 0)
    {
      expected.push_back(index);
    }
  }
  for (size_t index = 0; index < output_count; ++index)
  {
    actual.push_back(ReadIdentifier(
        blocks + (block_offset + index) * block_size,
        block_size));
  }

  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  return expected == actual;
}

bool CheckCorrectness(const std::vector<unsigned char> &initial,
                      const std::vector<uint8_t> &tags,
                      size_t n,
                      size_t n_left,
                      size_t n_right,
                      size_t block_size)
{
  std::vector<unsigned char> fms = initial;
  std::vector<unsigned char> compact = initial;

  if (FMSCompactOnline(fms.data(), n, block_size) < 0.0 ||
      OCompactOnline(compact.data(), n, block_size) < 0.0)
  {
    return false;
  }

  return VerifySide(fms.data(), 0, n_left, block_size, tags, TAG_LEFT) &&
         VerifySide(fms.data(), n_left, n_right, block_size, tags, TAG_RIGHT) &&
         VerifySide(compact.data(),
                    0,
                    n_left,
                    block_size,
                    tags,
                    TAG_LEFT);
}

double RunFMSCompact(std::vector<unsigned char> *work,
                     const std::vector<unsigned char> &initial,
                     size_t n,
                     size_t block_size)
{
  // Input restoration is deliberately outside the enclave-side timer.
  std::copy(initial.begin(), initial.end(), work->begin());
  return FMSCompactOnline(work->data(), n, block_size);
}

double RunOCompact(std::vector<unsigned char> *work,
                   const std::vector<unsigned char> &initial,
                   size_t n,
                   size_t block_size)
{
  std::copy(initial.begin(), initial.end(), work->begin());
  return OCompactOnline(work->data(), n, block_size);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc != 7)
  {
    std::fprintf(
        stderr,
        "Usage: %s <n> <block_size> <fork_ratio> <repeats> <warmups> <seed>\n",
        argv[0]);
    return 1;
  }

  size_t n = 0;
  size_t block_size = 0;
  size_t repeats = 0;
  size_t warmups = 0;
  size_t seed_value = 0;
  double fork_ratio = 0.0;

  if (!ParseSize(argv[1], &n) ||
      !ParseSize(argv[2], &block_size) ||
      !ParseDouble(argv[3], &fork_ratio) ||
      !ParseSize(argv[4], &repeats) ||
      !ParseSize(argv[5], &warmups) ||
      !ParseSize(argv[6], &seed_value) ||
      n < 2 ||
      !SupportedBlockSize(block_size) ||
      repeats == 0 ||
      warmups > std::numeric_limits<size_t>::max() - repeats ||
      fork_ratio < 0.0 ||
      fork_ratio > 0.5 ||
      (block_size == 4 && n > std::numeric_limits<uint32_t>::max()))
  {
    std::fprintf(
        stderr,
        "Invalid arguments: n must be >= 2; fork_ratio must "
        "be in [0,0.5]; repeats must be positive; supported block sizes are "
        "4, 8, 12, 16*n, and 8+16*n (n>=1).\n");
    return 1;
  }

  size_t bytes = 0;
  if (!BufferBytes(n, block_size, &bytes))
  {
    std::fprintf(stderr, "Requested buffers are too large.\n");
    return 1;
  }

  const size_t n_left = n / 2 + n % 2;
  const size_t n_right = n / 2;
  size_t fork_count = 0;
  std::vector<uint8_t> tags = MakeRoutingTags(
      n,
      n_left,
      n_right,
      fork_ratio,
      static_cast<uint64_t>(seed_value),
      &fork_count);
  const double actual_fork_ratio =
      static_cast<double>(fork_count) / static_cast<double>(n);

  std::vector<unsigned char> initial(bytes);
  std::vector<unsigned char> fms_work(bytes);
  std::vector<unsigned char> compact_work(bytes);
  InitializeInput(&initial, n, block_size);

  if (!OLib_initialize()) {
    return 2;
  }

  size_t control_words = 0;
  double control_us = -1.0;
  const int prepare_result = FMSCompactPrepare(tags.data(),
                                               n,
                                               n_left,
                                               n_right,
                                               &control_words,
                                               &control_us);
  if (prepare_result != 0)
  {
    std::fprintf(stderr, "FMSCompactPrepare failed with code %d.\n", prepare_result);
    return 2;
  }

  if (!CheckCorrectness(initial, tags, n, n_left, n_right, block_size))
  {
    std::fprintf(stderr,
                 "Correctness check failed for n=%zu, block_size=%zu, "
                 "fork_ratio=%.9f.\n",
                 n,
                 block_size,
                 actual_fork_ratio);
    FMSCompactRelease();
    return 3;
  }

  std::vector<double> fmscompact_samples;
  std::vector<double> compact_samples;
  fmscompact_samples.reserve(repeats);
  compact_samples.reserve(repeats);

  const size_t rounds = warmups + repeats;
  for (size_t round = 0; round < rounds; ++round)
  {
    double fmscompact_time = -1.0;
    double compact_time = -1.0;

    // Alternate order to reduce cache, frequency, and temperature bias.
    if ((round & 1U) == 0U)
    {
      fmscompact_time = RunFMSCompact(&fms_work, initial, n, block_size);
      compact_time = RunOCompact(&compact_work, initial, n, block_size);
    }
    else
    {
      compact_time = RunOCompact(&compact_work, initial, n, block_size);
      fmscompact_time = RunFMSCompact(&fms_work, initial, n, block_size);
    }

    if (fmscompact_time < 0.0 || compact_time < 0.0)
    {
      std::fprintf(stderr, "An online measurement ECALL failed.\n");
      FMSCompactRelease();
      return 2;
    }

    if (round >= warmups)
    {
      fmscompact_samples.push_back(fmscompact_time);
      compact_samples.push_back(compact_time);
    }
  }

  const double fms_control_ms = control_us / 1000.0;
  const double fmscompact_ms = Mean(fmscompact_samples) / 1000.0;
  const double ocompact_ms = Mean(compact_samples) / 1000.0;
  const double speedup = fmscompact_ms > 0.0
                             ? ocompact_ms / fmscompact_ms
                             : std::numeric_limits<double>::infinity();

  std::printf(
      "RESULT,%zu,%zu,%.9f,%zu,%zu,%.9f,"
      "%.9f,%.9f,%.9f,1\n",
      n,
      block_size,
      actual_fork_ratio,
      repeats,
      control_words,
      fms_control_ms,
      fmscompact_ms,
      ocompact_ms,
      speedup);

  FMSCompactRelease();
  return 0;
}
