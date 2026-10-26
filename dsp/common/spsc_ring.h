#ifndef EVERYTHINGEQ_DSP_COMMON_SPSC_RING_H_
#define EVERYTHINGEQ_DSP_COMMON_SPSC_RING_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>

namespace eeq {

// Lock-free single-producer / single-consumer ring buffer of trivially
// copyable values (e.g. audio thread -> UI thread).  Never allocates; when
// full, Push() drops what doesn't fit.  N must be a power of two.
template <typename T, size_t N>
class SpscRing {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");

 public:
  // Producer side.  Returns the number of items pushed.
  size_t Push(const T* items, size_t count) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    const size_t n = std::min(count, N - (head - tail));
    for (size_t i = 0; i < n; ++i) buffer_[(head + i) & (N - 1)] = items[i];
    head_.store(head + n, std::memory_order_release);
    return n;
  }
  bool Push(const T& item) { return Push(&item, 1) == 1; }

  // Consumer side.  Returns the number of items popped.
  size_t Pop(T* items, size_t max_count) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t n = std::min(max_count, head - tail);
    for (size_t i = 0; i < n; ++i) items[i] = buffer_[(tail + i) & (N - 1)];
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  size_t size() const {
    return head_.load(std::memory_order_acquire) -
           tail_.load(std::memory_order_acquire);
  }
  static constexpr size_t capacity() { return N; }

 private:
  std::array<T, N> buffer_{};
  std::atomic<size_t> head_{0};  // Written by the producer.
  std::atomic<size_t> tail_{0};  // Written by the consumer.
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_COMMON_SPSC_RING_H_
