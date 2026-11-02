#pragma once
#include <stddef.h>

#include <vector>

namespace tinytimegrapher {

/**
 * @brief Fixed capacity ring buffer that drops the oldest entry when full.
 * Index 0 is the oldest entry, size() - 1 the newest.
 */
template <typename T>
class HistoryBuffer {
 public:
  HistoryBuffer() = default;
  explicit HistoryBuffer(size_t capacity) { resize(capacity); }

  void resize(size_t capacity) {
    data_.assign(capacity, T{});
    start_ = 0;
    size_ = 0;
  }

  void clear() {
    start_ = 0;
    size_ = 0;
  }

  void push(const T& value) {
    if (data_.empty()) return;
    if (size_ < data_.size()) {
      data_[(start_ + size_) % data_.size()] = value;
      size_++;
    } else {
      data_[start_] = value;
      start_ = (start_ + 1) % data_.size();
    }
  }

  const T& operator[](size_t i) const { return data_[(start_ + i) % data_.size()]; }
  T& operator[](size_t i) { return data_[(start_ + i) % data_.size()]; }

  /// The newest entry; only valid if !empty().
  const T& back() const { return (*this)[size_ - 1]; }

  size_t size() const { return size_; }
  size_t capacity() const { return data_.size(); }
  bool empty() const { return size_ == 0; }

 private:
  std::vector<T> data_;
  size_t start_ = 0;
  size_t size_ = 0;
};

}  // namespace tinytimegrapher
