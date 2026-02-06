#include "common/session/replay_window.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace veil::session {

namespace {
constexpr std::size_t kBitsPerWord = std::numeric_limits<std::uint64_t>::digits;
}

ReplayWindow::ReplayWindow(std::size_t window_size)
    : window_size_(window_size),
      bits_((window_size + kBitsPerWord - 1) / kBitsPerWord) {}

bool ReplayWindow::mark_and_check(std::uint64_t sequence) {
  if (!initialized_) {
    highest_ = sequence;
    initialized_ = true;
    set_bit(0);
    return true;
  }

  if (sequence > highest_) {
    const std::size_t delta = static_cast<std::size_t>(sequence - highest_);
    if (delta >= window_size_) {
      std::fill(bits_.begin(), bits_.end(), 0);
    } else {
      shift(delta);
    }
    highest_ = sequence;
    set_bit(0);
    return true;
  }

  const std::uint64_t diff = highest_ - sequence;
  if (diff >= window_size_) {
    return false;
  }

  const std::size_t index = static_cast<std::size_t>(diff);
  if (get_bit(index)) {
    return false;
  }
  set_bit(index);
  return true;
}

void ReplayWindow::shift(std::size_t delta) {
  const auto word_shift = delta / kBitsPerWord;
  const auto bit_shift = delta % kBitsPerWord;

  if (word_shift >= bits_.size()) {
    std::fill(bits_.begin(), bits_.end(), 0);
    return;
  }

  for (std::size_t i = bits_.size(); i-- > 0;) {
    std::uint64_t value = 0;
    if (i >= word_shift) {
      value = bits_[i - word_shift];
      if (bit_shift != 0) {
        value <<= bit_shift;
        if (i > word_shift) {
          value |= bits_[i - word_shift - 1] >> (kBitsPerWord - bit_shift);
        }
      }
    }
    bits_[i] = value;
  }
  mask_tail();
}

bool ReplayWindow::get_bit(std::size_t index) const {
  const auto word = index / kBitsPerWord;
  const auto bit = index % kBitsPerWord;
  return ((bits_[word] >> bit) & 1U) != 0U;
}

void ReplayWindow::set_bit(std::size_t index) {
  const auto word = index / kBitsPerWord;
  const auto bit = index % kBitsPerWord;
  bits_[word] |= (std::uint64_t(1) << bit);
}

void ReplayWindow::clear_bit(std::size_t index) {
  const auto word = index / kBitsPerWord;
  const auto bit = index % kBitsPerWord;
  bits_[word] &= ~(std::uint64_t(1) << bit);
}

bool ReplayWindow::unmark(std::uint64_t sequence) {
  if (!initialized_) {
    return false;
  }

  // Can only unmark sequences <= highest_
  if (sequence > highest_) {
    return false;
  }

  const std::uint64_t diff = highest_ - sequence;
  if (diff >= window_size_) {
    return false;
  }

  // Issue #233: Track failure count to prevent DoS via repeated unmark()
  // If a sequence repeatedly fails decryption, an attacker could send the same
  // malformed packet over and over, causing mark->unmark->mark cycles that consume CPU.
  // We limit retries per sequence to prevent this attack.
  auto it = failure_counts_.find(sequence);
  if (it != failure_counts_.end()) {
    // Sequence has failed before - check if it exceeded retry limit
    if (it->second >= MAX_UNMARK_RETRIES) {
      // Blacklisted: exceeded maximum retries, permanently reject
      return false;
    }
    // Increment failure count
    ++it->second;
  } else {
    // First failure for this sequence
    failure_counts_[sequence] = 1;

    // Prevent memory exhaustion: if tracking map grows too large, clean up old entries
    if (failure_counts_.size() > MAX_FAILURE_TRACKING_SIZE) {
      cleanup_failure_tracking();
    }
  }

  const std::size_t index = static_cast<std::size_t>(diff);
  clear_bit(index);
  return true;
}

void ReplayWindow::mask_tail() {
  const auto remainder = window_size_ % kBitsPerWord;
  if (remainder == 0) {
    return;
  }
  const std::uint64_t mask = (std::uint64_t(1) << remainder) - 1;
  bits_.back() &= mask;
}

void ReplayWindow::cleanup_failure_tracking() {
  // Issue #233: Remove sequences that are now outside the replay window
  // This prevents unbounded memory growth while keeping relevant failure tracking.
  // Strategy: Remove sequences that are more than window_size_ behind highest_.
  // This is safe because such sequences would be rejected by mark_and_check anyway.

  if (!initialized_ || failure_counts_.empty()) {
    return;
  }

  // Calculate the minimum sequence number still within the window
  const std::uint64_t min_valid_seq = highest_ > window_size_ ? highest_ - window_size_ : 0;

  // Remove entries for sequences outside the window
  for (auto it = failure_counts_.begin(); it != failure_counts_.end();) {
    if (it->first < min_valid_seq) {
      it = failure_counts_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace veil::session
