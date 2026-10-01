#ifndef SUPPLE_OFR_FOURGATE_SSE2_HPP
#define SUPPLE_OFR_FOURGATE_SSE2_HPP

#include <cstdint>
#include <cstring>

#if defined(__SSE2__) && defined(__x86_64__) && !defined(__ILP32__)
namespace ofr
{
namespace detail
{
namespace fourgate_sse2
{

typedef int BatchVec32 __attribute__((vector_size(16)));
typedef long long BatchVec64 __attribute__((vector_size(16)));

struct Masks
{
  BatchVec32 top;
  BatchVec32 bottom;
};

__attribute__((always_inline)) inline Masks Decode(uint8_t packed_controls)
{
  const int packed = packed_controls;
  const BatchVec32 controls = {packed, packed, packed, packed};
  const BatchVec32 top_bits = {2, 8, 32, 128};
  const BatchVec32 bottom_bits = {1, 4, 16, 64};
  const BatchVec32 zero = {0, 0, 0, 0};
  Masks masks;
  masks.top = ~((controls & top_bits) == zero);
  masks.bottom = ~((controls & bottom_bits) == zero);
  return masks;
}

__attribute__((always_inline)) inline BatchVec32 Load(const unsigned char *data)
{
  BatchVec32 result;
  std::memcpy(&result, data, sizeof(result));
  return result;
}

__attribute__((always_inline)) inline void Store(unsigned char *data,
                                                BatchVec32 value)
{
  std::memcpy(data, &value, sizeof(value));
}

// Counter presence depends on public build configuration. Production callers
// pass &OSWAP_COUNTER under COUNT_OSWAPS, otherwise nullptr. Four independent
// gates retain exactly four switching-primitive counts, including copies.
__attribute__((always_inline)) inline void CountFour(uint64_t *switch_counter)
{
  if (switch_counter != nullptr)
    *switch_counter += 4;
}

// Preconditions: first/second each contain four contiguous 8-byte records;
// the eight records are disjoint and the four gates are independent. This
// must not execute a four-control packet containing dependent n=4 gates.
__attribute__((always_inline)) inline void ApplyFour8(
    unsigned char *first, unsigned char *second, uint8_t packed_controls,
    uint64_t *switch_counter = nullptr)
{
  const Masks masks = Decode(packed_controls);
  const BatchVec32 top_low = __builtin_ia32_punpckldq128(masks.top, masks.top);
  const BatchVec32 top_high = __builtin_ia32_punpckhdq128(masks.top, masks.top);
  const BatchVec32 bottom_low = __builtin_ia32_punpckldq128(masks.bottom, masks.bottom);
  const BatchVec32 bottom_high = __builtin_ia32_punpckhdq128(masks.bottom, masks.bottom);

  // Load both sources before any store; an OFork can copy either source into
  // both outputs. Low/high masks duplicate each 32-bit flag across its whole
  // 64-bit record, keeping all four controls 00/01/10/11 intact.
  const BatchVec32 x_low = Load(first);
  const BatchVec32 x_high = Load(first + 16);
  const BatchVec32 y_low = Load(second);
  const BatchVec32 y_high = Load(second + 16);
  const BatchVec32 diff_low = x_low ^ y_low;
  const BatchVec32 diff_high = x_high ^ y_high;
  CountFour(switch_counter);
  Store(first, x_low ^ (diff_low & top_low));
  Store(first + 16, x_high ^ (diff_high & top_high));
  Store(second, x_low ^ (diff_low & bottom_low));
  Store(second + 16, x_high ^ (diff_high & bottom_high));
}

template <int shuffle>
__attribute__((always_inline)) inline void ApplyOne16(
    unsigned char *first, unsigned char *second, const Masks &masks)
{
  const BatchVec32 top = __builtin_ia32_pshufd(masks.top, shuffle);
  const BatchVec32 bottom = __builtin_ia32_pshufd(masks.bottom, shuffle);
  const BatchVec32 x = Load(first);
  const BatchVec32 y = Load(second);
  const BatchVec32 diff = x ^ y;
  Store(first, x ^ (diff & top));
  Store(second, x ^ (diff & bottom));
}

// Same public preconditions as ApplyFour8, with 16 bytes per record. Each
// control is broadcast across four 32-bit payload lanes; scalar/other-width
// assembly remains available for the public widths not handled here.
__attribute__((always_inline)) inline void ApplyFour16(
    unsigned char *first, unsigned char *second, uint8_t packed_controls,
    uint64_t *switch_counter = nullptr)
{
  const Masks masks = Decode(packed_controls);
  CountFour(switch_counter);
  ApplyOne16<0x00>(first, second, masks);
  ApplyOne16<0x55>(first + 16, second + 16, masks);
  ApplyOne16<0xaa>(first + 32, second + 32, masks);
  ApplyOne16<0xff>(first + 48, second + 48, masks);
}

} // namespace fourgate_sse2
} // namespace detail
} // namespace ofr
#endif

#endif
