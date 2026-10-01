#ifndef SUPPLE_PACKED_CONTROLS_HPP
#define SUPPLE_PACKED_CONTROLS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ofr
{

class PackedControls;
class PackedControlMutableView;
class PackedControlCursor;

// Gate offsets and lengths are logical two-bit controls, including views
// whose first or last gate shares a storage byte with another node.
class PackedControlView
{
public:
  PackedControlView() : bytes_(NULL), offset_(0), count_(0) {}
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  uint8_t operator[](size_t index) const { return get(index); }
  uint8_t get(size_t index) const
  {
    if (index >= count_)
      throw std::out_of_range("Packed OFR control index");
    const size_t gate = offset_ + index;
    return static_cast<uint8_t>((bytes_[gate / 4] >> (2 * (gate % 4))) & 3U);
  }
  PackedControlView subview(size_t offset, size_t count) const
  {
    if (offset > count_ || count > count_ - offset)
      throw std::out_of_range("Packed OFR control span");
    return PackedControlView(bytes_, offset_ + offset, count);
  }

private:
  PackedControlView(const uint8_t *bytes, size_t offset, size_t count)
      : bytes_(bytes), offset_(offset), count_(count) {}
  const uint8_t *bytes_;
  size_t offset_;
  size_t count_;
  friend class PackedControls;
  friend class PackedControlMutableView;
  friend class PackedControlCursor;
};

class PackedControlMutableView
{
public:
  size_t size() const { return count_; }
  uint8_t get(size_t index) const { return view().get(index); }
  uint8_t operator[](size_t index) const { return get(index); }
  void set(size_t index, uint8_t value)
  {
    if (index >= count_)
      throw std::out_of_range("Packed OFR control index");
    if (value > 3U)
      throw std::invalid_argument("Invalid OFR control word");
    const size_t gate = offset_ + index;
    const size_t shift = 2 * (gate % 4);
    const uint8_t mask = static_cast<uint8_t>(3U << shift);
    bytes_[gate / 4] = static_cast<uint8_t>(
        (bytes_[gate / 4] & static_cast<uint8_t>(~mask)) | (value << shift));
  }
  PackedControlView view() const { return PackedControlView(bytes_, offset_, count_); }
  operator PackedControlView() const { return view(); }
  PackedControlMutableView subview(size_t offset, size_t count) const
  {
    if (offset > count_ || count > count_ - offset)
      throw std::out_of_range("Packed OFR control span");
    return PackedControlMutableView(bytes_, offset_ + offset, count);
  }

private:
  PackedControlMutableView(uint8_t *bytes, size_t offset, size_t count)
      : bytes_(bytes), offset_(offset), count_(count) {}
  uint8_t *bytes_;
  size_t offset_;
  size_t count_;
  friend class PackedControls;
};

// A forward reader loads each byte once and extracts up to four gates. The
// byte boundary branch depends only on the public tape offset.
class PackedControlCursor
{
public:
  explicit PackedControlCursor(PackedControlView controls, size_t position = 0)
      : controls_(controls), position_(position), cached_byte_(0), cached_index_(0),
        loaded_(false)
  {
    if (position > controls.size())
      throw std::out_of_range("Packed OFR control cursor");
  }
  uint8_t next()
  {
    if (position_ >= controls_.count_)
      throw std::out_of_range("Packed OFR control cursor exhausted");
    const size_t gate = controls_.offset_ + position_++;
    const size_t byte_index = gate / 4;
    if (!loaded_ || byte_index != cached_index_)
    {
      cached_byte_ = controls_.bytes_[byte_index];
      cached_index_ = byte_index;
      loaded_ = true;
    }
    return static_cast<uint8_t>((cached_byte_ >> (2 * (gate % 4))) & 3U);
  }
  size_t position() const { return position_; }

private:
  PackedControlView controls_;
  size_t position_;
  uint8_t cached_byte_;
  size_t cached_index_;
  bool loaded_;
};

class PackedControls
{
public:
  class Reference
  {
  public:
    Reference(PackedControls &owner, size_t index) : owner_(owner), index_(index) {}
    operator uint8_t() const { return owner_.get(index_); }
    Reference &operator=(uint8_t value) { owner_.set(index_, value); return *this; }
    Reference &operator=(const Reference &other) { return *this = uint8_t(other); }
  private:
    PackedControls &owner_;
    size_t index_;
  };

  PackedControls() : count_(0) {}
  explicit PackedControls(size_t count, uint8_t value = 0) : count_(0)
  { resize(count, value); }
  size_t size() const { return count_; }
  size_t byte_size() const { return bytes_.size(); }
  bool empty() const { return count_ == 0; }
  const std::vector<uint8_t> &bytes() const { return bytes_; }
  uint8_t get(size_t index) const { return view().get(index); }
  void set(size_t index, uint8_t value) { mutable_view().set(index, value); }
  uint8_t operator[](size_t index) const { return get(index); }
  Reference operator[](size_t index)
  {
    if (index >= count_) throw std::out_of_range("Packed OFR control index");
    return Reference(*this, index);
  }
  void resize(size_t count, uint8_t value = 0)
  {
    if (value > 3U) throw std::invalid_argument("Invalid OFR control word");
    const size_t previous = count_;
    bytes_.resize(count / 4 + (count % 4 != 0), 0);
    count_ = count;
    if (count > previous && value != 0)
      for (size_t i = previous; i < count; ++i) set(i, value);
    // Padding is deterministic, including when shrinking and growing again.
    if (count % 4 != 0)
      bytes_.back() &= static_cast<uint8_t>((1U << (2 * (count % 4))) - 1U);
  }
  void clear() { bytes_.clear(); count_ = 0; }
  void swap(PackedControls &other)
  { bytes_.swap(other.bytes_); std::swap(count_, other.count_); }
  PackedControlView view(size_t offset = 0) const
  {
    if (offset > count_) throw std::out_of_range("Packed OFR control span");
    return view(offset, count_ - offset);
  }
  PackedControlView view(size_t offset, size_t count) const
  {
    if (offset > count_) throw std::out_of_range("Packed OFR control span");
    if (count > count_ - offset) throw std::out_of_range("Packed OFR control span");
    return PackedControlView(bytes_.data(), offset, count);
  }
  PackedControlMutableView mutable_view(size_t offset = 0)
  {
    if (offset > count_) throw std::out_of_range("Packed OFR control span");
    return mutable_view(offset, count_ - offset);
  }
  PackedControlMutableView mutable_view(size_t offset, size_t count)
  {
    if (offset > count_) throw std::out_of_range("Packed OFR control span");
    if (count > count_ - offset) throw std::out_of_range("Packed OFR control span");
    return PackedControlMutableView(bytes_.data(), offset, count);
  }
  void copy_from(size_t offset, PackedControlView source)
  {
    PackedControlMutableView destination = mutable_view(offset, source.size());
    const bool backward = source.bytes_ == bytes_.data() && offset > source.offset_ &&
                          offset - source.offset_ < source.size();
    if (backward)
      for (size_t i = source.size(); i != 0; --i) destination.set(i - 1, source[i - 1]);
    else
      for (size_t i = 0; i < source.size(); ++i) destination.set(i, source[i]);
  }
  bool operator==(const PackedControls &other) const
  { return count_ == other.count_ && bytes_ == other.bytes_; }
  bool operator!=(const PackedControls &other) const { return !(*this == other); }

private:
  std::vector<uint8_t> bytes_;
  size_t count_;
};

} // namespace ofr
#endif

