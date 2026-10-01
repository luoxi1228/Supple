#include "OFR.hpp"

#include "Enclave_t.h"
#include "../../ObliviousPrimitives.hpp"

#include <sgx_trts.h>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace
{

ofr::PackedControls g_ofr_controls;
bool *g_ocompact_selected = NULL;
size_t g_ofr_n = 0;
size_t g_ofr_n_left = 0;
size_t g_ofr_n_right = 0;
bool g_ofr_postordered = false;

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
  g_ofr_n = 0;
  g_ofr_n_left = 0;
  g_ofr_n_right = 0;
  g_ofr_postordered = false;
  ofr::PackedControls().swap(g_ofr_controls);
}

} // namespace

extern "C" int OFRCompactPrepare(uint8_t *routing_tags,
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
    const bool postordered = n >= 2 && (n & (n - 1)) == 0 &&
                             n_left == n / 2 && n_right == n / 2;
    ofr::PackedControls controls = postordered
        ? ofr::OFRControlPostOrder(tags, n, n_left, n_right)
        : ofr::OFRControl(tags, n, n_left, n_right);
    ocall_clock(&stop_time);

    const std::vector<uint8_t> normalized = ofr::OFRNormalize(
        tags, n, n_left, n_right);
    bool *selected = new bool[n];
    for (size_t i = 0; i < n; ++i)
      selected[i] = ((normalized[i] >> 1U) & 1U) != 0;

    ReleaseBenchmarkContext();
    g_ofr_controls.swap(controls);
    g_ocompact_selected = selected;
    g_ofr_n = n;
    g_ofr_n_left = n_left;
    g_ofr_n_right = n_right;
    g_ofr_postordered = postordered;
    *control_words = g_ofr_controls.size();
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

extern "C" double OFRCompactOnline(unsigned char *buffer,
                                    size_t n,
                                    size_t block_size)
{
  if (n != g_ofr_n || block_size == 0 ||
      !IsOutsideBuffer(buffer, n, block_size))
    return -1.0;

  try
  {
    long start_time = 0;
    long stop_time = 0;
    ocall_clock(&start_time);
    if (g_ofr_postordered)
      ofr::OFRApplyPreparedPostOrderInPlace(
          buffer, g_ofr_controls,
          n, g_ofr_n_left, g_ofr_n_right, block_size);
    else
      ofr::OFRApplyPreparedInPlace(
          buffer, g_ofr_controls,
          n, g_ofr_n_left, g_ofr_n_right, block_size);
    ocall_clock(&stop_time);
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
  if (n != g_ofr_n || g_ocompact_selected == NULL ||
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

extern "C" void OFRCompactRelease(void)
{
  ReleaseBenchmarkContext();
}
