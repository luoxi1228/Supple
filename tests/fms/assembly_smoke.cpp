#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../Enclave/SubSample_v2/FMS/FMS.hpp"

thread_local uint64_t OSWAP_COUNTER = 0;

using namespace fms;

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

  const FMSDataResult output = FMSCompact(
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
      FMS_TAG_LEFT, FMS_TAG_BOTH, FMS_TAG_ZERO, FMS_TAG_RIGHT,
      FMS_TAG_BOTH, FMS_TAG_ZERO, FMS_TAG_LEFT, FMS_TAG_RIGHT};
  std::vector<unsigned char> data(n * width);
  for (size_t i = 0; i < n; ++i)
  {
    const uint32_t id = static_cast<uint32_t>(i);
    std::memcpy(data.data() + i * width, &id, sizeof(id));
  }
  const FMSDataResult expected = FMSCompact(
      data.data(), tags, n, n / 2, n / 2, width);
  const std::vector<uint8_t> controls = FMSControl(
      tags, n, n / 2, n / 2);
  const std::vector<uint8_t> level_controls = FMSLevelOrderControls(
      controls, n, n / 2, n / 2);
  const std::vector<FMSOutputSwap> output_swaps = FMSOutputSwaps(
      n, n / 2, n / 2);
  FMSApplyLevelOrderedInPlace(data.data(), level_controls, output_swaps,
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
  const std::vector<uint8_t> postorder_controls = FMSPostOrderControls(
      controls, n, n / 2, n / 2);
  FMSApplyPreparedPostOrderInPlace(
      postordered.data(), postorder_controls, output_swaps,
      n, n / 2, n / 2, width);
  assert(postordered == data);
}

int main()
{
  const size_t widths[] = {4, 7, 8, 12, 16, 24, 32, 40, 64};
  for (size_t width : widths)
  {
    Check(width, FMS_TAG_LEFT, FMS_TAG_RIGHT, 0, 1);
    Check(width, FMS_TAG_RIGHT, FMS_TAG_LEFT, 1, 0);
    Check(width, FMS_TAG_BOTH, FMS_TAG_ZERO, 0, 0);
    Check(width, FMS_TAG_ZERO, FMS_TAG_BOTH, 1, 1);
    CheckLevelOrdered(width);
  }
}
