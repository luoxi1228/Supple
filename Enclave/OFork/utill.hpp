#ifndef __FMS_OFORK_UTILL_HPP__
#define __FMS_OFORK_UTILL_HPP__

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ofork_internal
{

uint8_t CtEqualU8(uint8_t lhs, uint8_t rhs);
uint8_t CtLessSize(size_t lhs, size_t rhs);
uint8_t CtSelectU8(uint8_t old_value, uint8_t new_value, uint8_t flag);
uint8_t LeftBit(uint8_t tag);
uint8_t RightBit(uint8_t tag);

bool MulOverflowSize(size_t lhs, size_t rhs, size_t *result);
bool SupportedBlockSize(size_t block_size);

bool WriteLevelOrderedControls(const uint8_t *normalized_tags,
                               size_t n,
                               std::vector<uint8_t> *controls,
                               size_t position);

bool ApplyLevelOrdered(unsigned char *data,
                       size_t n,
                       size_t block_size,
                       const uint8_t *controls,
                       size_t control_count);

#ifndef BEFTS_MODE
bool StoreContext(std::vector<uint8_t> *controls,
                  std::vector<uint8_t> *normalized_tags);
void ReleaseContext();

size_t ContextSize();
size_t ContextControlCount();
const uint8_t *ContextControls();
bool *ContextLeftSelected();
bool *ContextRightSelected();

bool IsOutsideBuffer(unsigned char *buffer,
                     size_t n,
                     size_t block_size);
#endif

} // namespace ofork_internal

#endif
