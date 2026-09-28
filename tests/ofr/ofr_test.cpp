#define BEFTS_MODE

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

#include "../../Enclave/SubSample_v2/OFR/helper.cpp"
#include "../../Enclave/SubSample_v2/OFR/OFR.cpp"

using namespace ofr;

static size_t exact_cases = 0;
static size_t normalize_cases = 0;

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
  const size_t control_count = OFRControlCount(n, n_left, n_right);
  const std::vector<uint8_t> controls = OFRControl(
      tags, n, n_left, n_right);
  assert(controls.size() == control_count);

  const size_t offset = 3 + (exact_cases + normalize_cases) % 5;
  std::vector<uint8_t> shared(offset + control_count + 4, OFR_COPY_SECOND);
  const std::vector<uint8_t> before = shared;
  const size_t next = OFRControlWrite(normalized,
                                      shared,
                                      n,
                                      n_left,
                                      n_right,
                                      offset);
  assert(next == offset + control_count);
  assert(std::equal(shared.begin(), shared.begin() + offset, before.begin()));
  assert(std::equal(shared.begin() + next,
                    shared.end(),
                    before.begin() + next));
  assert(std::equal(controls.begin(),
                    controls.end(),
                    shared.begin() + offset));

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
      const std::vector<uint8_t> level_controls =
          OFRLevelOrderControls(controls, n, n_left, n_right);
      OFRApplyLevelOrderedInPlace(level_ordered.data(), level_controls,
                                  n, n_left, n_right, width);
      assert(level_ordered == in_place);

      std::vector<unsigned char> prepared_level = data;
      OFRApplyPreparedLevelOrderedInPlace(
          prepared_level.data(), level_controls,
          n, n_left, n_right, width);
      assert(prepared_level == level_ordered);

      const std::vector<uint8_t> postorder_controls =
          OFRPostOrderControls(controls, n, n_left, n_right);
      assert(postorder_controls.size() == controls.size());
      const std::vector<uint8_t> direct_postorder =
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

static size_t CheckEveryBalanceLayer(std::vector<uint8_t> &tags,
                                     std::vector<uint8_t> &controls,
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
        const std::vector<uint8_t> expected =
            OFRControl(tags, n, n_left, n_right);
        std::vector<uint8_t> controls(offset + expected.size() + 3, 0xa5);
        const size_t end = OFRControlWrite(
            view, controls, base, stride, n, n_left, n_right, offset);
        assert(end == offset + expected.size());
        assert(std::equal(expected.begin(), expected.end(),
                          controls.begin() + offset));
        for (size_t i = 0; i < view.size(); ++i)
          if (i < base || (i - base) % stride != 0 ||
              (i - base) / stride >= n)
            assert(view[i] == original[i]);
        for (size_t i = 0; i < controls.size(); ++i)
          if (i < offset || i >= end) assert(controls[i] == 0xa5);

        view = original;
        std::vector<uint8_t> layer(offset + n / 2 + 3, 0xa5);
        const size_t layer_end = OFRBalanceInPlace(
            view, layer, base, stride, n, n_left, n_right, offset);
        assert(layer_end == offset + n / 2);
        const OFRBalanceResult legacy = OFRBalance(tags, n, n_left, n_right);
        assert(std::equal(legacy.controls.begin(), legacy.controls.end(),
                          layer.begin() + offset));
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
        std::fill(controls.begin(), controls.end(), 0xa5);
        const size_t checked_end = CheckEveryBalanceLayer(
            view, controls, base, stride, n, n_left, n_right, offset);
        assert(checked_end == end);
        assert(std::equal(expected.begin(), expected.end(),
                          controls.begin() + offset));
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
  CheckHelpersAndErrors();
  CheckPublishedExamples();
  ExhaustiveExact();
  ExhaustiveNormalize();
  RandomLarge();
  BalancedPostOrder();
  CheckInPlaceViews();
  std::printf("PASS: %zu exact routings, %zu normalization/capacity cases, "
              "arbitrary lengths, offsets, one-sided outputs, and large inputs\n",
              exact_cases,
              normalize_cases);
}
