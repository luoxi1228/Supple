#include "../swo/host_support.hpp"
#include "../../Enclave/SubSample_v2/SWO/helper.cpp"
#include "../../Enclave/SubSample_v2/SWO/SuppleSWO.cpp"
#include "../../Enclave/SubSample_v2/OFR/helper.cpp"
#include "../../Enclave/SubSample_v2/OFR/OFR.cpp"
#include "../../Enclave/SubSample_v2/OFRSupple/OFRSupple.cpp"

#include <cassert>
#include <cstdio>

using Samples = std::vector<std::vector<size_t>>;
static size_t checked = 0;

static std::vector<unsigned char> Records(size_t n, size_t width)
{
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(data.data() + i * width, &id, sizeof(id));
    for (size_t j = sizeof(id); j < width; ++j)
      data[i * width + j] = static_cast<unsigned char>(i * 29 + j);
  }
  return data;
}

static void CheckSamples(const std::vector<unsigned char> &output,
                         const std::vector<unsigned char> &data,
                         Samples samples, size_t m, size_t width)
{
  assert(output.size() == samples.size() * m * width);
  for (size_t j = 0; j < samples.size(); ++j)
  {
    std::vector<size_t> actual;
    for (size_t i = 0; i < m; ++i)
    {
      const unsigned char *row = output.data() + (j * m + i) * width;
      uint32_t id = 0;
      std::memcpy(&id, row, sizeof(id));
      assert(id < data.size() / width);
      assert(std::memcmp(row, data.data() + id * width, width) == 0);
      actual.push_back(id);
    }
    std::sort(actual.begin(), actual.end());
    std::sort(samples[j].begin(), samples[j].end());
    assert(actual == samples[j]);
  }
}

static void Verify(size_t n, size_t m, const Samples &samples, size_t width)
{
  const size_t k = samples.size();
  const size_t words = swo_detail::MarkWords(k);
  std::vector<size_t> membership(n * words, 0);
  for (size_t j = 0; j < k; ++j)
    for (size_t id : samples[j])
      membership[id * words + j / swo_detail::SwoWordBits()] |=
          size_t(1) << (j % swo_detail::SwoWordBits());
  const std::vector<FrontierNode> frontier = m * k > n
      ? SWOFrontier(n, m, k) : std::vector<FrontierNode>{};
  const OFRSuppleControlCounts counts =
      OFRSuppleControlCount(frontier, n, m, k);
  const OFRSuppleControls controls =
      OFRSuppleControl(membership, frontier, n, m, k);
  assert(controls.swo.size() == (counts.swo_bits + 7) / 8);
  assert(controls.ofr.size() == counts.ofr_words);
  size_t prepared_words = 0;
  for (const OFRSuppleControls::Node &node : controls.nodes)
  {
    assert(node.offset == prepared_words);
    prepared_words += node.count;
  }
  assert(prepared_words == counts.ofr_words);

  const std::vector<unsigned char> data = Records(n, width);
  const size_t before_shuffle = shuffle_calls;
  const std::vector<unsigned char> result = OFRSuppleApply(
      data.data(), controls, frontier, n, m, k, width);
  assert(shuffle_calls == before_shuffle);
  CheckSamples(result, data, samples, m, width);

  // Both streams can start at nonzero offsets without changing nearby controls.
  const size_t swo_offset = 3;
  const size_t ofr_offset = 2;
  OFRSuppleControls shifted;
  shifted.swo.assign((swo_offset + counts.swo_bits + 8) / 8, 0xa5);
  shifted.ofr.assign(ofr_offset + counts.ofr_words + 2, 0xff);
  const OFRSuppleControls original = shifted;
  const OFRSuppleControlPositions end = OFRSuppleControlWrite(
      membership, shifted, frontier, n, m, k,
      {swo_offset, ofr_offset});
  assert(end.swo_bits == swo_offset + counts.swo_bits);
  assert(end.ofr_words == ofr_offset + counts.ofr_words);
  for (size_t i = 0; i < shifted.swo.size() * 8; ++i)
  {
    const bool bit = ((shifted.swo[i / 8] >> (i % 8)) & 1U) != 0;
    const bool expected = i >= swo_offset && i < end.swo_bits
        ? ((controls.swo[(i - swo_offset) / 8] >>
            ((i - swo_offset) % 8)) & 1U) != 0
        : ((original.swo[i / 8] >> (i % 8)) & 1U) != 0;
    assert(bit == expected);
  }
  std::vector<unsigned char> shifted_output(k * m * width);
  const OFRSuppleReadResult read = OFRSuppleControlRead(
      data.data(), shifted, frontier, n, m, k, width,
      shifted_output.data(), k * m, {swo_offset, ofr_offset});
  assert(read.written_blocks == k * m);
  assert(read.next.swo_bits == end.swo_bits);
  assert(read.next.ofr_words == end.ofr_words);
  assert(shifted_output == result);
  OFRSuppleControls raw;
  raw.swo.resize((counts.swo_bits + 7) / 8);
  raw.ofr.resize(counts.ofr_words);
  const OFRSuppleControlPositions raw_end = OFRSuppleControlWrite(
      membership, raw, frontier, n, m, k, {0, 0});
  assert(raw_end.swo_bits == counts.swo_bits);
  assert(raw_end.ofr_words == counts.ofr_words);
  for (size_t i = 0; i < shifted.ofr.size(); ++i)
    assert(shifted.ofr[i] == (i >= ofr_offset && i < end.ofr_words
        ? raw.ofr[i - ofr_offset] : original.ofr[i]));
  ++checked;
}

int main()
{
  assert(OFRSuppleControlCount({}, 8, 2, 4).swo_bits == 0);
  assert(OFRSuppleControlCount({}, 8, 2, 4).ofr_words > 0);
  assert(OFRSuppleControlCount(SWOFrontier(9, 2, 8), 9, 2, 8).swo_bits > 0);
  assert(OFRSuppleControlCount(SWOFrontier(9, 2, 8), 9, 2, 8).ofr_words > 0);
  Verify(8, 2, Samples(4, {1, 6}), 8);
  Verify(9, 2, Samples(8, {1, 6}), 24);

  for (size_t n : {size_t(4), size_t(5)})
  {
    Samples choices;
    for (size_t mask = 0; mask < (size_t(1) << n); ++mask)
    {
      std::vector<size_t> sample;
      for (size_t i = 0; i < n; ++i)
        if ((mask >> i) & 1U) sample.push_back(i);
      if (sample.size() == 2) choices.push_back(sample);
    }
    for (const auto &left : choices)
      for (const auto &middle : choices)
        for (const auto &right : choices)
          Verify(n, 2, {left, middle, right}, 8);
  }

  for (size_t n = 2; n <= 12; ++n)
    for (size_t m = 1; m <= n; ++m)
      for (size_t k = 1; k <= 8; ++k)
      {
        Samples samples(k);
        for (auto &sample : samples)
        {
          for (size_t i = 0; i < n; ++i) sample.push_back(i);
          std::shuffle(sample.begin(), sample.end(), rng);
          sample.resize(m);
        }
        Verify(n, m, samples, 8 + 4 * (k % 3));
      }
  for (size_t k : {size_t(65), size_t(129)})
    for (size_t n : {size_t(8), size_t(17), size_t(32)})
    {
      const size_t m = n / 2;
      Samples samples(k);
      for (auto &sample : samples)
      {
        for (size_t i = 0; i < n; ++i) sample.push_back(i);
        std::shuffle(sample.begin(), sample.end(), rng);
        sample.resize(m);
      }
      Verify(n, m, samples, 24);
    }

  const size_t n = 16, m = 4, k = 8, width = 24;
  const std::vector<unsigned char> data = Records(n, width);
  rng.seed(731);
  const std::vector<size_t> membership = SWOMark(n, m, k);
  Samples samples(k);
  for (size_t j = 0; j < k; ++j)
    for (size_t i = 0; i < n; ++i)
      if ((membership[i * swo_detail::MarkWords(k) +
                      j / swo_detail::SwoWordBits()] >>
           (j % swo_detail::SwoWordBits())) & 1U)
        samples[j].push_back(i);
  for (size_t j = 0; j < k; ++j) assert(samples[j].size() == m);
  rng.seed(731);
  CheckSamples(OFRSupple(data.data(), n, m, k, width), data,
               samples, m, width);
  std::vector<unsigned char> encrypted(k * m * width);
  enc_ret ret{};
  ret.collect_offline_profile = 1;
  rng.seed(731);
  DecOFRSupple(const_cast<unsigned char *>(data.data()), n, m, k, width,
               encrypted.data(), &ret);
  CheckSamples(encrypted, data, samples, m, width);
  std::printf("PASS: %zu OFRSupple cases; membership, both control streams, offsets and ECALL\n",
              checked);
}
