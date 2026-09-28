#include "../../Enclave/SubSample_v2/OFR/OFR.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <vector>

thread_local uint64_t OSWAP_COUNTER = 0;

namespace
{

volatile uint64_t sink = 0;

template <typename Prepare, typename Run>
double MedianMilliseconds(size_t rounds, Prepare prepare, Run run)
{
  std::vector<double> samples;
  samples.reserve(rounds);
  for (size_t i = 0; i < rounds + 3; ++i)
  {
    prepare();
    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    run();
    const std::chrono::steady_clock::time_point stop =
        std::chrono::steady_clock::now();
    if (i >= 3)
      samples.push_back(
          std::chrono::duration<double, std::milli>(stop - start).count());
  }
  std::sort(samples.begin(), samples.end());
  return samples[rounds / 2];
}

size_t ParsePositive(const char *value)
{
  char *end = NULL;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (value[0] == '-' || end == value || *end != '\0' || parsed == 0)
    throw std::invalid_argument("Expected a positive integer");
  return static_cast<size_t>(parsed);
}

} // namespace

int main(int argc, char **argv)
{
  try
  {
    const size_t n = argc > 1 ? ParsePositive(argv[1]) : 65536;
    const size_t rounds = argc > 2 ? ParsePositive(argv[2]) : 9;
    const size_t max_width = argc > 3 ? ParsePositive(argv[3]) : 1024;
    if (argc > 4 || n < 4 || (n & (n - 1)) != 0 ||
        rounds > 1000)
      throw std::invalid_argument("Usage: phase_benchmark [power-of-two n>=4] [rounds<=1000] [max_block_bytes]");

    std::vector<uint8_t> tags(n);
    for (size_t i = 0; i < n; i += 4)
    {
      tags[i] = ofr::OFR_TAG_BOTH;
      tags[i + 1] = ofr::OFR_TAG_ZERO;
      tags[i + 2] = ofr::OFR_TAG_LEFT;
      tags[i + 3] = ofr::OFR_TAG_RIGHT;
    }
    std::mt19937_64 random(20260928);
    std::shuffle(tags.begin(), tags.end(), random);

    const size_t half = n / 2;
    const std::vector<uint8_t> normalized =
        ofr::OFRNormalize(tags, n, half, half);
    const std::vector<uint8_t> controls =
        ofr::OFRControl(tags, n, half, half);
    const std::vector<uint8_t> postorder =
        ofr::OFRPostOrderControls(controls, n, half, half);
    if (ofr::OFRControlPostOrder(tags, n, half, half) != postorder)
      throw std::logic_error("Prepared postorder control mismatch");

    const double normalize_ms = MedianMilliseconds(
        rounds, []() {}, [&]() {
          const std::vector<uint8_t> result =
              ofr::OFRNormalize(tags, n, half, half);
          sink += result[n / 3];
        });

    std::vector<uint8_t> work_tags(n);
    std::vector<uint8_t> work_controls(controls.size());
    const double control_ms = MedianMilliseconds(
        rounds, [&]() { work_tags = normalized; }, [&]() {
          const size_t end = ofr::OFRControlWrite(
              work_tags, work_controls, n, half, half, 0);
          sink += work_controls[end / 3];
        });

    const double postorder_ms = MedianMilliseconds(
        rounds, []() {}, [&]() {
          const std::vector<uint8_t> result =
              ofr::OFRPostOrderControls(controls, n, half, half);
          sink += result[result.size() / 3];
        });

    const double prepared_control_ms = MedianMilliseconds(
        rounds, []() {}, [&]() {
          const std::vector<uint8_t> result =
              ofr::OFRControlPostOrder(tags, n, half, half);
          sink += result[result.size() / 3];
        });

    const double offline_ms = MedianMilliseconds(
        rounds, []() {}, [&]() {
          const std::vector<uint8_t> generated =
              ofr::OFRControlPostOrder(tags, n, half, half);
          sink += generated.size();
        });

    std::printf(
        "n=%zu gates=%zu normalize_ms=%.3f control_ms=%.3f "
        "postorder_ms=%.3f prepared_control_ms=%.3f "
        "offline_total_ms=%.3f\n",
        n, controls.size(), normalize_ms, control_ms, postorder_ms,
        prepared_control_ms, offline_ms);

    const size_t widths[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    for (size_t width : widths)
    {
      if (width > max_width)
        continue;
      std::vector<uint8_t> initial(n * width, 0x5a);
      std::vector<uint8_t> work(initial.size());
      const double online_ms = MedianMilliseconds(
          rounds,
          [&]() { std::copy(initial.begin(), initial.end(), work.begin()); },
          [&]() {
            ofr::OFRApplyPreparedPostOrderInPlace(
                work.data(), postorder, n, half, half, width);
            sink += work[width * (n / 3)];
          });
      std::printf(
          "block_bytes=%zu online_ms=%.3f control_over_online=%.2f "
          "offline_over_online=%.2f\n",
          width, online_ms, control_ms / online_ms, offline_ms / online_ms);
    }
    return sink == 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
