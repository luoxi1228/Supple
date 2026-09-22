#ifndef SUPPLE_FMS_HPP
#define SUPPLE_FMS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fms
{

// Routing tags: bit 1 is left responsibility; bit 0 is right responsibility.
enum FMSRoutingTag : uint8_t
{
  FMS_TAG_ZERO = 0,
  FMS_TAG_RIGHT = 1,
  FMS_TAG_LEFT = 2,
  FMS_TAG_BOTH = 3
};

// Control words use the same physical bit positions but different semantics:
// 00 -> (x,x), 01 -> (x,y), 10 -> (y,x), 11 -> (y,y).
enum FMSControlWord : uint8_t
{
  FMS_COPY_FIRST = 0,
  FMS_STRAIGHT = 1,
  FMS_SWAP = 2,
  FMS_COPY_SECOND = 3
};

struct FMSBalanceResult
{
  std::vector<uint8_t> controls;
  std::vector<uint8_t> top_tags;
  std::vector<uint8_t> bottom_tags;
};

struct FMSDataResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
};

struct FMSControlReadResult
{
  std::vector<unsigned char> left;
  std::vector<unsigned char> right;
  size_t next_pos;
};

std::array<uint8_t, 10> FMSPairMask(uint8_t x, uint8_t y);

uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t top_tag,
                     uint8_t bottom_tag);

std::vector<uint8_t> FMSNormalize(const std::vector<uint8_t> &tags,
                                  size_t n,
                                  size_t n_left,
                                  size_t n_right);

FMSBalanceResult FMSBalance(const std::vector<uint8_t> &tags,
                            size_t n,
                            size_t n_left,
                            size_t n_right);

size_t FMSControlCount(size_t n, size_t n_left, size_t n_right);

size_t FMSControlWrite(const std::vector<uint8_t> &tags,
                       std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t position);

std::vector<uint8_t> FMSControl(const std::vector<uint8_t> &tags,
                                size_t n,
                                size_t n_left,
                                size_t n_right);

FMSControlReadResult FMSControlRead(const unsigned char *data,
                                    const std::vector<uint8_t> &controls,
                                    size_t n,
                                    size_t n_left,
                                    size_t n_right,
                                    size_t block_size,
                                    size_t position);

FMSDataResult FMSApply(const unsigned char *data,
                       const std::vector<uint8_t> &controls,
                       size_t n,
                       size_t n_left,
                       size_t n_right,
                       size_t block_size);

FMSDataResult FMSCompact(const unsigned char *data,
                         const std::vector<uint8_t> &tags,
                         size_t n,
                         size_t n_left,
                         size_t n_right,
                         size_t block_size);

} // namespace fms

#endif
