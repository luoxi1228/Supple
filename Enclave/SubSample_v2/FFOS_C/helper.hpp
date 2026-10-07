#ifndef FFOS_C_HELPER_HPP
#define FFOS_C_HELPER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
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

namespace ffos_c_detail
{

constexpr size_t FfosCWordBits() { return sizeof(size_t) * 8; }

// Shared by serial recursive compactions after their flags are consumed.
// Own it in the control/apply call: SGX may reset TLS at the next root ECALL.
class SelectedScratch
{
public:
  bool *acquire(size_t required);

private:
  std::unique_ptr<bool[]> buffer_;
  size_t capacity_ = 0;
};

// Retain capacity across sibling nodes; recursive calls carry the live length.
struct FfosCMarkWorkspace
{
  std::vector<size_t> mark_buf;
  size_t *mark_ptr();
  void ensure_capacity(size_t items, size_t mark_words);
};

struct FfosCDataWorkspace
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
    size_t slice_bits, FfosCMarkWorkspace &ws);

void CompactMarkToWorkspaceAndControl(const size_t *mark,
    size_t len_items,
    size_t src_mark_words,
    bool *selected,
    size_t target_size,
    size_t slice_start,
    size_t slice_bits,
    std::vector<uint8_t> &C,
    size_t bit_pos,
    FfosCMarkWorkspace &ws);

size_t FfosCNodeCapacity(size_t n, size_t m, size_t k);

std::vector<FrontierNode> FfosCChildren(size_t k);

std::vector<FrontierNode> FfosCRootNodes(const std::vector<FrontierNode> &F, size_t k);

size_t FfosCNodeDepth(const std::vector<FrontierNode> &nodes);

void FfosCCheckControlSpan(const std::vector<uint8_t> &C, size_t p, size_t bits);

} // namespace ffos_c_detail

#endif
