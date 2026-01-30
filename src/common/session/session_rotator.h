#pragma once

#include <chrono>
#include <cstdint>
#include <random>

namespace veil::session {

class SessionRotator {
 public:
  SessionRotator(std::chrono::seconds interval, std::uint64_t max_packets);

  std::uint64_t current() const { return session_id_; }
  bool should_rotate(std::uint64_t sent_packets, std::chrono::steady_clock::time_point now) const;
  std::uint64_t rotate(std::chrono::steady_clock::time_point now);

 private:
  // Compute a jittered interval using exponential distribution to mimic
  // natural traffic patterns and resist ML-based DPI detection.
  std::chrono::milliseconds compute_jittered_interval();

  std::chrono::seconds base_interval_;
  std::uint64_t max_packets_;
  std::uint64_t session_id_;
  std::chrono::steady_clock::time_point last_rotation_;
  std::chrono::milliseconds current_interval_;
  std::mt19937_64 rng_;
};

}  // namespace veil::session
