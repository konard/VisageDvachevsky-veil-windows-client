#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace veil::utils {

/**
 * A simple object pool for packet buffers to reduce memory allocations in the hot path.
 *
 * This pool maintains a free-list of pre-allocated vectors that can be acquired
 * and released. When a buffer is acquired, it's removed from the free list.
 * When released, it's cleared and returned to the free list for reuse.
 *
 * Design Goals (Issue #94):
 * - Reduce heap allocations during packet processing
 * - Minimize cache pollution from frequent alloc/free
 * - Reduce latency variance from allocator contention
 *
 * Performance Characteristics:
 * - acquire(): O(1) when buffers available, O(n) when allocating new buffer
 * - release(): O(1) always
 * - Memory: Pre-allocated buffers remain allocated until pool destruction
 *
 * Thread Safety:
 * - The basic PacketPool is NOT thread-safe. Use ThreadSafePacketPool for
 *   multi-threaded access.
 * - For single-threaded event loop usage, prefer PacketPool for lower overhead.
 *
 * Usage Example:
 *   PacketPool pool(16, 1500);  // 16 buffers of 1500 bytes capacity
 *   auto buffer = pool.acquire();
 *   buffer.reserve(needed_size);
 *   // ... use buffer ...
 *   pool.release(std::move(buffer));  // Return to pool for reuse
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 * @see Issue #94 for the memory allocation reduction initiative.
 */
class PacketPool {
 public:
  /**
   * Construct a packet pool with initial buffers.
   *
   * @param initial_count Number of buffers to pre-allocate (0 for lazy allocation).
   * @param buffer_capacity Initial capacity for each buffer (reserve size).
   */
  explicit PacketPool(std::size_t initial_count = 0, std::size_t buffer_capacity = 1500);

  // Non-copyable, movable
  PacketPool(const PacketPool&) = delete;
  PacketPool& operator=(const PacketPool&) = delete;
  PacketPool(PacketPool&&) noexcept = default;
  PacketPool& operator=(PacketPool&&) noexcept = default;

  ~PacketPool() = default;

  /**
   * Acquire a buffer from the pool.
   *
   * If the pool is empty, allocates a new buffer. The returned buffer
   * is empty (size 0) but has capacity reserved.
   *
   * @return A buffer ready for use.
   */
  [[nodiscard]] std::vector<std::uint8_t> acquire();

  /**
   * Release a buffer back to the pool.
   *
   * The buffer is cleared (size set to 0) but capacity is preserved.
   * If the pool is at max capacity, the buffer is dropped.
   *
   * @param buffer The buffer to return to the pool.
   */
  void release(std::vector<std::uint8_t>&& buffer);

  /**
   * Get the number of buffers currently available in the pool.
   */
  [[nodiscard]] std::size_t available() const noexcept { return free_buffers_.size(); }

  /**
   * Get statistics about pool usage.
   */
  [[nodiscard]] std::uint64_t allocations() const noexcept { return stats_allocations_; }
  [[nodiscard]] std::uint64_t reuses() const noexcept { return stats_reuses_; }
  [[nodiscard]] std::uint64_t releases() const noexcept { return stats_releases_; }

  /**
   * Get the hit rate (reuses / total acquires).
   */
  [[nodiscard]] double hit_rate() const noexcept {
    const auto total = stats_allocations_ + stats_reuses_;
    return total == 0 ? 0.0 : static_cast<double>(stats_reuses_) / static_cast<double>(total);
  }

  /**
   * Pre-allocate additional buffers.
   *
   * @param count Number of buffers to add to the pool.
   */
  void preallocate(std::size_t count);

  /**
   * Set maximum pool size (0 = unlimited).
   *
   * When the pool is at max capacity, released buffers are dropped
   * instead of being stored.
   */
  void set_max_pool_size(std::size_t max_size) noexcept { max_pool_size_ = max_size; }

  /**
   * Get maximum pool size (0 = unlimited).
   */
  [[nodiscard]] std::size_t max_pool_size() const noexcept { return max_pool_size_; }

 private:
  std::vector<std::vector<std::uint8_t>> free_buffers_;
  std::size_t buffer_capacity_;
  std::size_t max_pool_size_{0};  // 0 = unlimited

  // Statistics
  std::uint64_t stats_allocations_{0};
  std::uint64_t stats_reuses_{0};
  std::uint64_t stats_releases_{0};
};

/**
 * Thread-safe version of PacketPool.
 *
 * Uses a mutex to protect all operations. Suitable for multi-threaded
 * scenarios but has higher overhead than the non-thread-safe version.
 *
 * Thread Safety: All methods are thread-safe.
 */
class ThreadSafePacketPool {
 public:
  explicit ThreadSafePacketPool(std::size_t initial_count = 0, std::size_t buffer_capacity = 1500);

  // Non-copyable, non-movable (contains mutex)
  ThreadSafePacketPool(const ThreadSafePacketPool&) = delete;
  ThreadSafePacketPool& operator=(const ThreadSafePacketPool&) = delete;
  ThreadSafePacketPool(ThreadSafePacketPool&&) = delete;
  ThreadSafePacketPool& operator=(ThreadSafePacketPool&&) = delete;

  [[nodiscard]] std::vector<std::uint8_t> acquire();
  void release(std::vector<std::uint8_t>&& buffer);

  [[nodiscard]] std::size_t available() const;
  [[nodiscard]] std::uint64_t allocations() const;
  [[nodiscard]] std::uint64_t reuses() const;
  [[nodiscard]] std::uint64_t releases() const;
  [[nodiscard]] double hit_rate() const;

  void preallocate(std::size_t count);
  void set_max_pool_size(std::size_t max_size);
  [[nodiscard]] std::size_t max_pool_size() const;

 private:
  mutable std::mutex mutex_;
  PacketPool pool_;
};

}  // namespace veil::utils
