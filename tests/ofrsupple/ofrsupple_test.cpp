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
static size_t rejected_plans = 0;
static size_t one_word_tag_cases = 0;

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
  const std::vector<size_t> membership_before = membership;
  const OFRSuppleControls controls =
      OFRSuppleControl(membership, frontier, n, m, k);
  assert(membership == membership_before);
  assert(controls.swo.size() == (counts.swo_bits + 7) / 8);
  assert(controls.ofr.size() == counts.ofr_words);
  assert(controls.ofr.byte_size() == (counts.ofr_words + 3) / 4);
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
  const size_t ofr_offset = checked % 4;
  OFRSuppleControls shifted;
  shifted.swo.assign((swo_offset + counts.swo_bits + 8) / 8, 0xa5);
  shifted.ofr = ofr::PackedControls(ofr_offset + counts.ofr_words + 2,
                                    ofr::OFR_COPY_SECOND);
  const OFRSuppleControls original = shifted;
  const OFRSuppleControlPositions end = OFRSuppleControlWrite(
      membership, shifted, frontier, n, m, k,
      {swo_offset, ofr_offset});
  assert(membership == membership_before);
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
  assert(membership == membership_before);
  assert(raw_end.swo_bits == counts.swo_bits);
  assert(raw_end.ofr_words == counts.ofr_words);
  for (size_t i = 0; i < shifted.ofr.size(); ++i)
    assert(shifted.ofr[i] == (i >= ofr_offset && i < end.ofr_words
        ? raw.ofr[i - ofr_offset] : original.ofr[i]));
  ++checked;
}

static OFRSuppleControls PreflightControls(size_t n, size_t m, size_t k)
{
  const size_t words = swo_detail::MarkWords(k);
  std::vector<size_t> membership(n * words, 0);
  for (size_t sample = 0; sample < k; ++sample)
    for (size_t row = 0; row < m; ++row)
      membership[((sample + row) % n) * words +
                 sample / swo_detail::SwoWordBits()] |=
          size_t(1) << (sample % swo_detail::SwoWordBits());
  const std::vector<FrontierNode> frontier = m * k > n
      ? SWOFrontier(n, m, k) : std::vector<FrontierNode>{};
  return OFRSuppleControl(membership, frontier, n, m, k);
}

static void RejectPlanBeforeWrites(const OFRSuppleControls &controls,
                                    size_t n, size_t m, size_t k,
                                    size_t out_capacity_blocks)
{
  const size_t guard = 13, width = 24;
  const std::vector<unsigned char> records = Records(n, width);
  std::vector<unsigned char> input(2 * guard + records.size(), 0xa5);
  std::copy(records.begin(), records.end(), input.begin() + guard);
  const std::vector<unsigned char> input_before = input;
  std::vector<unsigned char> output(2 * guard + m * k * width, 0x5a);
  const std::vector<unsigned char> output_before = output;
  const std::vector<FrontierNode> frontier = m * k > n
      ? SWOFrontier(n, m, k) : std::vector<FrontierNode>{};
  const size_t before_compact = compact_calls.load();
  const size_t before_shuffle = shuffle_calls.load();
  bool rejected = false;
  try
  {
    OFRSuppleControlRead(input.data() + guard, controls, frontier,
                         n, m, k, width, output.data() + guard,
                         out_capacity_blocks, {0, 0});
  }
  catch (const std::exception &) { rejected = true; }
  assert(rejected);
  assert(input == input_before && output == output_before);
  assert(compact_calls.load() == before_compact);
  assert(shuffle_calls.load() == before_shuffle);
  ++rejected_plans;
}

static void CheckReadPlanPreflight()
{
  const size_t n = 16, m = 2, k = 8;
  const OFRSuppleControls controls = PreflightControls(n, m, k);
  assert(controls.nodes.size() > 2);
  // Damage the final node so an incremental validator would already have
  // written earlier samples. Whole-plan validation rejects all of these first.
  OFRSuppleControls bad = controls;
  ++bad.nodes.back().offset;
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  bad.nodes.back().shape_index = bad.shapes.size();
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  --bad.nodes.back().count;
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  OFRSuppleControls::Shape wrong_shape = bad.shapes[bad.nodes.back().shape_index];
  ++wrong_shape.n_left;
  --wrong_shape.n_right;
  bad.nodes.back().shape_index = bad.shapes.size();
  bad.shapes.push_back(wrong_shape);
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  bad.nodes.pop_back();
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  bad.nodes.push_back(controls.nodes.back());
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  bad = controls;
  bad.ofr.resize(bad.ofr.size() - 1);
  RejectPlanBeforeWrites(bad, n, m, k, m * k);
  RejectPlanBeforeWrites(controls, n, m, k, m * k - 1);

  const OFRSuppleControls mixed = PreflightControls(25, 5, 21);
  assert(!mixed.swo.empty());
  bad = mixed;
  bad.swo.pop_back();
  RejectPlanBeforeWrites(bad, 25, 5, 21, 5 * 21);

  const OFRSuppleControls generic = PreflightControls(15, 3, 5);
  assert(generic.nodes.size() > 1 && !generic.nodes.back().postordered);
  bad = generic;
  bad.nodes.back().postordered = true;
  RejectPlanBeforeWrites(bad, 15, 3, 5, 3 * 5);
}

static void CheckOneWordTagPacking()
{
  for (size_t n = 0; n <= 17; ++n)
    for (size_t word_offset = 0; word_offset < 4; ++word_offset)
      for (size_t tag_offset = 0; tag_offset < 4; ++tag_offset)
        for (size_t k : {size_t(3), size_t(5), size_t(63), size_t(64)})
          for (size_t padding = 0; padding < 2; ++padding)
          {
            const size_t left_k = k / 2, right_k = k - left_k;
            const size_t left_bit = size_t(1) << (left_k - 1);
            const size_t right_bit = size_t(1) << (k - 1);
            const size_t outside = padding && k < swo_detail::SwoWordBits()
                ? ~((size_t(1) << k) - 1) : 0;
            std::vector<size_t> membership(word_offset + n + 3, size_t(0xa5));
            for (size_t row = 0; row < n; ++row)
            {
              const size_t flags = (row + word_offset + tag_offset) % 4;
              membership[word_offset + row] = outside |
                  ((flags & 2U) ? left_bit : 0) |
                  ((flags & 1U) ? right_bit : 0);
            }
            const std::vector<size_t> original = membership;
            std::vector<uint8_t> expected(tag_offset + n + 3, 0xa5);
            std::vector<uint8_t> actual = expected;
            size_t left_expected = 7, right_expected = 13;
            for (size_t row = 0; row < n; ++row)
            {
              const size_t *source = membership.data() + word_offset + row;
              const uint8_t left = HasMembership(source, 1, 0, left_k);
              const uint8_t right = HasMembership(source, 1, left_k, right_k);
              expected[tag_offset + row] = static_cast<uint8_t>((left << 1U) | right);
              left_expected += left;
              right_expected += right;
            }
            size_t left_actual = 7, right_actual = 13;
            OneWordTags(membership.data() + word_offset, actual.data() + tag_offset,
                         n, left_k, right_k, left_actual, right_actual);
            assert(actual == expected && membership == original);
            assert(left_actual == left_expected && right_actual == right_expected);
            const size_t left_weight = left_expected - 7;
            const size_t right_weight = right_expected - 13;
            if (n != 0 && left_weight + right_weight <= n)
            {
              const size_t capacity = left_weight + (n - left_weight - right_weight) / 2;
              const std::vector<uint8_t> scalar_tags(expected.begin() + tag_offset,
                                                    expected.begin() + tag_offset + n);
              const std::vector<uint8_t> packed_tags(actual.begin() + tag_offset,
                                                    actual.begin() + tag_offset + n);
              assert(ofr::OFRControl(scalar_tags, n, capacity, n - capacity) ==
                     ofr::OFRControl(packed_tags, n, capacity, n - capacity));
            }
            ++one_word_tag_cases;
          }
}

int main()
{
  CheckReadPlanPreflight();
  CheckOneWordTagPacking();
  assert(OFRSuppleControlCount({}, 8, 2, 4).swo_bits == 0);
  assert(OFRSuppleControlCount({}, 8, 2, 4).ofr_words > 0);
  assert(OFRSuppleControlCount(SWOFrontier(9, 2, 8), 9, 2, 8).swo_bits > 0);
  assert(OFRSuppleControlCount(SWOFrontier(9, 2, 8), 9, 2, 8).ofr_words > 0);
  Verify(8, 2, Samples(4, {1, 6}), 8);
  Verify(9, 2, Samples(8, {1, 6}), 24);
  // ceil(21 / floor(25 / 5)) yields five balanced frontier groups,
  // with counts 5,4,4,4,4 rather than four groups of five and a singleton.
  const std::vector<FrontierNode> mixed_frontier = SWOFrontier(25, 5, 21);
  assert(mixed_frontier.size() == 5);
  const OFRSuppleControlCounts mixed_counts =
      OFRSuppleControlCount(mixed_frontier, 25, 5, 21);
  assert(mixed_counts.swo_bits == 100 && mixed_counts.ofr_words == 390);
  Verify(25, 5, Samples(21, {1, 5, 7, 13, 22}), 24);

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
  // The single-word label fast path must include the highest membership bit.
  for (size_t m : {size_t(1), size_t(2)})
  {
    const size_t k = 64, n = m * k;
    Samples samples(k);
    for (size_t j = 0; j < k; ++j)
    {
      samples[j].push_back((j * 7) % n);
      if (m == 2) samples[j].push_back((j * 7 + 31) % n);
    }
    Verify(n, m, samples, 24);
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
  std::printf("PASS: %zu OFRSupple cases; %zu single-word tag cases; "
              "%zu invalid plans rejected before "
              "writes; membership, both control streams, offsets and ECALL\n",
              checked, one_word_tag_cases, rejected_plans);
}
