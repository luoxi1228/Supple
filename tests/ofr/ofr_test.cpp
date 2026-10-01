#define BEFTS_MODE

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "../../Enclave/SubSample_v2/OFR/helper.cpp"
#include "../../Enclave/SubSample_v2/OFR/OFR.cpp"

using namespace ofr;

static size_t exact_cases = 0;
static size_t normalize_cases = 0;
static size_t fused_cases = 0;
static size_t tiled_cases = 0;
static size_t narrow_boundary_cases = 0;
static size_t membership_byte_overflow_cases = 0;

static std::vector<unsigned char> Records(size_t n, size_t width);

static bool SameControls(const PackedControls &expected,
                         const PackedControls &actual, size_t offset = 0)
{
  assert(offset <= actual.size());
  assert(expected.size() <= actual.size() - offset);
  for (size_t i = 0; i < expected.size(); ++i)
    if (expected[i] != actual[offset + i]) return false;
  return true;
}

template <typename Exception, typename Action>
static void ExpectException(Action action)
{
  bool threw = false;
  try { action(); }
  catch (const Exception &) { threw = true; }
  assert(threw);
}

static void CheckPackedSpanErrors()
{
  const std::vector<uint8_t> valid_tags = {
      OFR_TAG_LEFT, OFR_TAG_RIGHT, OFR_TAG_BOTH, OFR_TAG_ZERO};
  const PackedControls valid_controls = OFRControl(valid_tags, 4, 2, 2);
  PackedControls short_controls(valid_controls.size() - 1);
  ExpectException<std::length_error>([&]() {
    OFRControlWrite(valid_tags, short_controls, 4, 2, 2, 0);
  });
  ExpectException<std::length_error>([&]() {
    OFRControlWrite(valid_tags, short_controls, 4, 2, 2,
                     std::numeric_limits<size_t>::max());
  });
  const std::vector<unsigned char> data = Records(4, 8);
  ExpectException<std::length_error>([&]() {
    OFRControlRead(data.data(), short_controls, 4, 2, 2, 8, 0);
  });
  ExpectException<std::length_error>([&]() {
    OFRControlRead(data.data(), valid_controls, 4, 2, 2, 8,
                    std::numeric_limits<size_t>::max());
  });
  ExpectException<std::length_error>([&]() {
    OFRPostOrderControls(short_controls.view(), 4, 2, 2);
  });
  ExpectException<std::length_error>([&]() {
    OFRLevelOrderControls(short_controls, 4, 2, 2);
  });

  // Unchecked four-gate readers must only run after a whole-span preflight.
  // A truncated final run is rejected before the first record is changed.
  const std::vector<uint8_t> run_tags = {
      OFR_TAG_LEFT, OFR_TAG_RIGHT, OFR_TAG_BOTH, OFR_TAG_ZERO,
      OFR_TAG_LEFT, OFR_TAG_RIGHT, OFR_TAG_BOTH, OFR_TAG_ZERO};
  const PackedControls run_controls = OFRControl(run_tags, 8, 4, 4);
  const PackedControls run_level = OFRLevelOrderControls(run_controls, 8, 4, 4);
  const PackedControls run_postorder = OFRPostOrderControls(run_controls, 8, 4, 4);
  PackedControls short_level = run_level;
  short_level.resize(run_level.size() - 1);
  const std::vector<unsigned char> saved = Records(8, 8);
  std::vector<unsigned char> work = saved;
  ExpectException<std::length_error>([&]() {
    OFRApplyPreparedLevelOrderedInPlace(work.data(), short_level, 8, 4, 4, 8);
  });
  assert(work == saved);
  for (size_t offset = 0; offset < 8; ++offset)
  {
    PackedControls shared(offset + run_postorder.size() + 3, OFR_COPY_SECOND);
    shared.copy_from(offset, run_postorder.view());
    ExpectException<std::length_error>([&]() {
      OFRApplyPreparedPostOrderInPlace(
          work.data(), shared.view(offset, run_postorder.size() - 1),
          8, 4, 4, 8);
    });
    assert(work == saved);
  }
}

static void CheckPackedControls()
{
  const PackedControls empty;
  assert(empty.size() == 0 && empty.byte_size() == 0);
  assert(empty.bytes().empty() && empty.view().size() == 0);

  // Every logical slot, including incomplete final bytes, stores exactly two
  // bits. Changing one slot must preserve all of its byte's neighboring slots.
  for (size_t count = 0; count <= 17; ++count)
  {
    PackedControls controls(count);
    for (size_t gate = 0; gate < count; ++gate)
      controls.set(gate, static_cast<uint8_t>(gate % 4));
    assert(controls.size() == count);
    assert(controls.byte_size() == (count + 3) / 4);
    for (size_t byte = 0; byte < controls.byte_size(); ++byte)
    {
      uint8_t expected = 0;
      for (size_t slot = 0; slot < 4 && byte * 4 + slot < count; ++slot)
        expected |= static_cast<uint8_t>(((byte * 4 + slot) % 4) << (slot * 2));
      assert(controls.bytes()[byte] == expected);
    }
    for (size_t gate = 0; gate < count; ++gate)
      for (uint8_t value = 0; value < 4; ++value)
      {
        PackedControls changed = controls;
        changed[gate] = value;
        for (size_t other = 0; other < count; ++other)
          assert(changed[other] == (other == gate ? value : controls[other]));
      }
    if (count % 4 != 0)
      assert((controls.bytes().back() >> (2 * (count % 4))) == 0);
    ExpectException<std::out_of_range>([&]() { controls.get(count); });
    ExpectException<std::out_of_range>([&]() { controls.set(count, OFR_SWAP); });
    ExpectException<std::out_of_range>([&]() { controls.view(count + 1, 0); });
    ExpectException<std::out_of_range>([&]() { controls.view(count, 1); });
  }

  PackedControls controls(12, OFR_STRAIGHT);
  ExpectException<std::invalid_argument>([&]() { controls.set(0, 4); });
  const size_t max = std::numeric_limits<size_t>::max();
  ExpectException<std::out_of_range>([&]() { controls.view(max, 2); });
  ExpectException<std::out_of_range>([&]() { controls.view(1, max); });
  ExpectException<std::out_of_range>([&]() { controls.mutable_view(max, 0); });
  for (size_t offset = 0; offset < 4; ++offset)
  {
    PackedControls shared(12, OFR_COPY_SECOND);
    const PackedControls before = shared;
    PackedControlMutableView view = shared.mutable_view(4 + offset, 3);
    for (size_t gate = 0; gate < 3; ++gate)
      view.set(gate, static_cast<uint8_t>(gate));
    const PackedControlView read = shared.view(4 + offset, 3);
    for (size_t gate = 0; gate < shared.size(); ++gate)
      assert(shared[gate] == (gate >= 4 + offset && gate < 7 + offset
          ? static_cast<uint8_t>(gate - 4 - offset) : before[gate]));
    assert(read.subview(1, 2)[0] == OFR_STRAIGHT);
    assert(read.subview(1, 2)[1] == OFR_SWAP);
    ExpectException<std::out_of_range>([&]() { read[3]; });
    ExpectException<std::out_of_range>([&]() { read.subview(2, max); });
    ExpectException<std::out_of_range>([&]() { view.set(3, OFR_SWAP); });
    PackedControlCursor cursor(read);
    for (uint8_t gate = 0; gate < 3; ++gate) assert(cursor.next() == gate);
    assert(cursor.position() == 3);
    ExpectException<std::out_of_range>([&]() { cursor.next(); });
    ExpectException<std::out_of_range>([&]() { PackedControlCursor invalid(read, 4); });
  }

  controls.resize(5, OFR_SWAP);
  assert(controls.size() == 5 && controls.byte_size() == 2);
  assert((controls.bytes().back() & 0xfcU) == 0);
  controls.resize(9);
  for (size_t gate = 5; gate < 9; ++gate) assert(controls[gate] == OFR_COPY_FIRST);
  controls.resize(13, OFR_SWAP);
  for (size_t gate = 9; gate < 13; ++gate) assert(controls[gate] == OFR_SWAP);
  controls.clear();
  assert(controls.size() == 0 && controls.byte_size() == 0);
  PackedControls source(13);
  for (size_t gate = 0; gate < source.size(); ++gate) source[gate] = gate % 4;
  for (size_t offset = 0; offset < 4; ++offset)
  {
    PackedControls destination(20, OFR_COPY_SECOND);
    destination.copy_from(4 + offset, source.view(1, 7));
    for (size_t gate = 0; gate < destination.size(); ++gate)
      assert(destination[gate] == (gate >= 4 + offset && gate < 11 + offset
          ? source[gate - 3 - offset] : OFR_COPY_SECOND));
    const PackedControls original = source;
    PackedControls backward = source;
    backward.copy_from(3, backward.view(1, 8));
    for (size_t gate = 0; gate < backward.size(); ++gate)
      assert(backward[gate] == (gate >= 3 && gate < 11
          ? original[gate - 2] : original[gate]));
    PackedControls forward = source;
    forward.copy_from(1, forward.view(3, 8));
    for (size_t gate = 0; gate < forward.size(); ++gate)
      assert(forward[gate] == (gate >= 1 && gate < 9
          ? original[gate + 2] : original[gate]));
  }
}

static void CheckPackedCursorRuns()
{
  for (size_t offset = 0; offset < 8; ++offset)
    for (size_t length = 0; length <= 17; ++length)
      for (size_t neighbors : {size_t(0), size_t(3)})
      {
        PackedControls shared(offset + length + neighbors);
        for (size_t gate = 0; gate < shared.size(); ++gate)
          shared[gate] = static_cast<uint8_t>((3 * gate + length + offset) % 4);
        const PackedControls saved = shared;
        const PackedControlView view = shared.view(offset, length);
        for (size_t start = 0; start <= length; ++start)
          for (size_t pattern = 0; pattern < 3; ++pattern)
          {
            PackedControlCursor cursor(view, start);
            size_t position = start;
            while (position < length)
            {
              const bool four = length - position >= 4 &&
                  (pattern == 1 || (pattern == 2 && position % 3 == 1));
              if (four)
              {
                const uint8_t packed = cursor.nextFourUnchecked();
                for (size_t slot = 0; slot < 4; ++slot)
                  assert(((packed >> (2 * slot)) & 3U) == view[position + slot]);
                position += 4;
              }
              else
              {
                const uint8_t control = pattern == 0 || position % 2 == 0
                    ? cursor.nextUnchecked() : cursor.next();
                assert(control == view[position]);
                ++position;
              }
              assert(cursor.position() == position);
            }
            ExpectException<std::out_of_range>([&]() { cursor.next(); });
          }
        assert(shared == saved);
        if (shared.size() % 4 != 0)
          assert((shared.bytes().back() >> (2 * (shared.size() % 4))) == 0);
      }
}

static void CheckPackedFourWrites()
{
  for (size_t offset = 0; offset < 8; ++offset)
    for (size_t index = 0; index < 8; ++index)
      for (size_t neighbors : {size_t(0), size_t(3)})
        for (size_t encoded = 0; encoded < 256; ++encoded)
        {
          const size_t start = offset + index;
          PackedControls shared(start + 4 + neighbors);
          for (size_t gate = 0; gate < shared.size(); ++gate)
            shared[gate] = static_cast<uint8_t>((3 * gate + offset) % 4);
          const PackedControls saved = shared;
          PackedControls expected = shared;
          for (size_t slot = 0; slot < 4; ++slot)
            expected[start + slot] = static_cast<uint8_t>((encoded >> (2 * slot)) & 3U);
          shared.mutable_view(offset, index + 4).set_four_encoded(
              index, static_cast<uint8_t>(encoded));
          assert(shared == expected);
          for (size_t gate = 0; gate < shared.size(); ++gate)
            assert(shared[gate] == (gate >= start && gate < start + 4
                ? static_cast<uint8_t>((encoded >> (2 * (gate - start))) & 3U)
                : saved[gate]));
          if (shared.size() % 4 != 0)
            assert((shared.bytes().back() >> (2 * (shared.size() % 4))) == 0);
        }
  for (size_t offset = 0; offset < 8; ++offset)
  {
    PackedControls shared(offset + 8, OFR_COPY_SECOND);
    const PackedControls saved = shared;
    for (size_t count = 0; count < 4; ++count)
      ExpectException<std::out_of_range>([&]() {
        shared.mutable_view(offset, count).set_four_encoded(0, 0x1b);
      });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 4).set_four_encoded(1, 0x1b);
    });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 4).set_four_encoded(
          std::numeric_limits<size_t>::max(), 0x1b);
    });
    assert(shared == saved);
  }
}

static void CheckPackedWideWrite(size_t gates, uint32_t encoded,
                                 size_t offset, size_t neighbors)
{
  const size_t start = 4 + offset;
  PackedControls shared(start + gates + neighbors);
  for (size_t gate = 0; gate < shared.size(); ++gate)
    shared[gate] = static_cast<uint8_t>((3 * gate + offset) % 4);
  const PackedControls saved = shared;
  PackedControls expected = shared;
  for (size_t slot = 0; slot < gates; ++slot)
    expected[start + slot] = static_cast<uint8_t>((encoded >> (2 * slot)) & 3U);
  PackedControlMutableView view = shared.mutable_view(1 + offset, 3 + gates);
  if (gates == 8)
    view.set_eight_encoded(3, static_cast<uint16_t>(encoded));
  else
    view.set_sixteen_encoded(3, encoded);
  assert(shared == expected);
  for (size_t gate = 0; gate < shared.size(); ++gate)
    assert(shared[gate] == (gate >= start && gate < start + gates
        ? static_cast<uint8_t>((encoded >> (2 * (gate - start))) & 3U)
        : saved[gate]));
  if (shared.size() % 4 != 0)
    assert((shared.bytes().back() >> (2 * (shared.size() % 4))) == 0);
}

static void CheckPackedWideWrites()
{
  for (size_t offset = 0; offset < 4; ++offset)
    for (size_t neighbors : {size_t(0), size_t(3)})
      for (uint32_t encoded = 0; encoded <= 65535U; ++encoded)
        CheckPackedWideWrite(8, encoded, offset, neighbors);

  std::vector<uint32_t> patterns = {
      0U, 0x55555555U, 0xaaaaaaaaU, 0xffffffffU, 0x1b1b1b1bU, 0xe4e4e4e4U};
  for (size_t slot = 0; slot < 16; ++slot)
    for (uint32_t value = 0; value < 4; ++value)
      patterns.push_back((0x55555555U & ~(uint32_t(3) << (2 * slot))) |
                         (value << (2 * slot)));
  std::mt19937 random(20261003);
  for (size_t i = 0; i < 4096; ++i) patterns.push_back(random());
  for (size_t offset = 0; offset < 4; ++offset)
    for (size_t neighbors : {size_t(0), size_t(3)})
      for (uint32_t encoded : patterns)
        CheckPackedWideWrite(16, encoded, offset, neighbors);

  for (size_t offset = 0; offset < 8; ++offset)
  {
    PackedControls shared(offset + 20, OFR_COPY_SECOND);
    const PackedControls saved = shared;
    for (size_t count = 0; count < 8; ++count)
      ExpectException<std::out_of_range>([&]() {
        shared.mutable_view(offset, count).set_eight_encoded(0, 0x1b1b);
      });
    for (size_t count = 0; count < 16; ++count)
      ExpectException<std::out_of_range>([&]() {
        shared.mutable_view(offset, count).set_sixteen_encoded(0, 0x1b1b1b1bU);
      });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 8).set_eight_encoded(1, 0x1b1b);
    });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 16).set_sixteen_encoded(1, 0x1b1b1b1bU);
    });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 8).set_eight_encoded(
          std::numeric_limits<size_t>::max(), 0x1b1b);
    });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 16).set_sixteen_encoded(
          std::numeric_limits<size_t>::max(), 0x1b1b1b1bU);
    });
    assert(shared == saved);
  }
}

static void CheckPackedTwoWrites()
{
  for (size_t offset = 0; offset < 4; ++offset)
    for (size_t neighbors : {size_t(0), size_t(3)})
      for (uint8_t encoded = 0; encoded < 16; ++encoded)
      {
        const size_t start = 4 + offset;
        PackedControls shared(start + 2 + neighbors, OFR_COPY_SECOND);
        PackedControls expected = shared;
        expected[start] = encoded & 3U;
        expected[start + 1] = encoded >> 2U;
        shared.mutable_view(1 + offset, 5).set_two_encoded(3, encoded);
        assert(shared == expected);
        if (shared.size() % 4 != 0)
          assert((shared.bytes().back() >> (2 * (shared.size() % 4))) == 0);
      }
  for (size_t offset = 0; offset < 8; ++offset)
  {
    PackedControls shared(offset + 8, OFR_SWAP);
    const PackedControls saved = shared;
    for (size_t count = 0; count < 2; ++count)
      ExpectException<std::out_of_range>([&]() {
        shared.mutable_view(offset, count).set_two_encoded(0, 7);
      });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 2).set_two_encoded(1, 7);
    });
    ExpectException<std::out_of_range>([&]() {
      shared.mutable_view(offset, 2).set_two_encoded(
          std::numeric_limits<size_t>::max(), 7);
    });
    for (size_t encoded = 16; encoded < 256; ++encoded)
      ExpectException<std::invalid_argument>([&]() {
        shared.mutable_view(offset, 2).set_two_encoded(0, static_cast<uint8_t>(encoded));
      });
    assert(shared == saved);
  }
}

static void CheckPackedPreparedSpans()
{
  const uint8_t pairs[4][2] = {
      {OFR_TAG_BOTH, OFR_TAG_ZERO}, {OFR_TAG_LEFT, OFR_TAG_RIGHT},
      {OFR_TAG_RIGHT, OFR_TAG_LEFT}, {OFR_TAG_ZERO, OFR_TAG_BOTH}};
  for (size_t offset = 0; offset < 4; ++offset)
    for (uint8_t gate = 0; gate < 4; ++gate)
    {
      const std::vector<uint8_t> tags(pairs[gate], pairs[gate] + 2);
      const PackedControls tape = OFRControl(tags, 2, 1, 1);
      assert(tape.size() == 1 && tape[0] == gate && tape.byte_size() == 1);
      PackedControls shared(4 + offset + tape.size() + 4, OFR_STRAIGHT);
      const PackedControls before = shared;
      const size_t start = 4 + offset;
      shared.copy_from(start, tape.view());
      const PackedControlView span = shared.view(start, tape.size());
      assert(OFRPostOrderControls(span, 2, 1, 1) == tape);
      const std::vector<unsigned char> original = Records(2, 8);
      std::vector<unsigned char> dfs = original;
      std::vector<unsigned char> postorder = original;
      OFRApplyPreparedInPlace(dfs.data(), span, 2, 1, 1, 8);
      OFRApplyPreparedPostOrderInPlace(postorder.data(), span, 2, 1, 1, 8);
      assert(postorder == dfs);
      for (size_t neighbor = 0; neighbor < shared.size(); ++neighbor)
        if (neighbor != start) assert(shared[neighbor] == before[neighbor]);
      ExpectException<std::length_error>([&]() {
        OFRPostOrderControls(shared.view(start, 0), 2, 1, 1);
      });
      ExpectException<std::exception>([&]() {
        OFRApplyPreparedInPlace(dfs.data(), shared.view(start, 0), 2, 1, 1, 8);
      });
      ExpectException<std::exception>([&]() {
        OFRApplyPreparedPostOrderInPlace(
            dfs.data(), shared.view(start, 0), 2, 1, 1, 8);
      });
    }
}

static std::vector<unsigned char> Records(size_t n, size_t width)
{
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    for (size_t byte = 0; byte < width; ++byte)
      data[i * width + byte] = static_cast<unsigned char>(i * 37 + byte);
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(data.data() + i * width, &id, sizeof(id));
  }
  return data;
}

static std::vector<uint32_t> Ids(const std::vector<unsigned char> &data,
                                 size_t width)
{
  assert(data.size() % width == 0);
  std::vector<uint32_t> ids(data.size() / width);
  for (size_t i = 0; i < ids.size(); ++i)
    std::memcpy(&ids[i], data.data() + i * width, sizeof(ids[i]));
  std::sort(ids.begin(), ids.end());
  return ids;
}

static std::vector<uint32_t> Expected(const std::vector<uint8_t> &tags,
                                      bool left)
{
  std::vector<uint32_t> ids;
  for (size_t i = 0; i < tags.size(); ++i)
  {
    const bool selected = left ? ((tags[i] >> 1U) & 1U) != 0
                               : (tags[i] & 1U) != 0;
    if (selected)
      ids.push_back(static_cast<uint32_t>(i));
  }
  return ids;
}

static void CheckNormalizationPaths(const std::vector<uint8_t> &tags,
                                    size_t n_left, size_t n_right,
                                    const std::vector<uint8_t> &normalized)
{
  size_t left_weight = 0, right_weight = 0;
  for (uint8_t tag : tags)
  {
    left_weight += (tag >> 1U) & 1U;
    right_weight += tag & 1U;
  }
  // Preserve all original responsibilities; assign unused records in input
  // order to the remaining left capacity, then the remaining right capacity.
  std::vector<uint8_t> expected(tags);
  size_t left_padding = n_left - left_weight;
  size_t right_padding = n_right - right_weight;
  for (uint8_t &tag : expected)
    if (tag == OFR_TAG_ZERO)
    {
      if (left_padding != 0) { tag = OFR_TAG_LEFT; --left_padding; }
      else if (right_padding != 0) { tag = OFR_TAG_RIGHT; --right_padding; }
    }
  assert(left_padding == 0 && right_padding == 0);
  assert(normalized == expected);
  std::vector<uint8_t> in_place(tags);
  const uint8_t *in_place_storage = in_place.data();
  OFRNormalizeInPlace(in_place, tags.size(), n_left, n_right);
  assert(in_place == normalized && in_place.data() == in_place_storage);
  std::vector<uint8_t> owned(tags);
  const uint8_t *owned_storage = owned.data();
  detail::OFRNormalizeOwnedInPlace(owned, tags.size(), n_left, n_right,
                                  left_weight, right_weight);
  assert(owned == normalized && owned.data() == owned_storage);
}

static void CheckNormalizationErrors()
{
  std::vector<uint8_t> empty;
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(empty, 0, 0, 0);
  });
  std::vector<uint8_t> short_tags(1, OFR_TAG_ZERO);
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(short_tags, 2, 1, 1);
  });
  std::vector<uint8_t> invalid_tag = {4, OFR_TAG_ZERO};
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(invalid_tag, 2, 1, 1);
  });
  std::vector<uint8_t> excessive_left(2, OFR_TAG_LEFT);
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(excessive_left, 2, 1, 1);
  });
  std::vector<uint8_t> excessive_right(2, OFR_TAG_RIGHT);
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(excessive_right, 2, 2, 0);
  });
  std::vector<uint8_t> tags(2, OFR_TAG_ZERO);
  ExpectException<std::invalid_argument>([&]() {
    OFRNormalizeInPlace(tags, 2, 1, 0);
  });
  ExpectException<std::invalid_argument>([&]() {
    detail::OFRNormalizeOwnedInPlace(tags, 2, 1, 1, 2, 0);
  });
  ExpectException<std::invalid_argument>([&]() {
    detail::OFRNormalizeOwnedInPlace(short_tags, 2, 1, 1, 0, 0);
  });
}

static void CheckOutput(const OFRDataResult &output,
                        const std::vector<uint8_t> &normalized,
                        size_t width)
{
  assert(Ids(output.left, width) == Expected(normalized, true));
  assert(Ids(output.right, width) == Expected(normalized, false));
}

static void Verify(const std::vector<uint8_t> &tags,
                   size_t n_left,
                   size_t n_right,
                   size_t width,
                   bool check_compact)
{
  const size_t n = tags.size();
  const std::vector<uint8_t> normalized = OFRNormalize(
      tags, n, n_left, n_right);
  CheckNormalizationPaths(tags, n_left, n_right, normalized);
  const size_t control_count = OFRControlCount(n, n_left, n_right);
  const PackedControls controls = OFRControl(
      tags, n, n_left, n_right);
  assert(controls.size() == control_count);
  assert(controls.byte_size() == (control_count + 3) / 4);

  const size_t offset = 3 + (exact_cases + normalize_cases) % 5;
  PackedControls shared(offset + control_count + 4, OFR_COPY_SECOND);
  const PackedControls before = shared;
  const size_t next = OFRControlWrite(normalized,
                                      shared,
                                      n,
                                      n_left,
                                      n_right,
                                      offset);
  assert(next == offset + control_count);
  for (size_t i = 0; i < shared.size(); ++i)
    if (i < offset || i >= next) assert(shared[i] == before[i]);
  assert(SameControls(controls, shared, offset));

  const std::vector<unsigned char> data = Records(n, width);
  const std::vector<unsigned char> saved = data;
  const OFRDataResult applied = OFRApply(
      data.data(), controls, n, n_left, n_right, width);
  CheckOutput(applied, normalized, width);
  assert(data == saved);

  const OFRControlReadResult read = OFRControlRead(
      data.data(), shared, n, n_left, n_right, width, offset);
  assert(read.next_pos == next);
  assert(read.left == applied.left && read.right == applied.right);

  if (check_compact)
  {
    std::vector<unsigned char> in_place = data;
    OFRApplyInPlace(in_place.data(), controls,
                    n, n_left, n_right, width);
    assert(std::equal(applied.left.begin(), applied.left.end(),
                      in_place.begin()));
    assert(std::equal(applied.right.begin(), applied.right.end(),
                      in_place.begin() + applied.left.size()));

    std::vector<unsigned char> prepared = data;
    OFRApplyPreparedInPlace(prepared.data(), controls,
                            n, n_left, n_right, width);
    assert(prepared == in_place);

    if (n >= 2 && (n & (n - 1)) == 0 && n_left == n / 2)
    {
      std::vector<unsigned char> level_ordered = data;
      const PackedControls level_controls =
          OFRLevelOrderControls(controls, n, n_left, n_right);
      OFRApplyLevelOrderedInPlace(level_ordered.data(), level_controls,
                                  n, n_left, n_right, width);
      assert(level_ordered == in_place);

      std::vector<unsigned char> prepared_level = data;
      OFRApplyPreparedLevelOrderedInPlace(
          prepared_level.data(), level_controls,
          n, n_left, n_right, width);
      assert(prepared_level == level_ordered);

      const PackedControls postorder_controls =
          OFRPostOrderControls(controls, n, n_left, n_right);
      assert(postorder_controls.size() == controls.size());
      const PackedControls direct_postorder =
          OFRControlPostOrder(tags, n, n_left, n_right);
      assert(direct_postorder == postorder_controls);
      std::vector<unsigned char> postordered = data;
      OFRApplyPostOrderInPlace(postordered.data(), postorder_controls,
                               n, n_left, n_right, width);
      assert(postordered == in_place);

      std::vector<unsigned char> prepared_postorder = data;
      OFRApplyPreparedPostOrderInPlace(
          prepared_postorder.data(), postorder_controls,
          n, n_left, n_right, width);
      assert(prepared_postorder == in_place);
    }

    const OFRDataResult compacted = OFRCompact(
        data.data(), tags, n, n_left, n_right, width);
    assert(compacted.left == applied.left && compacted.right == applied.right);
  }
}

static size_t Popcount(size_t value)
{
  size_t count = 0;
  while (value != 0)
  {
    count += value & 1U;
    value >>= 1U;
  }
  return count;
}

static void ExhaustiveExact()
{
  for (size_t n = 1; n <= 10; ++n)
  {
    const size_t limit = size_t(1) << n;
    for (size_t n_left = 0; n_left <= n; ++n_left)
    {
      const size_t n_right = n - n_left;
      for (size_t left_mask = 0; left_mask < limit; ++left_mask)
      {
        if (Popcount(left_mask) != n_left)
          continue;
        for (size_t right_mask = 0; right_mask < limit; ++right_mask)
        {
          if (Popcount(right_mask) != n_right)
            continue;
          std::vector<uint8_t> tags(n);
          for (size_t i = 0; i < n; ++i)
          {
            tags[i] = static_cast<uint8_t>(
                (((left_mask >> i) & 1U) << 1U) |
                ((right_mask >> i) & 1U));
          }
          Verify(tags, n_left, n_right, 8, false);
          ++exact_cases;
        }
      }
    }
  }
}

static void ExhaustiveNormalize()
{
  size_t power = 4;
  for (size_t n = 1; n <= 7; ++n, power *= 4)
  {
    for (size_t encoded = 0; encoded < power; ++encoded)
    {
      size_t value = encoded;
      size_t left_weight = 0;
      size_t right_weight = 0;
      std::vector<uint8_t> tags(n);
      for (size_t i = 0; i < n; ++i)
      {
        tags[i] = static_cast<uint8_t>(value & 3U);
        value >>= 2U;
        left_weight += (tags[i] >> 1U) & 1U;
        right_weight += tags[i] & 1U;
      }
      for (size_t n_left = left_weight;
           n_left <= n - right_weight;
           ++n_left)
      {
        Verify(tags, n_left, n - n_left, 8, true);
        ++normalize_cases;
      }
    }
  }
}

static void RandomLarge()
{
  std::mt19937 random(20260922);
  const size_t lengths[] = {11, 16, 17, 31, 32, 33, 63, 64, 65, 127, 129, 1024, 1025};
  const size_t widths[] = {4, 8, 12, 16, 24, 32, 40, 64};
  for (size_t n : lengths)
  {
    for (size_t trial = 0; trial < 20; ++trial)
    {
      const size_t n_left = random() % (n + 1);
      const size_t n_right = n - n_left;
      std::vector<size_t> order(n);
      for (size_t i = 0; i < n; ++i)
        order[i] = i;
      std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
      std::shuffle(order.begin(), order.end(), random);
      for (size_t i = 0; i < n_left; ++i)
        tags[order[i]] = static_cast<uint8_t>(tags[order[i]] | OFR_TAG_LEFT);
      std::shuffle(order.begin(), order.end(), random);
      for (size_t i = 0; i < n_right; ++i)
        tags[order[i]] = static_cast<uint8_t>(tags[order[i]] | OFR_TAG_RIGHT);
      Verify(tags,
             n_left,
             n_right,
             widths[trial % (sizeof(widths) / sizeof(widths[0]))],
             trial == 0);
    }
  }
}

static void BalancedPostOrder()
{
  std::mt19937 random(20260925);
  const size_t lengths[] = {2, 4, 8, 16, 32, 64, 256, 1024};
  const size_t widths[] = {4, 7, 8, 12, 16, 24, 40, 64};
  for (size_t n : lengths)
  {
    for (size_t width : widths)
    {
      std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
      for (size_t i = 0; i < n / 4; ++i)
      {
        tags[i] = OFR_TAG_BOTH;
        tags[n / 4 + i] = OFR_TAG_LEFT;
        tags[n / 2 + i] = OFR_TAG_RIGHT;
      }
      std::shuffle(tags.begin(), tags.end(), random);
      Verify(tags, n / 2, n / 2, width, true);
    }
  }
}

static void CheckWordReplay()
{
  for (size_t n : {size_t(2), size_t(5), size_t(16), size_t(33), size_t(65)})
    for (size_t words : {size_t(1), size_t(2), size_t(3)})
    {
      const size_t left = n / 2;
      std::vector<uint8_t> tags(n, OFR_TAG_RIGHT);
      for (size_t i = 0; i < left; ++i) tags[(i * 7) % n] = OFR_TAG_LEFT;
      // For lengths sharing factors with seven, use a permutation instead.
      if (n % 7 == 0)
      {
        std::fill(tags.begin(), tags.end(), OFR_TAG_RIGHT);
        for (size_t i = 0; i < left; ++i) tags[i] = OFR_TAG_LEFT;
      }
      const PackedControls tape = OFRControl(tags, n, left, n - left);
      PackedControls shared(3 + tape.size() + 2, OFR_COPY_SECOND);
      shared.copy_from(3, tape.view());
      std::vector<size_t> routed(n * words);
      for (size_t i = 0; i < routed.size(); ++i)
        routed[i] = i * size_t(0x9e3779b9U) ^ (i + 17);
      const std::vector<size_t> original = routed;
      OFRApplyWordsInPlace(routed.data(), words, shared, 3, n, left, n - left);
      const OFRControlReadResult expected = OFRControlRead(
          reinterpret_cast<const unsigned char *>(original.data()), shared,
          n, left, n - left, words * sizeof(size_t), 3);
      assert(expected.next_pos == 3 + tape.size());
      assert(std::memcmp(routed.data(), expected.left.data(),
                         expected.left.size()) == 0);
      assert(std::memcmp(routed.data() + left * words,
                         expected.right.data(), expected.right.size()) == 0);
    }
}

static void CheckFusedWordRouting()
{
  std::mt19937_64 random(20261001);
  // Independent writing plus replay remains the reference for the fused
  // writer, including tags mutated by balancing and every membership word.
  for (size_t n = 1; n <= 33; ++n)
    for (size_t left = 0; left <= n; ++left)
      for (size_t underfilled = 0; underfilled < 2; ++underfilled)
      {
        const size_t right = n - left;
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = i;
        std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
        std::shuffle(order.begin(), order.end(), random);
        for (size_t i = 0; i < left; ++i) tags[order[i]] |= OFR_TAG_LEFT;
        std::shuffle(order.begin(), order.end(), random);
        for (size_t i = 0; i < right; ++i) tags[order[i]] |= OFR_TAG_RIGHT;
        if (underfilled)
          for (uint8_t &tag : tags) tag &= static_cast<uint8_t>(random() & 3U);
        const std::vector<uint8_t> normalized = OFRNormalize(tags, n, left, right);
        const size_t count = OFRControlCount(n, left, right);
        for (size_t words : {size_t(1), size_t(2), size_t(3), size_t(9)})
          for (size_t offset = 0; offset < 8; ++offset)
          {
            const size_t guard = 3;
            std::vector<size_t> initial(guard + n * words + guard);
            for (size_t &word : initial) word = static_cast<size_t>(random());
            PackedControls expected_controls(offset + count + 5);
            for (size_t gate = 0; gate < expected_controls.size(); ++gate)
              expected_controls[gate] = static_cast<uint8_t>(gate % 4);
            const PackedControls controls_before = expected_controls;
            PackedControls fused_controls = expected_controls;
            std::vector<uint8_t> expected_tags = normalized;
            std::vector<uint8_t> fused_tags = normalized;
            std::vector<size_t> expected_rows = initial;
            std::vector<size_t> fused_rows = initial;
            const size_t expected_end = OFRControlWrite(
                expected_tags, expected_controls, n, left, right, offset);
            OFRApplyWordsInPlace(expected_rows.data() + guard, words,
                                 expected_controls, offset, n, left, right);
            const size_t fused_end = OFRControlWriteAndRoute(
                fused_tags, fused_controls, fused_rows.data() + guard,
                words, n, left, right, offset);
            assert(fused_end == expected_end && fused_end == offset + count);
            assert(fused_controls == expected_controls);
            assert(fused_tags == expected_tags);
            assert(fused_rows == expected_rows);
            for (size_t gate = 0; gate < fused_controls.size(); ++gate)
              if (gate < offset || gate >= fused_end)
                assert(fused_controls[gate] == controls_before[gate]);
            for (size_t word = 0; word < guard; ++word)
            {
              assert(fused_rows[word] == initial[word]);
              assert(fused_rows[guard + n * words + word] ==
                     initial[guard + n * words + word]);
            }
            ++fused_cases;
          }
      }

  std::vector<uint8_t> tags = {OFR_TAG_LEFT, OFR_TAG_RIGHT};
  PackedControls controls(1);
  size_t rows[2] = {7, 11};
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWriteAndRoute(tags, controls, NULL, 1, 2, 1, 1, 0);
  });
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWriteAndRoute(tags, controls, rows, 0, 2, 1, 1, 0);
  });
  PackedControls short_controls;
  ExpectException<std::length_error>([&]() {
    OFRControlWriteAndRoute(tags, short_controls, rows, 1, 2, 1, 1, 0);
  });
  ExpectException<std::length_error>([&]() {
    OFRControlWriteAndRoute(tags, controls, rows, 1, 2, 1, 1,
                            std::numeric_limits<size_t>::max());
  });
  std::vector<uint8_t> invalid_tags(2, OFR_TAG_BOTH);
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWriteAndRoute(invalid_tags, controls, rows, 1, 2, 1, 1, 0);
  });
}

static void CheckTiledPostOrderRouting()
{
  std::mt19937_64 random(20261002);
  // Small networks exercise partial tiles; larger residues cross the 256-row
  // cap. Wide membership rows lower the public working-set tile size.
  for (size_t n = 2; n <= 8192; n *= 2)
    for (size_t underfilled = 0; underfilled < 2; ++underfilled)
    {
      const size_t half = n / 2;
      std::vector<size_t> order(n);
      for (size_t i = 0; i < n; ++i) order[i] = i;
      std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
      std::shuffle(order.begin(), order.end(), random);
      for (size_t i = 0; i < half; ++i) tags[order[i]] |= OFR_TAG_LEFT;
      std::shuffle(order.begin(), order.end(), random);
      for (size_t i = 0; i < half; ++i) tags[order[i]] |= OFR_TAG_RIGHT;
      if (underfilled)
        for (uint8_t &tag : tags) tag &= static_cast<uint8_t>(random() & 3U);
      const std::vector<uint8_t> normalized = OFRNormalize(tags, n, half, half);
      const size_t count = OFRControlCount(n, half, half);
      PackedControls dfs(count);
      std::vector<uint8_t> expected_tags = normalized;
      assert(OFRControlWrite(expected_tags, dfs, n, half, half, 0) == count);
      const PackedControls postorder = OFRPostOrderControls(dfs, n, half, half);
      assert(OFRControlPostOrder(tags, n, half, half) == postorder);
      for (size_t words : {size_t(1), size_t(2), size_t(3), size_t(8), size_t(17)})
        for (size_t offset = 0; offset < 8; ++offset)
        {
          const size_t guard = 3;
          std::vector<size_t> initial(guard + n * words + guard);
          for (size_t &word : initial) word = static_cast<size_t>(random());
          std::vector<size_t> expected_rows = initial;
          OFRApplyWordsInPlace(expected_rows.data() + guard, words, dfs, 0,
                               n, half, half);
          PackedControls shared(offset + count + 5);
          for (size_t gate = 0; gate < shared.size(); ++gate)
            shared[gate] = static_cast<uint8_t>(gate % 4);
          const PackedControls before = shared;
          PackedControls expected_controls = shared;
          expected_controls.copy_from(offset, postorder.view());
          std::vector<uint8_t> work_tags = normalized;
          std::vector<size_t> work_rows = initial;
          const size_t end = OFRControlWritePostOrderAndRoute(
              work_tags, shared, work_rows.data() + guard, words,
              n, half, half, offset);
          assert(end == offset + count);
          assert(shared == expected_controls);
          assert(work_tags == expected_tags);
          assert(work_rows == expected_rows);
          for (size_t gate = 0; gate < shared.size(); ++gate)
            if (gate < offset || gate >= end) assert(shared[gate] == before[gate]);
          for (size_t word = 0; word < guard; ++word)
          {
            assert(work_rows[word] == initial[word]);
            assert(work_rows[guard + n * words + word] ==
                   initial[guard + n * words + word]);
          }
          ++tiled_cases;
        }
    }

  std::vector<uint8_t> tags = {OFR_TAG_LEFT, OFR_TAG_RIGHT};
  PackedControls controls(1);
  size_t rows[6] = {0, 1, 2, 3, 4, 5};
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWritePostOrderAndRoute(tags, controls, NULL, 1, 2, 1, 1, 0);
  });
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWritePostOrderAndRoute(tags, controls, rows, 0, 2, 1, 1, 0);
  });
  PackedControls short_controls;
  ExpectException<std::length_error>([&]() {
    OFRControlWritePostOrderAndRoute(tags, short_controls, rows, 1, 2, 1, 1, 0);
  });
  ExpectException<std::length_error>([&]() {
    OFRControlWritePostOrderAndRoute(tags, controls, rows, 1, 2, 1, 1,
                                     std::numeric_limits<size_t>::max());
  });
  std::vector<uint8_t> unbalanced = {OFR_TAG_LEFT, OFR_TAG_RIGHT, OFR_TAG_RIGHT};
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWritePostOrderAndRoute(unbalanced, controls, rows, 1, 3, 1, 2, 0);
  });
  std::vector<uint8_t> non_power(6, OFR_TAG_RIGHT);
  for (size_t i = 0; i < 3; ++i) non_power[i] = OFR_TAG_LEFT;
  ExpectException<std::invalid_argument>([&]() {
    OFRControlWritePostOrderAndRoute(non_power, controls, rows, 1, 6, 3, 3, 0);
  });
}

static void CheckNarrowCounterBoundaries()
{
  const size_t lengths[] = {4096, 524288};
  const size_t blocks[] = {16, 8};
  for (size_t shape = 0; shape < 2; ++shape)
    for (size_t variable = 0; variable < 2; ++variable)
    {
      const size_t n = lengths[shape], block = blocks[shape], half = n / 2;
      std::vector<uint8_t> tags(n);
      for (size_t row = 0; row < n; ++row)
      {
        const bool second = (row / block) % 2 != 0;
        tags[row] = variable
            ? (second ? OFR_TAG_BOTH : OFR_TAG_ZERO)
            : (second ? OFR_TAG_LEFT : OFR_TAG_RIGHT);
      }
      // All early layers are fixed. At stride 16/8 the independent views
      // reach length 256/65536 with every gate diagonal or variable: the
      // maximum counters are 128/32768, including the final rank increment.
      std::vector<uint8_t> expected_tags = tags;
      const size_t count = OFRControlCount(n, half, half);
      PackedControls dfs(count);
      assert(OFRControlWrite(expected_tags, dfs, n, half, half, 0) == count);
      const PackedControls postorder = OFRPostOrderControls(dfs, n, half, half);
      const size_t offset = 1, guard = 3;
      PackedControls actual(count + offset + 3, OFR_COPY_SECOND);
      PackedControls expected_controls = actual;
      expected_controls.copy_from(offset, postorder.view());
      std::vector<size_t> initial(n + 2 * guard);
      for (size_t row = 0; row < initial.size(); ++row)
        initial[row] = row * size_t(0x9e3779b97f4a7c15ULL) ^ (row + 19);
      std::vector<size_t> expected_rows = initial;
      OFRApplyWordsInPlace(expected_rows.data() + guard, 1, dfs, 0,
                           n, half, half);
      std::vector<size_t> actual_rows = initial;
      assert(OFRControlWritePostOrderAndRoute(
          tags, actual, actual_rows.data() + guard, 1,
          n, half, half, offset) == offset + count);
      assert(actual == expected_controls);
      assert(tags == expected_tags);
      assert(actual_rows == expected_rows);
      for (size_t row = 0; row < guard; ++row)
      {
        assert(actual_rows[row] == initial[row]);
        assert(actual_rows[n + guard + row] == initial[n + guard + row]);
      }
      ++narrow_boundary_cases;
    }
}

static void CheckMembershipByteOverflow()
{
  const size_t n = 2, max = std::numeric_limits<size_t>::max();
  const size_t words = max / (n * sizeof(size_t)) + 1;
  // The number of words itself fits; only converting the whole membership
  // span to bytes overflows. The small real buffers must never be inspected.
  assert(words <= max / n);
  assert(n * words > max / sizeof(size_t));
  const uint8_t pairs[2][2] = {
      {OFR_TAG_LEFT, OFR_TAG_RIGHT}, {OFR_TAG_BOTH, OFR_TAG_ZERO}};
  for (size_t pair = 0; pair < 2; ++pair)
    for (size_t api = 0; api < 3; ++api)
    {
      std::vector<uint8_t> tags(pairs[pair], pairs[pair] + 2);
      const std::vector<uint8_t> saved_tags = tags;
      PackedControls controls(3, OFR_COPY_SECOND);
      const PackedControls saved_controls = controls;
      std::vector<size_t> rows = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};
      const std::vector<size_t> saved_rows = rows;
      bool rejected = false;
      try
      {
        if (api == 0)
          OFRControlWriteAndRoute(tags, controls, rows.data() + 1,
                                  words, n, 1, 1, 1);
        else if (api == 1)
          OFRControlWritePostOrderAndRoute(tags, controls, rows.data() + 1,
                                           words, n, 1, 1, 1);
        else
          OFRApplyWordsInPlace(rows.data() + 1, words, controls, 1, n, 1, 1);
      }
      catch (const std::invalid_argument &error)
      {
        assert(std::strcmp(error.what(), "Invalid OFR membership dimensions") == 0);
        rejected = true;
      }
      assert(rejected);
      assert(tags == saved_tags && controls == saved_controls && rows == saved_rows);
      ++membership_byte_overflow_cases;
    }
}

static size_t CheckEveryBalanceLayer(std::vector<uint8_t> &tags,
                                     PackedControls &controls,
                                     size_t base, size_t stride, size_t n,
                                     size_t n_left, size_t n_right,
                                     size_t position)
{
  if (n_left == 0 || n_right == 0)
    return position;
  if (n == 2)
    return OFRControlWrite(tags, controls, base, stride, n,
                           n_left, n_right, position);

  position = OFRBalanceInPlace(tags, controls, base, stride, n,
                               n_left, n_right, position);
  const size_t top_n = n / 2 + n % 2;
  const size_t top_left = n_left / 2 + n_left % 2;
  const size_t top_right = top_n - top_left;
  size_t actual_top_left = 0, actual_top_right = 0;
  size_t actual_bottom_left = 0, actual_bottom_right = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const uint8_t tag = tags[base + i * stride];
    if (i % 2 == 0)
    {
      actual_top_left += (tag >> 1U) & 1U;
      actual_top_right += tag & 1U;
    }
    else
    {
      actual_bottom_left += (tag >> 1U) & 1U;
      actual_bottom_right += tag & 1U;
    }
  }
  assert(actual_top_left == top_left && actual_top_right == top_right);
  assert(actual_bottom_left == n_left - top_left);
  assert(actual_bottom_right == n_right - top_right);
  position = CheckEveryBalanceLayer(tags, controls, base, stride * 2,
                                     top_n, top_left, top_right, position);
  return CheckEveryBalanceLayer(tags, controls, base + stride, stride * 2,
                                 n / 2, n_left - top_left,
                                 n_right - top_right, position);
}

static void CheckInPlaceViews()
{
  std::mt19937 random(20260928);
  for (size_t n = 3; n <= 33; ++n)
    for (size_t n_left = 1; n_left < n; ++n_left)
      for (size_t trial = 0; trial < 4; ++trial)
      {
        const size_t n_right = n - n_left;
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = i;
        std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
        std::shuffle(order.begin(), order.end(), random);
        for (size_t i = 0; i < n_left; ++i)
          tags[order[i]] |= OFR_TAG_LEFT;
        std::shuffle(order.begin(), order.end(), random);
        for (size_t i = 0; i < n_right; ++i)
          tags[order[i]] |= OFR_TAG_RIGHT;

        const size_t base = 2, stride = 3, offset = 5;
        std::vector<uint8_t> view(base + n * stride + 2, 0xa5);
        for (size_t i = 0; i < n; ++i) view[base + i * stride] = tags[i];
        const std::vector<uint8_t> original = view;
        const PackedControls expected =
            OFRControl(tags, n, n_left, n_right);
        PackedControls controls(offset + expected.size() + 3, OFR_STRAIGHT);
        const size_t end = OFRControlWrite(
            view, controls, base, stride, n, n_left, n_right, offset);
        assert(end == offset + expected.size());
        assert(SameControls(expected, controls, offset));
        for (size_t i = 0; i < view.size(); ++i)
          if (i < base || (i - base) % stride != 0 ||
              (i - base) / stride >= n)
            assert(view[i] == original[i]);
        for (size_t i = 0; i < controls.size(); ++i)
          if (i < offset || i >= end) assert(controls[i] == OFR_STRAIGHT);

        view = original;
        PackedControls layer(offset + n / 2 + 3, OFR_STRAIGHT);
        const size_t layer_end = OFRBalanceInPlace(
            view, layer, base, stride, n, n_left, n_right, offset);
        assert(layer_end == offset + n / 2);
        const OFRBalanceResult legacy = OFRBalance(tags, n, n_left, n_right);
        assert(SameControls(legacy.controls, layer, offset));
        size_t top_left = 0, top_right = 0, bottom_left = 0, bottom_right = 0;
        for (size_t i = 0; i < n; ++i)
        {
          const uint8_t tag = view[base + i * stride];
          if (i % 2 == 0)
          {
            assert(tag == legacy.top_tags[i / 2]);
            top_left += (tag >> 1U) & 1U;
            top_right += tag & 1U;
          }
          else
          {
            assert(tag == legacy.bottom_tags[i / 2]);
            bottom_left += (tag >> 1U) & 1U;
            bottom_right += tag & 1U;
          }
        }
        assert(top_left == (n_left + 1) / 2);
        assert(top_right == (n + 1) / 2 - top_left);
        assert(bottom_left == n_left - top_left);
        assert(bottom_right == n_right - top_right);
        for (size_t gate = 0; gate < n / 2; ++gate)
        {
          const uint8_t x = tags[2 * gate];
          const uint8_t y = tags[2 * gate + 1];
          const uint8_t top = view[base + 2 * gate * stride];
          const uint8_t bottom = view[base + (2 * gate + 1) * stride];
          assert(layer[offset + gate] == OForkControl(x, y, top, bottom));
        }

        view = original;
        controls = PackedControls(controls.size(), OFR_STRAIGHT);
        const size_t checked_end = CheckEveryBalanceLayer(
            view, controls, base, stride, n, n_left, n_right, offset);
        assert(checked_end == end);
        assert(SameControls(expected, controls, offset));
      }
}

static void CheckHelpersAndErrors()
{
  size_t pair_index = 0;
  const uint8_t tag_order[] = {
      OFR_TAG_ZERO, OFR_TAG_LEFT, OFR_TAG_RIGHT, OFR_TAG_BOTH};
  for (size_t x_index = 0; x_index < 4; ++x_index)
  {
    for (size_t y_index = x_index; y_index < 4; ++y_index, ++pair_index)
    {
      const uint8_t x = tag_order[x_index];
      const uint8_t y = tag_order[y_index];
      const std::array<uint8_t, 10> pair = OFRPairMask(x, y);
      assert(std::count(pair.begin(), pair.end(), uint8_t(1)) == 1);
      assert(pair[pair_index] == 1);
    }
  }

  assert(OFRControlCount(3, 2, 1) == 2);
  assert(OFRControlCount(4, 1, 3) == 3);
  assert(OFRControlCount(4, 2, 2) == 4);
  assert(OFRControlCount(6, 3, 3) == 7);
  assert(OFRControlCount(9, 3, 6) == 12);
  assert(OFRControlCount(12, 6, 6) == 20);
  assert(OFRControlCount(1024, 0, 1024) == 0);

  bool threw = false;
  try
  {
    OFRControlCount(0, 0, 0);
  }
  catch (const std::invalid_argument &)
  {
    threw = true;
  }
  assert(threw);

  threw = false;
  try
  {
    OFRControl(std::vector<uint8_t>(3, OFR_TAG_BOTH), 3, 1, 2);
  }
  catch (const std::invalid_argument &)
  {
    threw = true;
  }
  assert(threw);
}

static void CheckPublishedExamples()
{
  {
    // A-L; left is ABC U ADE, right is AFG U FHI. The two padding records
    // follow OFRNormalize's deterministic zero-tag assignment.
    const size_t n = 12;
    std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
    for (size_t id : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(4)})
      tags[id] = static_cast<uint8_t>(tags[id] | OFR_TAG_LEFT);
    for (size_t id : {size_t(0), size_t(5), size_t(6), size_t(7), size_t(8)})
      tags[id] = static_cast<uint8_t>(tags[id] | OFR_TAG_RIGHT);
    const OFRDataResult output = OFRCompact(
        Records(n, 4).data(), tags, n, 6, 6, 4);
    const std::vector<uint32_t> expected_left = {0, 4, 2, 9, 1, 3};
    const std::vector<uint32_t> expected_right = {0, 8, 6, 5, 10, 7};
    std::vector<uint32_t> sorted_left = expected_left;
    std::vector<uint32_t> sorted_right = expected_right;
    std::sort(sorted_left.begin(), sorted_left.end());
    std::sort(sorted_right.begin(), sorted_right.end());
    assert(Ids(output.left, 4) == sorted_left);
    assert(Ids(output.right, 4) == sorted_right);
  }
  {
    // A-I; left is ACI, right is ADE U FHI.
    const size_t n = 9;
    std::vector<uint8_t> tags(n, OFR_TAG_ZERO);
    for (size_t id : {size_t(0), size_t(2), size_t(8)})
      tags[id] = static_cast<uint8_t>(tags[id] | OFR_TAG_LEFT);
    for (size_t id : {size_t(0), size_t(3), size_t(4), size_t(5), size_t(7), size_t(8)})
      tags[id] = static_cast<uint8_t>(tags[id] | OFR_TAG_RIGHT);
    const OFRDataResult output = OFRCompact(
        Records(n, 4).data(), tags, n, 3, 6, 4);
    const std::vector<uint32_t> expected_left = {8, 0, 2};
    const std::vector<uint32_t> expected_right = {8, 3, 4, 5, 0, 7};
    std::vector<uint32_t> sorted_left = expected_left;
    std::vector<uint32_t> sorted_right = expected_right;
    std::sort(sorted_left.begin(), sorted_left.end());
    std::sort(sorted_right.begin(), sorted_right.end());
    assert(Ids(output.left, 4) == sorted_left);
    assert(Ids(output.right, 4) == sorted_right);
  }
}

int main()
{
  CheckPackedControls();
  CheckPackedCursorRuns();
  CheckPackedFourWrites();
  CheckPackedWideWrites();
  CheckPackedTwoWrites();
  CheckPackedPreparedSpans();
  CheckPackedSpanErrors();
  CheckHelpersAndErrors();
  CheckNormalizationErrors();
  CheckPublishedExamples();
  ExhaustiveExact();
  ExhaustiveNormalize();
  RandomLarge();
  BalancedPostOrder();
  CheckWordReplay();
  CheckFusedWordRouting();
  CheckTiledPostOrderRouting();
  CheckNarrowCounterBoundaries();
  CheckMembershipByteOverflow();
  CheckInPlaceViews();
  std::printf("PASS: %zu exact routings, %zu normalization/capacity cases, "
              "%zu fused word routings, %zu tiled postorder routings, "
              "%zu narrow-counter boundary routings, "
              "%zu membership byte-overflow rejections, "
              "arbitrary lengths, offsets, "
              "one-sided outputs, and large inputs\n",
              exact_cases,
              normalize_cases, fused_cases, tiled_cases, narrow_boundary_cases,
              membership_byte_overflow_cases);
}
