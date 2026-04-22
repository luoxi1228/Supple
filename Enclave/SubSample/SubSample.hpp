#ifndef __SUBSAMPLE_HPP__
#define __SUBSAMPLE_HPP__

#ifndef BEFTS_MODE
  #include <cstddef>
  #include <cstdint>
  #include <limits>
  #include <vector>
  #include "../foav.h"
  #include "../oasm_lib.h"
  #include "../ObliviousPrimitives.hpp"
  #include "../Enclave_globals.h"
  #include "../utils.hpp"
#endif

constexpr size_t wordBits = sizeof(size_t) * 8;

// 工作区：持有可复用的大缓冲
struct PairWorkspace
{
  std::vector<unsigned char> data_buf;
  std::vector<size_t> mark_buf;

  // 当前逻辑有效长度
  size_t item_count = 0;
  size_t mark_words_per_item = 0;

  unsigned char *data_ptr()
  {
    return data_buf.empty() ? nullptr : data_buf.data();
  }

  size_t *mark_ptr()
  {
    return mark_buf.empty() ? nullptr : mark_buf.data();
  }

  void ensure_capacity(size_t items, size_t block_size, size_t dst_mark_words)
  {
    const size_t data_bytes = items * block_size;
    const size_t mark_words_total = items * dst_mark_words;

    if (data_buf.size() < data_bytes)
    {
      data_buf.resize(data_bytes);
    }
    if (mark_buf.size() < mark_words_total)
    {
      mark_buf.resize(mark_words_total);
    }

    item_count = items;
    mark_words_per_item = dst_mark_words;
  }

  void release_memory()
  {
    std::vector<unsigned char>().swap(data_buf);
    std::vector<size_t>().swap(mark_buf);
    item_count = 0;
    mark_words_per_item = 0;
  }
};

struct MarkWorkspace_opt {
    std::vector<size_t> mark_buf;
    size_t item_count = 0;
    
    size_t *mark_ptr() { return mark_buf.empty() ? nullptr : mark_buf.data(); }
    void ensure_capacity(size_t items, size_t dst_mark_words) {
        if (mark_buf.size() < items * dst_mark_words) mark_buf.resize(items * dst_mark_words);
        item_count = items;
    }
};

struct DataWorkspace_opt {
    std::vector<unsigned char> data_buf;
    size_t item_count = 0;
    
    unsigned char *data_ptr() { return data_buf.empty() ? nullptr : data_buf.data(); }
    void ensure_capacity(size_t items, size_t block_size) {
        if (data_buf.size() < items * block_size) data_buf.resize(items * block_size);
        item_count = items;
    }
};

inline size_t nodeNum(size_t n, size_t m, size_t k) {
    if (k <= 1 || n == 0) return 0;
    
    size_t k_left = k / 2;
    size_t k_right = k - k_left;
    
    size_t s_left = std::min(n, m * k_left);
    size_t s_right = std::min(n, m * k_right);
    
    return (2 * n) + nodeNum(s_left, m, k_left) + nodeNum(s_right, m, k_right);
}



inline size_t MakeRangeMarkWord(size_t range_start, size_t range_end, size_t word_index)
{
  if (range_end <= range_start)
  {
    return 0;
  }

  const size_t word_start = word_index * wordBits;
  const size_t word_end = word_start + wordBits;
  if (range_end <= word_start || range_start >= word_end)
  {
    return 0;
  }

  const size_t local_start = (range_start > word_start) ? (range_start - word_start) : 0;
  const size_t local_end = (range_end < word_end) ? (range_end - word_start) : wordBits;
  const size_t width = local_end - local_start;

  if (width >= wordBits)
  {
    return ~size_t(0);
  }
  return ((size_t(1) << width) - 1) << local_start;
}

inline void ProjectMarkSlice(const size_t *src_row,
                             size_t src_words,
                             size_t slice_start,
                             size_t slice_bits,
                             size_t *dst_row,
                             size_t dst_words)
{
  if (slice_bits == 0 || dst_words == 0)
  {
    return;
  }

  for (size_t w = 0; w < dst_words; w++)
  {
    const size_t bit_pos = slice_start + (w * wordBits);
    const size_t src_w = bit_pos / wordBits;
    const size_t off = bit_pos % wordBits;

    const size_t low = (src_w < src_words) ? (src_row[src_w] >> off) : 0;
    size_t high = 0;
    if (off != 0 && (src_w + 1) < src_words)
    {
      high = src_row[src_w + 1] << (wordBits - off);
    }

    dst_row[w] = (low | high);
  }

  const size_t valid_bits_last_word = slice_bits % wordBits;
  if (valid_bits_last_word != 0)
  {
    const size_t keep = (size_t(1) << valid_bits_last_word) - 1;
    dst_row[dst_words - 1] &= keep;
  }
}

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

inline bool *AcquireSelectedScratchA(size_t required)
{
  thread_local bool *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required == 0)
  {
    return scratch;
  }
  if (required <= capacity)
  {
    return scratch;
  }

  bool *new_scratch = new (std::nothrow) bool[required];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required;
  return scratch;
}

inline bool *AcquireSelectedScratchB(size_t required)
{
  thread_local bool *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required == 0)
  {
    return scratch;
  }
  if (required <= capacity)
  {
    return scratch;
  }

  bool *new_scratch = new (std::nothrow) bool[required];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required;
  return scratch;
}

inline size_t *AcquireMarkScratchA(size_t required_words)
{
  thread_local size_t *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required_words == 0)
  {
    return scratch;
  }
  if (required_words <= capacity)
  {
    return scratch;
  }

  size_t *new_scratch = new (std::nothrow) size_t[required_words];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required_words;
  return scratch;
}

inline size_t *AcquireMarkScratchB(size_t required_words)
{
  thread_local size_t *scratch = nullptr;
  thread_local size_t capacity = 0;

  if (required_words == 0)
  {
    return scratch;
  }
  if (required_words <= capacity)
  {
    return scratch;
  }

  size_t *new_scratch = new (std::nothrow) size_t[required_words];
  if (new_scratch == nullptr)
  {
    return nullptr;
  }

  delete[] scratch;
  scratch = new_scratch;
  capacity = required_words;
  return scratch;
}

// 生成数量为m个1的数组
void markGen(size_t n, size_t m, bool *M);

// 生成长度为n的掩码数组，每个元素包含k个子选择标志位
void markGenMulti(size_t n, size_t m, size_t k, size_t *M);

// 写入外部预分配工作区
void ORCompactPair(const unsigned char *data,
                   const size_t *mark,
                   size_t len_items,
                   size_t src_mark_words,
                   const bool *selected,
                   size_t target_size,
                   size_t block_size,
                   size_t slice_start,
                   size_t slice_bits,
                   PairWorkspace &ws);

// 递归采样
size_t RecSampling(const unsigned char *data,
                   const size_t *mark,
                   size_t len_items,
                   size_t mark_words,
                   size_t k,
                   size_t M,
                   size_t block_size,
                   unsigned char *out,
                   size_t out_capacity,
                   PairWorkspace *left_ws = nullptr,
                   PairWorkspace *right_ws = nullptr,
                   std::vector<unsigned char> *data_owner = nullptr,
                   std::vector<size_t> *mark_owner = nullptr);

void subSample(unsigned char *buffer,
               size_t N,
               size_t M,
               size_t block_size,
               unsigned char *result_buffer,
               enc_ret *ret);

void subSampleMulti(unsigned char *buffer,
                    size_t N,
                    size_t M,
                    size_t K,
                    size_t block_size,
                    unsigned char *result_buffer,
                    enc_ret *ret);

void decryptAndSubSample(unsigned char *encrypted_buffer,
                         size_t N,
                         size_t M,
                         size_t encrypted_block_size,
                         unsigned char *encryt_result_buffer,
                         enc_ret *ret);

void decryptAndSubSampleMulti(unsigned char *encrypted_buffer,
                              size_t N,
                              size_t M,
                              size_t K,
                              size_t encrypted_block_size,
                              unsigned char *encryt_result_buffer,
                              enc_ret *ret);


void decryptAndSubSampleMulti_opt(unsigned char *encrypted_buffer,
                              size_t N,
                              size_t M,
                              size_t K,
                              size_t encrypted_block_size,
                              unsigned char *encryt_result_buffer,
                              enc_ret *ret);

void CompactMark_opt(const size_t *mark, size_t len_items, size_t src_mark_words,
                     const bool *selected, size_t target_size, size_t slice_start,
                     size_t slice_bits, MarkWorkspace_opt &ws);


void RecPrecompute_opt(const size_t *mark, size_t len_items, size_t mark_words,
                       size_t k, size_t M, uint8_t *B, size_t &bit_offset,
                       MarkWorkspace_opt *left_ws = nullptr, MarkWorkspace_opt *right_ws = nullptr);

void CompactData_opt(const unsigned char *data, size_t len_items, const bool *selected,
                     size_t target_size, size_t block_size, DataWorkspace_opt &ws);

size_t RecSampling_opt(const unsigned char *data, size_t len_items, size_t k, size_t M,
                       size_t block_size, unsigned char *out, size_t out_capacity,
                       const uint8_t *B, size_t &bit_offset,
                       DataWorkspace_opt *left_ws = nullptr, DataWorkspace_opt *right_ws = nullptr);      
                       
void subSampleMulti_opt(unsigned char *buffer, size_t N, size_t M, size_t K,
                        size_t block_size, unsigned char *result_buffer, enc_ret *ret);                      

#endif