#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace veil::utils {

/**
 * Lock-free Single Producer Single Consumer (SPSC) queue.
 *
 * This queue is designed for high-throughput inter-thread communication
 * in the pipeline processing model (Issue #85). It provides:
 * - Wait-free push (single producer)
 * - Wait-free pop (single consumer)
 * - No locks, no mutexes
 * - Cache-line aligned for optimal performance
 *
 * Design Notes:
 * - Uses a ring buffer with power-of-2 capacity for efficient modulo via bitmasking
 * - Head and tail indices use relaxed atomics where possible, acquire-release where needed
 * - The queue can hold up to (capacity - 1) elements to distinguish full from empty
 *
 * Thread Safety:
 * - Exactly ONE thread may call push() (the producer)
 * - Exactly ONE thread may call pop() (the consumer)
 * - These may be different threads, and no external synchronization is required
 * - Calling push() or pop() from multiple threads is UNDEFINED BEHAVIOR
 *
 * Performance Characteristics:
 * - Push: O(1) wait-free
 * - Pop: O(1) wait-free
 * - Memory: O(capacity) pre-allocated
 *
 * @tparam T The type of elements stored in the queue. Must be movable.
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 * @see Issue #85 for the multi-threading performance improvement initiative.
 */
template <typename T>
class SpscQueue {
  static_assert(std::is_move_constructible_v<T>, "T must be move constructible");

 public:
  /**
   * Construct a queue with the given capacity.
   * The actual capacity will be rounded up to the next power of 2.
   *
   * @param min_capacity Minimum number of elements the queue should hold.
   *                     Actual capacity will be >= min_capacity.
   */
  explicit SpscQueue(std::size_t min_capacity = 1024)
      : capacity_(next_power_of_2(min_capacity + 1)),  // +1 because we use one slot as sentinel
        mask_(capacity_ - 1),
        buffer_(std::make_unique<Slot[]>(capacity_)),
        head_(0),
        tail_(0) {}

  // Non-copyable, non-movable (contains atomics)
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;
  SpscQueue(SpscQueue&&) = delete;
  SpscQueue& operator=(SpscQueue&&) = delete;

  /**
   * Try to push an element to the queue.
   * This operation is wait-free.
   *
   * @param value The value to push (moved into the queue).
   * @return true if the element was pushed, false if the queue is full.
   *
   * Thread Safety: Must only be called from the producer thread.
   */
  bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next_tail = (current_tail + 1) & mask_;

    // Check if queue is full
    if (next_tail == head_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[current_tail].data = std::move(value);
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  /**
   * Try to push an element to the queue (copy version).
   *
   * @param value The value to push (copied into the queue).
   * @return true if the element was pushed, false if the queue is full.
   *
   * Thread Safety: Must only be called from the producer thread.
   */
  bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return try_push(T(value));
  }

  /**
   * Try to pop an element from the queue.
   * This operation is wait-free.
   *
   * @return The popped element if available, std::nullopt if the queue is empty.
   *
   * Thread Safety: Must only be called from the consumer thread.
   */
  std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
    const std::size_t current_head = head_.load(std::memory_order_relaxed);

    // Check if queue is empty
    if (current_head == tail_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    T result = std::move(buffer_[current_head].data);
    head_.store((current_head + 1) & mask_, std::memory_order_release);
    return result;
  }

  /**
   * Check if the queue is empty.
   *
   * Note: This is only accurate if called from the consumer thread.
   * The result may be stale by the time it's used.
   *
   * @return true if the queue appears empty.
   */
  [[nodiscard]] bool empty() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  /**
   * Get the approximate number of elements in the queue.
   *
   * Note: This is an approximation and may be stale by the time it's used.
   *
   * @return Approximate number of elements.
   */
  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    return (tail - head) & mask_;
  }

  /**
   * Get the capacity of the queue.
   *
   * @return Maximum number of elements the queue can hold.
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_ - 1; }

 private:
  /**
   * Round up to the next power of 2.
   */
  static constexpr std::size_t next_power_of_2(std::size_t n) noexcept {
    if (n == 0) return 1;
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if constexpr (sizeof(std::size_t) > 4) {
      n |= n >> 32;
    }
    return n + 1;
  }

  // Slot structure to hold data (cache-line padded in future optimization)
  struct Slot {
    T data{};
  };

  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<Slot[]> buffer_;

  // Cache line separation to avoid false sharing
  // Head is modified by consumer, tail by producer
  alignas(64) std::atomic<std::size_t> head_;
  alignas(64) std::atomic<std::size_t> tail_;
};

/**
 * Bounded Multi-Producer Multi-Consumer (MPMC) queue with mutex.
 *
 * This queue is used when multiple threads need to produce or consume
 * from the same queue. It's simpler but slower than SPSC due to locking.
 *
 * Usage: For workloads where SPSC topology doesn't fit (e.g., multiple
 * worker threads sharing a queue).
 *
 * Thread Safety: All methods are thread-safe.
 */
template <typename T>
class MpmcQueue {
 public:
  explicit MpmcQueue(std::size_t capacity = 1024)
      : capacity_(capacity), buffer_(capacity) {}

  // Non-copyable, non-movable
  MpmcQueue(const MpmcQueue&) = delete;
  MpmcQueue& operator=(const MpmcQueue&) = delete;
  MpmcQueue(MpmcQueue&&) = delete;
  MpmcQueue& operator=(MpmcQueue&&) = delete;

  /**
   * Try to push an element. Returns false if full.
   */
  bool try_push(T&& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ >= capacity_) {
      return false;
    }
    buffer_[(head_ + size_) % capacity_] = std::move(value);
    ++size_;
    return true;
  }

  bool try_push(const T& value) { return try_push(T(value)); }

  /**
   * Try to pop an element. Returns nullopt if empty.
   */
  std::optional<T> try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) {
      return std::nullopt;
    }
    T result = std::move(buffer_[head_]);
    head_ = (head_ + 1) % capacity_;
    --size_;
    return result;
  }

  [[nodiscard]] bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_ == 0;
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

 private:
  const std::size_t capacity_;
  std::vector<T> buffer_;
  std::size_t head_{0};
  std::size_t size_{0};
  mutable std::mutex mutex_;
};

}  // namespace veil::utils
