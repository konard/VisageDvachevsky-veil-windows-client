#include "common/session/session_rotator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "common/crypto/random.h"

namespace veil::session {

SessionRotator::SessionRotator(std::chrono::seconds interval, std::uint64_t max_packets)
    : base_interval_(interval),
      max_packets_(max_packets),
      session_id_(crypto::random_uint64()),
      last_rotation_(std::chrono::steady_clock::now()),
      current_interval_(),
      rng_(crypto::random_uint64()) {
  current_interval_ = compute_jittered_interval();
}

bool SessionRotator::should_rotate(std::uint64_t sent_packets,
                                   std::chrono::steady_clock::time_point now) const {
  const bool too_many_packets = sent_packets >= max_packets_;
  const bool expired = (now - last_rotation_) >= current_interval_;
  return too_many_packets || expired;
}

std::uint64_t SessionRotator::rotate(std::chrono::steady_clock::time_point now) {
  std::uint64_t next = crypto::random_uint64();
  if (next == session_id_) {
    next = crypto::random_uint64();
  }
  session_id_ = next;
  last_rotation_ = now;
  current_interval_ = compute_jittered_interval();
  return session_id_;
}

std::chrono::milliseconds SessionRotator::compute_jittered_interval() {
  // Use exponential jitter: most intervals cluster near the base, some are longer.
  // This mimics natural human/P2P session durations that ML classifiers expect.
  // Range: [base * 0.67, base * 1.67] — e.g. for 30s base: ~20s to ~50s.
  const double base_ms = static_cast<double>(
      std::chrono::duration_cast<std::chrono::milliseconds>(base_interval_).count());

  // Jitter range is 1/3 of base interval on each side.
  const double jitter_range = base_ms / 3.0;

  // Exponential distribution with lambda = 1/jitter_range gives a mean of jitter_range.
  // We use it to bias towards shorter jitter values with occasional longer ones.
  std::exponential_distribution<double> exp_dist(1.0 / jitter_range);
  double jitter = exp_dist(rng_);

  // Clamp jitter to [-jitter_range, 2*jitter_range] relative to base.
  // Use uniform to decide sign (subtract vs add).
  std::uniform_real_distribution<double> sign_dist(0.0, 1.0);
  const bool subtract = sign_dist(rng_) < 0.33;

  double result_ms;
  if (subtract) {
    // Shorter interval: base - clamp(jitter, 0, jitter_range).
    result_ms = base_ms - std::min(jitter, jitter_range);
  } else {
    // Longer interval: base + clamp(jitter, 0, 2*jitter_range).
    result_ms = base_ms + std::min(jitter, 2.0 * jitter_range);
  }

  // Safety floor: never go below 25% of base.
  result_ms = std::max(result_ms, base_ms * 0.25);

  return std::chrono::milliseconds(static_cast<std::int64_t>(result_ms));
}

}  // namespace veil::session
