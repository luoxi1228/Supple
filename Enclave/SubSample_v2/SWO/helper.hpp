#ifndef SUPPLE_SWO_HELPER_HPP
#define SUPPLE_SWO_HELPER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

struct FrontierNode
{
  size_t start;
  size_t count;
};

struct ControlReadResult
{
  size_t written_blocks;
  size_t next_pos;
};

namespace swo_detail
{

constexpr size_t SwoWordBits() { return sizeof(size_t) * 8; }

// Retain capacity across sibling nodes; recursive calls carry the live length.
struct SwoMarkWorkspace
{
  std::vector<size_t> mark_buf;
  size_t *mark_ptr();
  void ensure_capacity(size_t items, size_t mark_words);
};

struct SwoDataWorkspace
{
  std::vector<unsigned char> data_buf;
  unsigned char *data_ptr();
  void ensure_capacity(size_t items, size_t block_size);
};

size_t MarkWords(size_t k);

size_t WorkspaceDepth(size_t k);

bool MulOverflowSizeT(size_t a, size_t b, size_t *out);

bool AddOverflowSizeT(size_t a, size_t b, size_t *out);

void UnpackControlBitsToBoolArray(const std::vector<uint8_t> &C,
    size_t bit_pos,
    size_t bit_count,
    bool *dst);

void ProjectMarkSlice(const size_t *src_row,
    size_t src_words,
    size_t slice_start,
    size_t slice_bits,
    size_t *dst_row,
    size_t dst_words);

size_t ProjectMarkSliceOneWord(const size_t *src_row,
    size_t src_words,
    size_t slice_start,
    size_t slice_bits);

void ProjectMarkToWorkspace(const size_t *mark, size_t len_items,
    size_t src_mark_words, size_t slice_start,
    size_t slice_bits, SwoMarkWorkspace &ws);

bool *AcquireSelectedScratch(size_t required);

void CompactMarkToWorkspaceAndControl(const size_t *mark,
    size_t len_items,
    size_t src_mark_words,
    bool *selected,
    size_t target_size,
    size_t slice_start,
    size_t slice_bits,
    std::vector<uint8_t> &C,
    size_t bit_pos,
    SwoMarkWorkspace &ws);

size_t SwoNodeCapacity(size_t n, size_t m, size_t k);

std::vector<FrontierNode> SwoChildren(size_t k);

std::vector<FrontierNode> SwoRootNodes(const std::vector<FrontierNode> &F, size_t k);

size_t SwoNodeDepth(const std::vector<FrontierNode> &nodes);

void SwoCheckControlSpan(const std::vector<uint8_t> &C, size_t p, size_t bits);

} // namespace swo_detail

#endif
