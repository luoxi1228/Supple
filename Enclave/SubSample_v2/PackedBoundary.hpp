#ifndef SUPPLE_PACKED_BOUNDARY_HPP
#define SUPPLE_PACKED_BOUNDARY_HPP
#include <cstddef>
#include <cstdint>

namespace supple_parallel {
// Only partial bytes are staged. Full bytes belong to exactly one public
// span. Commit after ALL writers finish: adjacent spans may share a byte.
struct PackedBoundary {
  size_t first = 0, last = 0;
  uint8_t first_mask = 0, last_mask = 0;
  uint8_t first_value = 0, last_value = 0;
  void Init(size_t bit, size_t bits) {
    first_mask = last_mask = first_value = last_value = 0;
    if (!bits) return;
    first = bit / 8;
    last = (bit + bits - 1) / 8;
    const size_t start = bit % 8, end = (bit + bits) % 8;
    if (first == last) {
      const uint8_t mask = static_cast<uint8_t>(((1U << bits) - 1U) << start);
      if (mask != 255U) first_mask = mask;
    } else {
      if (start) first_mask = static_cast<uint8_t>(255U << start);
      if (end) last_mask = static_cast<uint8_t>((1U << end) - 1U);
    }
  }
  uint8_t Read(const uint8_t *bytes, size_t index) const {
    if (first_mask && index == first) return first_value;
    if (last_mask && index == last) return last_value;
    return bytes[index];
  }
  void Write(uint8_t *bytes, size_t index, uint8_t value) {
    if (first_mask && index == first) first_value = value;
    else if (last_mask && index == last) last_value = value;
    else bytes[index] = value;
  }
  void Commit(uint8_t *bytes) const {
    if (first_mask) bytes[first] = static_cast<uint8_t>((bytes[first] & ~first_mask) | (first_value & first_mask));
    if (last_mask) bytes[last] = static_cast<uint8_t>((bytes[last] & ~last_mask) | (last_value & last_mask));
  }
};
}
#endif
