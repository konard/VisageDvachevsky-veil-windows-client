#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace veil::session {

class ReplayWindow {
 public:
  explicit ReplayWindow(std::size_t window_size = 1024);
  bool mark_and_check(std::uint64_t sequence);

  // Issue #78: Unmark sequence to allow retransmission after decryption failure
  // Issue #233: Returns false if sequence is blacklisted (exceeded retry limit)
  bool unmark(std::uint64_t sequence);

  // Getter for diagnostic logging (Issue #72)
  [[nodiscard]] std::uint64_t highest() const { return highest_; }
  [[nodiscard]] bool initialized() const { return initialized_; }

 private:
  std::size_t window_size_;
  std::uint64_t highest_{0};
  bool initialized_{false};
  std::vector<std::uint64_t> bits_;

  // Issue #233: Track failed decryption attempts to prevent DoS via repeated unmark()
  // Maps sequence number to failure count. Sequences exceeding MAX_UNMARK_RETRIES are blacklisted.
  std::unordered_map<std::uint64_t, std::uint8_t> failure_counts_;

  // Issue #233: Maximum times a sequence can be unmarked before being permanently rejected
  static constexpr std::uint8_t MAX_UNMARK_RETRIES = 3;

  // Issue #233: Maximum size of failure_counts_ map to prevent memory exhaustion
  static constexpr std::size_t MAX_FAILURE_TRACKING_SIZE = 1024;

  void shift(std::size_t delta);
  bool get_bit(std::size_t index) const;
  void set_bit(std::size_t index);
  void clear_bit(std::size_t index);
  void mask_tail();

  // Issue #233: Clean up old entries from failure tracking map
  void cleanup_failure_tracking();
};

}  // namespace veil::session
