#ifndef SUPPLE_OFR_HELPER_HPP
#define SUPPLE_OFR_HELPER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ofr
{
namespace detail
{

inline uint8_t CtEqualU8(uint8_t lhs, uint8_t rhs)
{
  uint8_t result;
  __asm__ volatile("cmpb %[rhs], %[lhs]\n\tsete %[result]"
                   : [result] "=r"(result)
                   : [lhs] "r"(lhs), [rhs] "r"(rhs)
                   : "cc");
  return result;
}

inline uint8_t CtLessSize(size_t lhs, size_t rhs)
{
  uint8_t result;
  __asm__ volatile("cmp %[rhs], %[lhs]\n\tsetb %[result]"
                   : [result] "=r"(result)
                   : [lhs] "r"(lhs), [rhs] "r"(rhs)
                   : "cc");
  return result;
}

inline uint8_t CtLessI64(int64_t lhs, int64_t rhs)
{
  uint8_t result;
  __asm__ volatile("cmpq %[rhs], %[lhs]\n\tsetl %[result]"
                   : [result] "=r"(result)
                   : [lhs] "r"(lhs), [rhs] "r"(rhs)
                   : "cc");
  return result;
}

inline uint8_t CtSelectU8(uint8_t old_value, uint8_t new_value, uint8_t flag)
{
  const uint8_t mask = static_cast<uint8_t>(0U - (flag & 1U));
  return static_cast<uint8_t>((old_value & static_cast<uint8_t>(~mask)) |
                              (new_value & mask));
}

inline int64_t CtSelectI64(int64_t old_value, int64_t new_value, uint8_t flag)
{
  const uint64_t mask = 0U - static_cast<uint64_t>(flag & 1U);
  const uint64_t old_bits = static_cast<uint64_t>(old_value);
  const uint64_t new_bits = static_cast<uint64_t>(new_value);
  return static_cast<int64_t>((old_bits & ~mask) | (new_bits & mask));
}

inline int64_t CtMinI64(int64_t lhs, int64_t rhs)
{
  return CtSelectI64(lhs, rhs, CtLessI64(rhs, lhs));
}

inline int64_t CtMaxI64(int64_t lhs, int64_t rhs)
{
  return CtSelectI64(lhs, rhs, CtLessI64(lhs, rhs));
}

inline uint8_t LeftBit(uint8_t tag) { return static_cast<uint8_t>((tag >> 1U) & 1U); }
inline uint8_t RightBit(uint8_t tag) { return static_cast<uint8_t>(tag & 1U); }

bool MulOverflowSize(size_t lhs, size_t rhs, size_t *result);
bool AddOverflowSize(size_t lhs, size_t rhs, size_t *result);

void ValidateCapacities(size_t n, size_t n_left, size_t n_right);
void ValidateTags(const std::vector<uint8_t> &tags,
                  size_t n,
                  size_t n_left,
                  size_t n_right,
                  bool require_exact);

// Execute one OFork on two records already copied into top and bottom.
void ApplyOFork(unsigned char *top,
                unsigned char *bottom,
                size_t block_size,
                uint8_t control);

} // namespace detail
} // namespace ofr

#endif
