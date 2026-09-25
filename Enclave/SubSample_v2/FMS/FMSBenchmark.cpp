#include "FMS.hpp"

#include "Enclave_t.h"
#include "../../ObliviousPrimitives.hpp"

#include <sgx_trts.h>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace
{

std::vector<uint8_t> g_fms_controls;
std::vector<fms::FMSOutputSwap> g_fms_output_swaps;
bool *g_ocompact_selected = NULL;
size_t g_fms_n = 0;
size_t g_fms_n_left = 0;
size_t g_fms_n_right = 0;
bool g_fms_postordered = false;

bool MulOverflow(size_t lhs, size_t rhs, size_t *result)
{
  if (result == NULL ||
      (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs))
    return true;
  *result = lhs * rhs;
  return false;
}

bool SupportedOCompactBlockSize(size_t block_size)
{
  return block_size <= std::numeric_limits<uint32_t>::max() &&
         (block_size == 4 ||
          block_size == 8 ||
          block_size == 12 ||
          (block_size >= 16 && block_size % 16 == 0) ||
          (block_size >= 24 && block_size % 16 == 8));
}

bool IsOutsideBuffer(unsigned char *buffer, size_t n, size_t block_size)
{
  size_t bytes = 0;
  return buffer != NULL &&
         !MulOverflow(n, block_size, &bytes) &&
         sgx_is_outside_enclave(buffer, bytes) != 0;
}

void ReleaseBenchmarkContext()
{
  delete[] g_ocompact_selected;
  g_ocompact_selected = NULL;
  g_fms_n = 0;
  g_fms_n_left = 0;
  g_fms_n_right = 0;
  g_fms_postordered = false;
  std::vector<uint8_t>().swap(g_fms_controls);
  std::vector<fms::FMSOutputSwap>().swap(g_fms_output_swaps);
}

} // namespace

extern "C" int FMSCompactPrepare(uint8_t *routing_tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right,
                                  size_t *control_words,
                                  double *control_us)
{
  if (routing_tags == NULL || control_words == NULL || control_us == NULL)
    return -1;

  *control_us = -1.0;
  try
  {
    const std::vector<uint8_t> tags(routing_tags, routing_tags + n);
    long start_time = 0;
    long stop_time = 0;
    ocall_clock(&start_time);
    std::vector<uint8_t> controls = fms::FMSControl(
        tags, n, n_left, n_right);
    std::vector<fms::FMSOutputSwap> output_swaps =
        fms::FMSOutputSwaps(n, n_left, n_right);
    const bool postordered = n >= 2 && (n & (n - 1)) == 0 &&
                             n_left == n / 2 && n_right == n / 2;
    if (postordered)
    {
      std::vector<uint8_t> postorder_controls =
          fms::FMSPostOrderControls(controls, n, n_left, n_right);
      controls.swap(postorder_controls);
    }
    ocall_clock(&stop_time);

    const std::vector<uint8_t> normalized = fms::FMSNormalize(
        tags, n, n_left, n_right);
    bool *selected = new bool[n];
    for (size_t i = 0; i < n; ++i)
      selected[i] = ((normalized[i] >> 1U) & 1U) != 0;

    ReleaseBenchmarkContext();
    g_fms_controls.swap(controls);
    g_fms_output_swaps.swap(output_swaps);
    g_ocompact_selected = selected;
    g_fms_n = n;
    g_fms_n_left = n_left;
    g_fms_n_right = n_right;
    g_fms_postordered = postordered;
    *control_words = g_fms_controls.size();
    *control_us = static_cast<double>(stop_time - start_time);
    return 0;
  }
  catch (const std::bad_alloc &)
  {
    return -3;
  }
  catch (...)
  {
    return -2;
  }
}

extern "C" double FMSCompactOnline(unsigned char *buffer,
                                    size_t n,
                                    size_t block_size)
{
  if (n != g_fms_n || block_size == 0 ||
      !IsOutsideBuffer(buffer, n, block_size))
    return -1.0;

  try
  {
    long start_time = 0;
    long stop_time = 0;
    ocall_clock(&start_time);
    if (g_fms_postordered)
      fms::FMSApplyPreparedPostOrderInPlace(
          buffer, g_fms_controls, g_fms_output_swaps,
          n, g_fms_n_left, g_fms_n_right, block_size, false);
    else
      fms::FMSApplyPreparedInPlace(
          buffer, g_fms_controls, g_fms_output_swaps,
          n, g_fms_n_left, g_fms_n_right, block_size, false);
    ocall_clock(&stop_time);

    // Keep the final output identical, but exclude output swaps from the timer.
    fms::FMSApplyOutputSwapsInPlace(
        buffer, n, block_size, g_fms_output_swaps);
    return static_cast<double>(stop_time - start_time);
  }
  catch (...)
  {
    return -1.0;
  }
}

extern "C" double OCompactOnline(unsigned char *buffer,
                                  size_t n,
                                  size_t block_size)
{
  if (n != g_fms_n || g_ocompact_selected == NULL ||
      !SupportedOCompactBlockSize(block_size) ||
      !IsOutsideBuffer(buffer, n, block_size))
    return -1.0;

  long start_time = 0;
  long stop_time = 0;
  ocall_clock(&start_time);
  TightCompact_v2(buffer, n, block_size, g_ocompact_selected);
  ocall_clock(&stop_time);
  return static_cast<double>(stop_time - start_time);
}

extern "C" void FMSCompactRelease(void)
{
  ReleaseBenchmarkContext();
}
