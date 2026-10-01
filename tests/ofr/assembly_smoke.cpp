#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "../../CONFIG.h"
#include "../../Enclave/SubSample_v2/OFR/OFR.hpp"

thread_local uint64_t OSWAP_COUNTER = 0;

using namespace ofr;

static size_t batch_cases = 0;
static size_t mixed_cases = 0;

static void ReferenceGate(unsigned char *first, unsigned char *second,
                           size_t width, uint8_t control)
{
  unsigned char x[64], y[64];
  assert(width <= sizeof(x));
  std::memcpy(x, first, width);
  std::memcpy(y, second, width);
  std::memcpy(first, (control & 2U) ? y : x, width);
  std::memcpy(second, (control & 1U) ? y : x, width);
}

static std::vector<unsigned char> GuardedRecords(size_t n, size_t width,
                                                 size_t byte_offset)
{
  const size_t guard = 32;
  std::vector<unsigned char> data(guard + byte_offset + n * width + guard, 0xa5);
  unsigned char *records = data.data() + guard + byte_offset;
  for (size_t row = 0; row < n; ++row)
    for (size_t byte = 0; byte < width; ++byte)
      records[row * width + byte] = static_cast<unsigned char>(37 * row + 11 * byte + 19);
  return data;
}

static size_t ReferenceDfs(unsigned char *data, const std::vector<uint8_t> &controls,
                            size_t n, size_t width, size_t base,
                            size_t stride, size_t position)
{
  for (size_t gate = 0; gate < n / 2; ++gate)
  {
    const size_t first = base + 2 * gate * stride;
    ReferenceGate(data + first * width, data + (first + stride) * width,
                   width, controls[position + gate]);
  }
  position += n / 2;
  if (n > 2)
  {
    position = ReferenceDfs(data, controls, n / 2, width,
                             base, stride * 2, position);
    position = ReferenceDfs(data, controls, n / 2, width,
                             base + stride, stride * 2, position);
  }
  return position;
}

static void CheckFourGateBatches()
{
  const size_t n = 8, count = 12, guard = 32;
  for (size_t width : {size_t(8), size_t(16)})
    for (size_t gate_offset = 0; gate_offset < 8; ++gate_offset)
      for (size_t byte_offset = 0; byte_offset < 32; ++byte_offset)
        for (size_t encoded = 0; encoded < 256; ++encoded)
        {
          PackedControls shared(gate_offset + count + 3, OFR_COPY_SECOND);
          for (size_t gate = 0; gate < 8; ++gate)
            shared[gate_offset + gate] = OFR_STRAIGHT;
          shared.mutable_view(gate_offset, count).set_four_encoded(
              8, static_cast<uint8_t>(encoded));
          const PackedControls saved_controls = shared;
          std::vector<unsigned char> actual = GuardedRecords(n, width, byte_offset);
          std::vector<unsigned char> expected = actual;
          unsigned char *reference = expected.data() + guard + byte_offset;
          // The first eight gates are straight. Only the four independent
          // root gates change data, giving all 256 possible control packets.
          for (size_t gate = 0; gate < 4; ++gate)
            ReferenceGate(reference + gate * width,
                           reference + (gate + 4) * width, width,
                           static_cast<uint8_t>((encoded >> (2 * gate)) & 3U));
#ifdef COUNT_OSWAPS
          const uint64_t before = OSWAP_COUNTER;
#endif
          OFRApplyPreparedPostOrderInPlace(
              actual.data() + guard + byte_offset, shared.view(gate_offset, count),
              n, n / 2, n / 2, width);
#ifdef COUNT_OSWAPS
          assert(OSWAP_COUNTER - before == count);
#endif
          assert(actual == expected && shared == saved_controls);
          if (shared.size() % 4 != 0)
            assert((shared.bytes().back() >> (2 * (shared.size() % 4))) == 0);
          ++batch_cases;
        }
}

static void CheckMixedPostOrderBatches()
{
  std::mt19937 random(20261004);
  const size_t guard = 32;
  for (size_t n : {size_t(16), size_t(32)})
    for (size_t width : {size_t(8), size_t(16)})
      for (size_t trial = 0; trial < 4; ++trial)
      {
        const size_t count = OFRControlCount(n, n / 2, n / 2);
        std::vector<uint8_t> logical(count);
        PackedControls dfs(count);
        for (size_t gate = 0; gate < count; ++gate)
          dfs[gate] = logical[gate] = static_cast<uint8_t>(random() & 3U);
        const PackedControls postorder = OFRPostOrderControls(dfs, n, n / 2, n / 2);
        for (size_t gate_offset = 0; gate_offset < 8; ++gate_offset)
          for (size_t byte_offset = 0; byte_offset < 32; ++byte_offset)
          {
            PackedControls shared(gate_offset + count + 3, OFR_COPY_SECOND);
            shared.copy_from(gate_offset, postorder.view());
            const PackedControls saved_controls = shared;
            std::vector<unsigned char> actual = GuardedRecords(n, width, byte_offset);
            std::vector<unsigned char> expected = actual;
            // The reference uses the original independent strided DFS
            // network. Its physical row order is already the final order.
            assert(ReferenceDfs(expected.data() + guard + byte_offset,
                                  logical, n, width, 0, 1, 0) == count);
#ifdef COUNT_OSWAPS
            const uint64_t before = OSWAP_COUNTER;
#endif
            OFRApplyPreparedPostOrderInPlace(
                actual.data() + guard + byte_offset, shared.view(gate_offset, count),
                n, n / 2, n / 2, width);
#ifdef COUNT_OSWAPS
            assert(OSWAP_COUNTER - before == count);
#endif
            assert(actual == expected && shared == saved_controls);
            ++mixed_cases;
          }
      }
}

static uint32_t ReadId(const std::vector<unsigned char> &data)
{
  assert(data.size() >= sizeof(uint32_t));
  uint32_t id = 0;
  std::memcpy(&id, data.data(), sizeof(id));
  return id;
}

static void Check(size_t width,
                  uint8_t first_tag,
                  uint8_t second_tag,
                  uint32_t expected_left,
                  uint32_t expected_right)
{
  std::vector<unsigned char> data(2 * width);
  for (size_t byte = 0; byte < width; ++byte)
  {
    data[byte] = static_cast<unsigned char>(17 + byte);
    data[width + byte] = static_cast<unsigned char>(91 + byte);
  }
  const uint32_t first = 0;
  const uint32_t second = 1;
  std::memcpy(data.data(), &first, sizeof(first));
  std::memcpy(data.data() + width, &second, sizeof(second));

  const OFRDataResult output = OFRCompact(
      data.data(), {first_tag, second_tag}, 2, 1, 1, width);
  assert(ReadId(output.left) == expected_left);
  assert(ReadId(output.right) == expected_right);
  const unsigned char *expected_left_record = data.data() + expected_left * width;
  const unsigned char *expected_right_record = data.data() + expected_right * width;
  assert(std::memcmp(output.left.data(), expected_left_record, width) == 0);
  assert(std::memcmp(output.right.data(), expected_right_record, width) == 0);
}

static void CheckLevelOrdered(size_t width)
{
  const size_t n = 8;
  const std::vector<uint8_t> tags = {
      OFR_TAG_LEFT, OFR_TAG_BOTH, OFR_TAG_ZERO, OFR_TAG_RIGHT,
      OFR_TAG_BOTH, OFR_TAG_ZERO, OFR_TAG_LEFT, OFR_TAG_RIGHT};
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(data.data() + i * width, &id, sizeof(id));
  }
  const OFRDataResult expected = OFRCompact(
      data.data(), tags, n, n / 2, n / 2, width);
  const PackedControls controls = OFRControl(
      tags, n, n / 2, n / 2);
  const PackedControls level_controls = OFRLevelOrderControls(
      controls, n, n / 2, n / 2);
  OFRApplyLevelOrderedInPlace(data.data(), level_controls,
                              n, n / 2, n / 2, width);
  assert(std::memcmp(data.data(), expected.left.data(),
                     expected.left.size()) == 0);
  assert(std::memcmp(data.data() + expected.left.size(),
                     expected.right.data(), expected.right.size()) == 0);

  std::vector<unsigned char> postordered(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(postordered.data() + i * width, &id, sizeof(id));
  }
  const PackedControls postorder_controls = OFRPostOrderControls(
      controls, n, n / 2, n / 2);
  OFRApplyPreparedPostOrderInPlace(
      postordered.data(), postorder_controls,
      n, n / 2, n / 2, width);
  assert(postordered == data);

  // Production assembly must also consume gate spans sharing their boundary
  // storage bytes with neighboring nodes, without expanding the tape.
  for (size_t offset = 0; offset < 4; ++offset)
  {
    PackedControls shared(offset + postorder_controls.size() + 3,
                           OFR_COPY_SECOND);
    shared.copy_from(offset, postorder_controls.view());
    const PackedControls saved = shared;
    std::vector<unsigned char> shifted(n * width);
    for (size_t i = 0; i < n; ++i)
    {
      const uint32_t id = static_cast<uint32_t>(i);
      std::memcpy(shifted.data() + i * width, &id, sizeof(id));
    }
    OFRApplyPreparedPostOrderInPlace(
        shifted.data(), shared.view(offset, postorder_controls.size()),
        n, n / 2, n / 2, width);
    assert(shifted == data && shared == saved);
  }
}

int main()
{
  const size_t widths[] = {4, 7, 8, 12, 16, 24, 32, 40, 64};
  for (size_t width : widths)
  {
    Check(width, OFR_TAG_LEFT, OFR_TAG_RIGHT, 0, 1);
    Check(width, OFR_TAG_RIGHT, OFR_TAG_LEFT, 1, 0);
    Check(width, OFR_TAG_BOTH, OFR_TAG_ZERO, 0, 0);
    Check(width, OFR_TAG_ZERO, OFR_TAG_BOTH, 1, 1);
    CheckLevelOrdered(width);
  }
  CheckFourGateBatches();
  CheckMixedPostOrderBatches();
  std::printf("PASS: %zu production four-gate packets; %zu mixed postorder/DFS "
              "cases; unaligned data, packed spans and sentinels\n",
              batch_cases, mixed_cases);
}
