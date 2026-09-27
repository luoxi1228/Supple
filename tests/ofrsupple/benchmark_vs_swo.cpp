#include "../swo/host_support.hpp"
#include "../../Enclave/SubSample_v2/SWO/helper.cpp"
#include "../../Enclave/SubSample_v2/SWO/SuppleSWO.cpp"
#include "../../Enclave/SubSample_v2/OFRSupple/OFRSupple.cpp"

#include <chrono>
#include <numeric>
#include <string>

thread_local uint64_t OSWAP_COUNTER = 0;

namespace
{

volatile size_t benchmark_sink = 0;

double Median(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

template <typename F>
double TimeMs(F fn)
{
  const auto begin = std::chrono::steady_clock::now();
  fn();
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void Run(const char *name, size_t n, size_t m, size_t k, size_t width,
         size_t rounds)
{
  const size_t words = swo_detail::MarkWords(k);
  std::vector<size_t> membership(n * words, 0);
  std::mt19937 source_rng(20260927);
  std::vector<size_t> indices(n);
  for (size_t j = 0; j < k; ++j)
  {
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), source_rng);
    for (size_t t = 0; t < m; ++t)
      membership[indices[t] * words + j / swo_detail::SwoWordBits()] |=
          size_t(1) << (j % swo_detail::SwoWordBits());
  }
  const std::vector<FrontierNode> frontier = m * k > n
      ? SWOFrontier(n, m, k) : std::vector<FrontierNode>{};
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<unsigned char>(i * 37 + 13);

  const auto swo_nodes = swo_detail::SwoRootNodes(frontier, k);
  const size_t swo_bits = SWOControlCount(swo_nodes, n, m);
  const OFRSuppleControlCounts ofr_counts =
      OFRSuppleControlCount(frontier, n, m, k);
  std::vector<uint8_t> swo_controls = SWOControl(membership, frontier, n, m, k);
  OFRSuppleControls ofr_controls =
      OFRSuppleControl(membership, frontier, n, m, k);
  std::vector<double> swo_control, ofr_control, swo_apply, ofr_apply;

  for (size_t round = 0; round < rounds + 2; ++round)
  {
    double sc = 0, oc = 0, sa = 0, oa = 0;
    const auto run_swo_control = [&] {
      sc = TimeMs([&] {
        swo_controls = SWOControl(membership, frontier, n, m, k);
        benchmark_sink ^= swo_controls.size();
      });
    };
    const auto run_ofr_control = [&] {
      oc = TimeMs([&] {
        ofr_controls = OFRSuppleControl(membership, frontier, n, m, k);
        benchmark_sink ^= ofr_controls.ofr.size();
      });
    };
    const auto run_swo_apply = [&] {
      sa = TimeMs([&] {
        const auto result = SWOApply(data.data(), swo_controls, frontier,
                                     n, m, k, width);
        benchmark_sink ^= result[round % result.size()];
      });
    };
    const auto run_ofr_apply = [&] {
      oa = TimeMs([&] {
        const auto result = OFRSuppleApply(data.data(), ofr_controls, frontier,
                                           n, m, k, width);
        benchmark_sink ^= result[round % result.size()];
      });
    };
    if (round % 2 == 0)
    {
      run_swo_control();
      run_ofr_control();
      run_swo_apply();
      run_ofr_apply();
    }
    else
    {
      run_ofr_control();
      run_swo_control();
      run_ofr_apply();
      run_swo_apply();
    }
    if (round >= 2)
    {
      swo_control.push_back(sc);
      ofr_control.push_back(oc);
      swo_apply.push_back(sa);
      ofr_apply.push_back(oa);
    }
  }
  std::printf("%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f\n",
              name, n, m, k, width, swo_bits, ofr_counts.swo_bits,
              ofr_counts.ofr_words, Median(swo_control), Median(ofr_control),
              Median(swo_apply), Median(ofr_apply));
}

} // namespace

int main(int argc, char **argv)
{
  const size_t rounds = argc > 1 ? static_cast<size_t>(std::stoul(argv[1])) : 7;
  if (rounds == 0) return 2;
  std::puts("case,n,m,k,width,swo_bits,ofr_swo_bits,ofr_words,"
            "swo_control_ms,ofr_control_ms,swo_apply_ms,ofr_apply_ms");
  Run("compact", 4096, 16, 16, 16, rounds);
  Run("equal", 4096, 64, 64, 16, rounds);
  Run("frontier", 4096, 64, 128, 16, rounds);
  Run("mixed", 4096, 48, 128, 16, rounds);
  Run("equal_wide", 4096, 64, 64, 64, rounds);
  return static_cast<int>(benchmark_sink == 0xfffe);
}
