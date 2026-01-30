#include "common/utils/packet_pool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace veil::utils {

PacketPool::PacketPool(std::size_t initial_count, std::size_t buffer_capacity)
    : buffer_capacity_(buffer_capacity) {
  if (initial_count > 0) {
    preallocate(initial_count);
  }
}

std::vector<std::uint8_t> PacketPool::acquire() {
  if (!free_buffers_.empty()) {
    auto buffer = std::move(free_buffers_.back());
    free_buffers_.pop_back();
    ++stats_reuses_;
    // Buffer should already be cleared from release(), but ensure it's empty
    buffer.clear();
    return buffer;
  }

  // No free buffers - allocate new one
  ++stats_allocations_;
  std::vector<std::uint8_t> buffer;
  buffer.reserve(buffer_capacity_);
  return buffer;
}

void PacketPool::release(std::vector<std::uint8_t>&& buffer) {
  ++stats_releases_;

  // Check if pool is at capacity
  if (max_pool_size_ > 0 && free_buffers_.size() >= max_pool_size_) {
    // Pool is full - just drop the buffer (destructor will free memory)
    return;
  }

  // Clear the buffer but preserve capacity
  buffer.clear();
  free_buffers_.push_back(std::move(buffer));
}

void PacketPool::preallocate(std::size_t count) {
  free_buffers_.reserve(free_buffers_.size() + count);
  for (std::size_t i = 0; i < count; ++i) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(buffer_capacity_);
    free_buffers_.push_back(std::move(buffer));
  }
}

// ThreadSafePacketPool implementation

ThreadSafePacketPool::ThreadSafePacketPool(std::size_t initial_count, std::size_t buffer_capacity)
    : pool_(initial_count, buffer_capacity) {}

std::vector<std::uint8_t> ThreadSafePacketPool::acquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.acquire();
}

void ThreadSafePacketPool::release(std::vector<std::uint8_t>&& buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  pool_.release(std::move(buffer));
}

std::size_t ThreadSafePacketPool::available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.available();
}

std::uint64_t ThreadSafePacketPool::allocations() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.allocations();
}

std::uint64_t ThreadSafePacketPool::reuses() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.reuses();
}

std::uint64_t ThreadSafePacketPool::releases() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.releases();
}

double ThreadSafePacketPool::hit_rate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.hit_rate();
}

void ThreadSafePacketPool::preallocate(std::size_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  pool_.preallocate(count);
}

void ThreadSafePacketPool::set_max_pool_size(std::size_t max_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  pool_.set_max_pool_size(max_size);
}

std::size_t ThreadSafePacketPool::max_pool_size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.max_pool_size();
}

}  // namespace veil::utils
