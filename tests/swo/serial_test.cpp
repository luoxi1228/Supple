#include "host_support.hpp"
#include "../../Enclave/SubSample_v2/SWO/helper.cpp"
#include "../../Enclave/SubSample_v2/SWO/SuppleSWO.cpp"

// Test-only inspection of packed flags; production readers use span checks.
static bool GetControlBit(const std::vector<uint8_t> &C, size_t p)
{
  return ((C.at(p / 8) >> (p % 8)) & 1U) != 0;
}

using Samples = std::vector<std::vector<size_t>>;
static size_t cases = 0;

// Independent membership oracle: shift individual bits AFTER compacting full
// membership rows, as in the LaTeX, rather than reusing projection helpers.
static void reference_write(const std::vector<size_t> &words, size_t width,
                            const std::vector<FrontierNode> &nodes,
                            size_t n, size_t m, std::vector<bool> &controls,
                            size_t &calls)
{
  const size_t word_bits = sizeof(size_t) * 8;
  for (const FrontierNode &node : nodes)
  {
    const size_t nv = std::min(n, m * node.count);
    std::vector<size_t> full = words;
    if (nv < n)
    {
      std::unique_ptr<bool[]> flags(new bool[n]);
      for (size_t i = 0; i < n; ++i)
      {
        bool selected = false;
        for (size_t j = node.start; j < node.start + node.count; ++j)
          selected |= (words[i * width + j / word_bits] >> (j % word_bits)) & 1;
        flags[i] = selected;
        controls.push_back(selected);
      }
      HostTightCompact(reinterpret_cast<unsigned char *>(full.data()), n,
                       width * sizeof(size_t), flags.get());
      ++calls;
    }
    const size_t child_width = (node.count + word_bits - 1) / word_bits;
    std::vector<size_t> child(nv * child_width, 0);
    for (size_t i = 0; i < nv; ++i)
      for (size_t j = 0; j < node.count; ++j)
      {
        const size_t bit = node.start + j;
        const size_t value = (full[i * width + bit / word_bits] >> (bit % word_bits)) & 1;
        child[i * child_width + j / word_bits] |= value << (j % word_bits);
      }
    if (node.count > 1)
    {
      const size_t left = node.count / 2;
      reference_write(child, child_width, {{0, left}, {left, node.count - left}},
                       nv, m, controls, calls);
    }
    else
      for (size_t value : child) assert(value == 1);
  }
}

static std::vector<unsigned char> records(size_t n, size_t width)
{
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    for (size_t b = 0; b < width; ++b)
      data[i * width + b] = static_cast<unsigned char>(i * 31 + b);
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(data.data() + i * width, &id, sizeof(id));
  }
  return data;
}

static void check_samples(const std::vector<unsigned char> &output,
                          const std::vector<unsigned char> &data,
                          const Samples &samples, size_t m, size_t width)
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
    std::vector<size_t> expected = samples[j];
    std::sort(expected.begin(), expected.end());
    assert(actual == expected);
  }
}

static void verify(size_t n, size_t m, const Samples &samples, size_t width)
{
  const size_t k = samples.size(), words = MarkWords(k);
  std::vector<size_t> M(n * words, 0);
  for (size_t j = 0; j < k; ++j)
    for (size_t id : samples[j])
      M[id * words + j / SwoWordBits()] |= size_t(1) << (j % SwoWordBits());
  const std::vector<size_t> saved_M = M;
  const std::vector<FrontierNode> F = m * k > n ? SWOFrontier(n, m, k)
                                               : std::vector<FrontierNode>{};
  std::vector<FrontierNode> nodes = F;
  if (nodes.empty())
    nodes = k == 1 ? std::vector<FrontierNode>{{0, 1}}
                   : std::vector<FrontierNode>{{0, k / 2}, {k / 2, k - k / 2}};
  size_t start = 0;
  for (const FrontierNode &node : F)
  {
    assert(node.start == start && node.count * m <= n);
    start += node.count;
  }
  assert(F.empty() || start == k);
  std::vector<bool> expected_controls;
  size_t expected_calls = 0;
  reference_write(M, words, nodes, n, m, expected_controls, expected_calls);
  const size_t bits = expected_controls.size();
  assert(SWOControlCount(nodes, n, m) == bits);
  compact_calls = compact_items = 0;
  const std::vector<uint8_t> C = SWOControl(M, F, n, m, k);
  assert(C.size() == (bits + 7) / 8 && M == saved_M);
  assert(compact_calls == expected_calls && compact_items == bits);
  for (size_t i = 0; i < bits; ++i) assert(GetControlBit(C, i) == expected_controls[i]);

  // Exercise a nonzero, non-byte-aligned span and preserve its surrounding bits.
  const size_t offset = 3 + cases % 7;
  std::vector<uint8_t> shared((offset + bits + 15) / 8, 0xa5);
  const std::vector<uint8_t> before = shared;
  assert(SWOControlWrite(M, shared, nodes, n, m, offset) == offset + bits);
  for (size_t i = 0; i < shared.size() * 8; ++i)
    assert(GetControlBit(shared, i) ==
           (i >= offset && i < offset + bits ? expected_controls[i - offset]
                                             : GetControlBit(before, i)));
  const std::vector<unsigned char> data = records(n, width);
  const std::vector<unsigned char> saved = data;
  compact_calls = compact_items = shuffle_calls = 0;
  const std::vector<unsigned char> result = SWOApply(data.data(), C, F, n, m, k, width);
  assert(data == saved && shuffle_calls == k);
  assert(compact_calls == expected_calls && compact_items == bits);
  check_samples(result, data, samples, m, width);
  std::vector<unsigned char> shifted(k * m * width);
  const ControlReadResult read = SWOControlRead(data.data(), shared, nodes, n, m,
      width, shifted.data(), k * m, offset);
  assert(read.next_pos == offset + bits && read.written_blocks == k * m);
  assert(shifted == result && data == saved);
  ++cases;
}

int main()
{
  // Equality boundary: retain the two capacity-8 nodes, omit their root filters.
  const std::vector<FrontierNode> frontier = SWOFrontier(8, 2, 8);
  assert(frontier.size() == 2 && frontier[0].count == 4 && frontier[1].start == 4);
  assert(SWOControlCount(frontier, 8, 2) == 64);
  assert(SWOControlCount(SWOFrontier(8, 8, 4), 8, 8) == 0);
  const auto overflow_frontier = SWOFrontier(SIZE_MAX, SIZE_MAX / 2 + 1, 3);
  assert(overflow_frontier.size() == 3);

  // Exhaust all small independent sample tuples, including complete overlap.
  for (size_t n = 2; n <= 5; ++n)
    for (size_t m = 1; m <= n; ++m)
    {
      Samples choices;
      for (size_t mask = 0; mask < (size_t(1) << n); ++mask)
      {
        std::vector<size_t> sample;
        for (size_t i = 0; i < n; ++i) if ((mask >> i) & 1) sample.push_back(i);
        if (sample.size() == m) choices.push_back(sample);
      }
      for (const auto &a : choices)
        for (const auto &b : choices)
          for (const auto &c : choices) verify(n, m, {a, b, c}, 8);
    }
  const size_t widths[] = {4, 8, 12, 16, 24, 32, 40, 64};
  const size_t ks[] = {1, 2, 3, 8, 17, 63, 64, 65, 127, 128, 129, 257};
  for (size_t k : ks)
    for (size_t width : widths)
      for (size_t n : {size_t(9), size_t(32), size_t(65)})
      {
        const size_t m = 1 + rng() % n;
        Samples samples(k);
        for (auto &sample : samples)
        {
          for (size_t i = 0; i < n; ++i) sample.push_back(i);
          std::shuffle(sample.begin(), sample.end(), rng);
          sample.resize(m);
        }
        verify(n, m, samples, width);
      }
  for (size_t k : {size_t(3), size_t(8), size_t(129)})
    verify(8, 2, Samples(k, {1, 6}), 24);

  // SWOMark and both production sampling entry points, including the ECALL.
  for (size_t k : {size_t(1), size_t(3), size_t(8), size_t(65)})
  {
    const size_t n = 16, m = 4, width = 24;
    rng.seed(731);
    const auto M = SWOMark(n, m, k);
    Samples samples(k);
    for (size_t j = 0; j < k; ++j)
    {
      for (size_t i = 0; i < n; ++i)
        if ((M[i * MarkWords(k) + j / SwoWordBits()] >> (j % SwoWordBits())) & 1)
          samples[j].push_back(i);
      assert(samples[j].size() == m);
    }
    auto data = records(n, width);
    rng.seed(731);
    check_samples(SWOSample(data.data(), n, m, k, width), data, samples, m, width);
    std::vector<unsigned char> encrypted(k * m * width);
    enc_ret ret{};
    rng.seed(731);
    shuffle_calls = 0;
    DecSuppleSWO(data.data(), n, m, k, width, encrypted.data(), &ret);
    assert(shuffle_calls == k);
    check_samples(encrypted, data, samples, m, width);
  }
  std::printf("PASS: %zu routing cases; exact controls and sample identities, "
              "skipped Compact calls, offset guards, wide membership, leaf "
              "Shuffle calls, serial entry points\n", cases);
}
