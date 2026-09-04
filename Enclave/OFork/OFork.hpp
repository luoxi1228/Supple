#ifndef __FMS_OFORK_HPP__
#define __FMS_OFORK_HPP__

#include <stddef.h>
#include <stdint.h>
#include <vector>

// bit 1 is the left/top responsibility and bit 0 is the right/bottom
// responsibility.  An OFork control word has the same bit layout:
//   00 -> (x, x), 01 -> (x, y), 10 -> (y, x), 11 -> (y, y).
enum FMSRoutingTag : uint8_t
{
  FMS_TAG_NONE = 0,
  FMS_TAG_RIGHT = 1,
  FMS_TAG_LEFT = 2,
  FMS_TAG_BOTH = 3
};

struct FMSBalanceOutput
{
  std::vector<uint8_t> controls;
  std::vector<uint8_t> top_tags;
  std::vector<uint8_t> bottom_tags;
};

bool FMSIsPowerOfTwo(size_t n);

// Returns 0 when n is invalid or the result overflows size_t.
size_t FMSControlNum(size_t n);

// mask[0..9] follows the pair-type order in the paper.
void TagPairMask(uint8_t x, uint8_t y, uint8_t mask[10]);

// Algorithm 16. The input may have at most n/2 responsibilities for either
// child. Remaining 00 tags are deterministically assigned as padding.
bool TagNormalize(const uint8_t *tags,
                  size_t n,
                  std::vector<uint8_t> *normalized_tags);

// Algorithm 17. t_top and t_bottom are the desired output tags.
uint8_t OForkControl(uint8_t x,
                     uint8_t y,
                     uint8_t t_top,
                     uint8_t t_bottom);

// Algorithm 19. n must be a power of two of at least four, and both routing
// responsibility sums must equal n/2.
bool FMSBalance(const uint8_t *tags,
                size_t n,
                FMSBalanceOutput *output);

// Algorithm 14. Writes controls in level order. Every level contains n/2
// controls, ordered by the corresponding in-place OFork gate position.
bool FMSControlWrite(const uint8_t *normalized_tags,
                     size_t n,
                     std::vector<uint8_t> *controls,
                     size_t position,
                     size_t *next_pos);

// Algorithm 12. normalized_tags is optional and is useful to construct the
// semantically equivalent two-compaction baseline.
bool FMSControlBits(const uint8_t *tags,
                    size_t n,
                    std::vector<uint8_t> *controls,
                    std::vector<uint8_t> *normalized_tags = NULL);

// Algorithm 18. Executes the OFork network level by level. The first half is
// the left output set and the second half is the right output set; order within
// either half is unspecified.
bool FMSApply(unsigned char *data,
              size_t n,
              size_t block_size,
              const uint8_t *controls,
              size_t control_count);

// EDL entry points used by Application/OForkApplication.cpp. FMSPrepare is the
// offline phase and reports the FMSControlBits generation time in microseconds.
// The two online measurement calls time one invocation only and return
// microseconds; negative values indicate invalid state or arguments.
#ifdef __cplusplus
extern "C" {
#endif

int FMSPrepare(uint8_t *routing_tags,
               size_t n,
               size_t *control_words,
               double *control_bits_us);

double FMSApplyOnline(unsigned char *buffer,
                      size_t n,
                      size_t block_size);

double TwoCompactOnline(unsigned char *left_buffer,
                        unsigned char *right_buffer,
                        size_t n,
                        size_t block_size);

void FMSRelease(void);

#ifdef __cplusplus
}
#endif

#endif
