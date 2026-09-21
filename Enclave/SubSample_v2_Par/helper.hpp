#ifndef SUPPLE_SWO_PAR_HELPER_HPP
#define SUPPLE_SWO_PAR_HELPER_HPP
#include <cstddef>
#include <cstdint>
#include <vector>

namespace swo_parallel {
namespace detail {

struct FrontierNode { size_t start; size_t count; };
struct ControlReadResult { size_t written_blocks; size_t next_pos; };

constexpr size_t SwoWordBits() { return sizeof(size_t) * 8; }

struct SwoMarkWorkspace
{
  std::vector<size_t> mark_buf;
  size_t item_count = 0;
  size_t mark_words_per_item = 0;

  size_t *mark_ptr()
  {
    return mark_buf.empty() ? nullptr : mark_buf.data();
  }

  void ensure_capacity(size_t items, size_t mark_words)
  {
    const size_t required = items * mark_words;
    if (mark_buf.size() < required)
    {
      mark_buf.resize(required);
    }
    item_count = items;
    mark_words_per_item = mark_words;
  }

  void release_memory()
  {
    std::vector<size_t>().swap(mark_buf);
    item_count = 0;
    mark_words_per_item = 0;
  }
};

struct SwoDataWorkspace
{
  std::vector<unsigned char> data_buf;
  size_t item_count = 0;

  unsigned char *data_ptr()
  {
    return data_buf.empty() ? nullptr : data_buf.data();
  }

  void ensure_capacity(size_t items, size_t block_size)
  {
    const size_t required = items * block_size;
    if (data_buf.size() < required)
    {
      data_buf.resize(required);
    }
    item_count = items;
  }

  void release_memory()
  {
    std::vector<unsigned char>().swap(data_buf);
    item_count = 0;
  }
};

struct MarkSliceRange
{
  size_t slice_start = 0;
  size_t slice_bits = 0;
  size_t dst_words = 0;
  size_t first_word = 0;
  size_t last_word = 0;
  size_t first_mask = 0;
  size_t last_mask = 0;
  bool valid = false;
};

size_t MarkWords(size_t k);

size_t WorkspaceDepth(size_t k);

bool MulOverflowSizeT(size_t a, size_t b, size_t *out);

bool AddOverflowSizeT(size_t a, size_t b, size_t *out);

void UnpackControlBitsToBoolArray(const std::vector<uint8_t> &C,
                                  size_t bit_pos,
                                  size_t bit_count,
                                  bool *dst);

bool MakeMarkSliceRange(size_t row_words,
                        size_t slice_start,
                        size_t slice_bits,
                        MarkSliceRange *range);

void CompactMarkToWorkspaceAndControl(const size_t *mark,
                                      size_t len_items,
                                      size_t src_mark_words,
                                      bool *selected,
                                      size_t target_size,
                                      const MarkSliceRange &slice_range,
                                      std::vector<uint8_t> &C,
                                      size_t bit_pos,
                                      SwoMarkWorkspace &ws);

void CompactDataToWorkspace(const unsigned char *data,
                            size_t len_items,
                            const bool *selected,
                            size_t target_size,
                            size_t block_size,
                            SwoDataWorkspace &ws);

size_t CompactDataInPlace(unsigned char *data,
                          size_t len_items,
                          const bool *selected,
                          size_t target_size,
                          size_t block_size);

size_t OutputBlocksFor(size_t n, size_t m, size_t k);

size_t MakeRangeMarkWord(size_t range_start, size_t range_end, size_t word_index);

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

std::vector<size_t> MarkMembership(size_t n, size_t m, size_t k);

std::vector<FrontierNode> BuildFrontier(size_t s, size_t l, size_t n, size_t m);

std::vector<FrontierNode> NextNodes(const std::vector<FrontierNode> &F, size_t k);

size_t CountControlBits(const std::vector<FrontierNode> &F, size_t n, size_t m, size_t k);

size_t WriteControlWorkspace(const size_t *M,
                              size_t n,
                              size_t mark_words,
                              std::vector<uint8_t> &C,
                              const std::vector<FrontierNode> &F,
                              size_t m,
                              size_t k,
                              size_t p,
                              std::vector<SwoMarkWorkspace> &workspaces,
                              size_t depth);

ControlReadResult ReadControlWorkspace(unsigned char *D,
                                        const std::vector<uint8_t> &C,
                                        const std::vector<FrontierNode> &F,
                                        size_t n,
                                        size_t m,
                                        size_t k,
                                        size_t block_size,
                                        unsigned char *S,
                                        size_t out_capacity_blocks,
                                        size_t p,
                                        std::vector<SwoDataWorkspace> &workspaces,
                                        size_t depth);

bool *AcquireSelectedScratchA(size_t required);

bool *AcquireSelectedScratchB(size_t required);

} // namespace detail
} // namespace swo_parallel
#endif
