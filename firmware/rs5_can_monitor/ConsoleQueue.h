#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Single-threaded byte queue: accept whole lines, drain in USB-sized chunks.
template <size_t Capacity>
class ConsoleQueue {
 public:
  bool append(const char *data, size_t length) {
    if (length > Capacity - used_) return false;
    const size_t first = length < Capacity - tail_ ? length : Capacity - tail_;
    memcpy(buffer_ + tail_, data, first);
    memcpy(buffer_, data + first, length - first);
    tail_ = (tail_ + length) % Capacity;
    used_ += length;
    return true;
  }

  const uint8_t *data() const { return buffer_ + head_; }
  size_t size() const { return used_; }
  size_t contiguousSize() const {
    return used_ < Capacity - head_ ? used_ : Capacity - head_;
  }
  void consume(size_t length) {
    if (length > used_) length = used_;
    head_ = (head_ + length) % Capacity;
    used_ -= length;
  }

 private:
  static_assert(Capacity > 0, "Queue must have capacity");
  uint8_t buffer_[Capacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t used_ = 0;
};
