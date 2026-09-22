#ifndef SUPPLE_FMS_HELPER_HPP
#define SUPPLE_FMS_HELPER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fms
{
namespace detail
{

uint8_t CtEqualU8(uint8_t lhs, uint8_t rhs);
uint8_t CtLessSize(size_t lhs, size_t rhs);
uint8_t CtLessI64(int64_t lhs, int64_t rhs);
uint8_t CtSelectU8(uint8_t old_value, uint8_t new_value, uint8_t flag);
int64_t CtSelectI64(int64_t old_value, int64_t new_value, uint8_t flag);
int64_t CtMinI64(int64_t lhs, int64_t rhs);
int64_t CtMaxI64(int64_t lhs, int64_t rhs);

uint8_t LeftBit(uint8_t tag);
uint8_t RightBit(uint8_t tag);

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
} // namespace fms

#endif
