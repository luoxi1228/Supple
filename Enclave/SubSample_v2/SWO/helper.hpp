#ifndef __SUBSAMPLE_V2_SWO_HELPER_HPP__
#define __SUBSAMPLE_V2_SWO_HELPER_HPP__

#ifndef BEFTS_MODE
  #include <cstddef>
  #include <cstdint>
  #include <vector>
#endif

constexpr size_t SwoWordBits()
{
  return sizeof(size_t) * 8;
}

size_t MarkWords(size_t k);

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

inline bool MulOverflowSizeT(size_t a, size_t b, size_t *out)
{
  if (out == nullptr)
  {
    return true;
  }
  if (a == 0 || b == 0)
  {
    *out = 0;
    return false;
  }
  if (a > (SIZE_MAX / b))
  {
    return true;
  }
  *out = a * b;
  return false;
}

bool AddOverflowSizeT(size_t a, size_t b, size_t *out);

size_t MakeRangeMarkWord(size_t range_start, size_t range_end, size_t word_index);

void ProjectMarkSlice(const size_t *src_row,
                      size_t src_words,
                      size_t slice_start,
                      size_t slice_bits,
                      size_t *dst_row,
                      size_t dst_words);

bool *AcquireSelectedScratchA(size_t required);

bool *AcquireSelectedScratchB(size_t required);

bool GetControlBit(const std::vector<uint8_t> &C, size_t pos);

void UnpackControlBitsToBoolArray(const std::vector<uint8_t> &C,
                                  size_t bit_pos,
                                  size_t bit_count,
                                  bool *dst);

void PackBoolArrayToControlBits(std::vector<uint8_t> &C,
                                size_t bit_pos,
                                size_t bit_count,
                                const bool *src);

void SetControlBit(std::vector<uint8_t> &C, size_t pos, bool value);

bool RowHasSliceBit(const size_t *row,
                    size_t row_words,
                    size_t slice_start,
                    size_t slice_bits);

std::vector<bool> ControlSliceToSelected(const std::vector<uint8_t> &C,
                                         size_t p,
                                         size_t n);

void CopyVectorBoolToBoolArray(const std::vector<bool> &src, bool *dst);

size_t OutputBlocksFor(size_t n, size_t m, size_t k);

size_t OMBSUBSAMPLE(const unsigned char *D,
                    size_t n,
                    size_t m,
                    size_t k,
                    size_t block_size,
                    unsigned char *S,
                    size_t out_capacity_blocks);

#endif
