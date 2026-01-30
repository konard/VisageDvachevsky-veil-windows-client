#include "transport/mux/congestion_controller.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

#include "common/logging/logger.h"

namespace veil::mux {

CongestionController::CongestionController(CongestionConfig config,
                                           std::function<TimePoint()> now_fn)
    : config_(config),
      now_fn_(std::move(now_fn)),
      cwnd_(config_.initial_cwnd),
      ssthresh_(config_.initial_ssthresh),
      last_send_time_(now_fn_()),
      pacing_burst_remaining_(config_.max_pacing_burst) {
  // Initialize pacing rate based on initial cwnd and default RTT.
  update_pacing_rate(srtt_);

  LOG_DEBUG("CongestionController initialized: cwnd={}, ssthresh={}, pacing_rate={}",
            cwnd_, ssthresh_, pacing_rate_);
}

void CongestionController::on_ack(std::size_t acked_bytes) {
  if (acked_bytes == 0) {
    return;
  }

  // Reset duplicate ACK count on new ACK.
  dup_ack_count_ = 0;

  switch (state_) {
    case CongestionState::kSlowStart: {
      // Slow start: Increase cwnd by acked_bytes (exponential growth).
      // RFC 5681: cwnd += min(acked_bytes, SMSS) for each ACK.
      const std::size_t increase = std::min(acked_bytes, config_.mss);
      cwnd_ = std::min(cwnd_ + increase, config_.max_cwnd);
      ++stats_.cwnd_increases;

      LOG_DEBUG("Slow start: cwnd increased to {} (+{})", cwnd_, increase);

      // Check if we've exceeded ssthresh.
      if (cwnd_ >= ssthresh_) {
        enter_congestion_avoidance();
      }
      break;
    }

    case CongestionState::kCongestionAvoidance: {
      // Congestion avoidance: Linear increase.
      // RFC 5681: cwnd += SMSS * SMSS / cwnd for each ACK.
      // This results in ~1 MSS increase per RTT.
      if (cwnd_ > 0) {
        const std::size_t increase = (config_.mss * acked_bytes) / cwnd_;
        if (increase > 0) {
          cwnd_ = std::min(cwnd_ + increase, config_.max_cwnd);
          ++stats_.cwnd_increases;
          LOG_DEBUG("Congestion avoidance: cwnd increased to {} (+{})", cwnd_, increase);
        }
      }
      break;
    }

    case CongestionState::kFastRecovery: {
      // RFC 5681 Fast Recovery: Inflate cwnd by the amount of data acknowledged.
      // This allows additional data to be sent during recovery.
      cwnd_ = std::min(cwnd_ + acked_bytes, config_.max_cwnd);
      LOG_DEBUG("Fast recovery: cwnd inflated to {} (+{})", cwnd_, acked_bytes);
      break;
    }
  }

  // Update peak tracking.
  if (cwnd_ > stats_.peak_cwnd) {
    stats_.peak_cwnd = cwnd_;
  }
}

bool CongestionController::on_duplicate_ack() {
  ++dup_ack_count_;
  ++stats_.duplicate_acks;

  LOG_DEBUG("Duplicate ACK received: count={}", dup_ack_count_);

  if (state_ == CongestionState::kFastRecovery) {
    // RFC 5681: During fast recovery, inflate cwnd by MSS for each dup ACK.
    cwnd_ = std::min(cwnd_ + config_.mss, config_.max_cwnd);
    LOG_DEBUG("Fast recovery cwnd inflation: cwnd={}", cwnd_);
    return false;  // Already in fast recovery.
  }

  // Check if we've hit the fast retransmit threshold.
  if (dup_ack_count_ >= config_.fast_retransmit_threshold) {
    ++stats_.fast_retransmits;
    return true;  // Trigger fast retransmit.
  }

  return false;
}

void CongestionController::on_timeout_loss() {
  ++stats_.timeout_retransmits;
  ++stats_.cwnd_decreases;

  // RFC 5681: On timeout, enter slow start.
  // ssthresh = max(FlightSize / 2, 2 * MSS)
  // cwnd = 1 MSS (or IW in RFC 6928)
  ssthresh_ = std::max(cwnd_ / 2, 2 * config_.mss);
  cwnd_ = config_.mss;  // Conservative: 1 MSS on timeout.

  LOG_INFO("Timeout loss: ssthresh={}, cwnd={}", ssthresh_, cwnd_);

  dup_ack_count_ = 0;
  enter_slow_start();
}

void CongestionController::on_fast_retransmit_loss() {
  ++stats_.cwnd_decreases;

  // RFC 5681 Fast Retransmit/Fast Recovery:
  // ssthresh = max(FlightSize / 2, 2 * MSS)
  // cwnd = ssthresh + 3 * MSS (accounting for the 3 dup ACKs)
  ssthresh_ = std::max(cwnd_ / 2, 2 * config_.mss);
  cwnd_ = ssthresh_ + 3 * config_.mss;

  LOG_INFO("Fast retransmit loss: ssthresh={}, cwnd={}", ssthresh_, cwnd_);

  enter_fast_recovery();
}

void CongestionController::on_recovery_complete() {
  if (state_ != CongestionState::kFastRecovery) {
    return;
  }

  // RFC 5681: After fast recovery, set cwnd to ssthresh (deflate).
  cwnd_ = ssthresh_;
  dup_ack_count_ = 0;

  LOG_INFO("Fast recovery complete: cwnd deflated to {}", cwnd_);

  enter_congestion_avoidance();
}

bool CongestionController::can_send(std::size_t bytes_in_flight) const {
  // Can send if bytes_in_flight < cwnd.
  return bytes_in_flight < cwnd_;
}

std::size_t CongestionController::sendable_bytes(std::size_t bytes_in_flight) const {
  if (bytes_in_flight >= cwnd_) {
    return 0;
  }
  return cwnd_ - bytes_in_flight;
}

bool CongestionController::check_pacing() {
  if (!config_.enable_pacing) {
    return true;  // Pacing disabled, always allow.
  }

  // Allow burst sending at start of connection or after idle.
  if (pacing_burst_remaining_ > 0) {
    --pacing_burst_remaining_;
    ++stats_.pacing_tokens_granted;
    return true;
  }

  const auto now = now_fn_();
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      now - last_send_time_);
  const auto interval = calculate_pacing_interval();

  if (elapsed >= interval) {
    last_send_time_ = now;
    // Reset burst on pacing interval completion.
    pacing_burst_remaining_ = config_.max_pacing_burst - 1;
    ++stats_.pacing_tokens_granted;
    return true;
  }

  ++stats_.pacing_delays;
  return false;
}

std::optional<std::chrono::microseconds> CongestionController::time_until_next_send() const {
  if (!config_.enable_pacing) {
    return std::nullopt;
  }

  if (pacing_burst_remaining_ > 0) {
    return std::nullopt;  // Can send immediately.
  }

  const auto now = now_fn_();
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      now - last_send_time_);
  const auto interval = calculate_pacing_interval();

  if (elapsed >= interval) {
    return std::nullopt;
  }

  return std::chrono::duration_cast<std::chrono::microseconds>(interval - elapsed);
}

void CongestionController::update_pacing_rate(std::chrono::milliseconds rtt) {
  srtt_ = rtt;

  if (rtt.count() <= 0) {
    // Avoid division by zero - use a high default rate.
    pacing_rate_ = cwnd_ * 1000;  // Assume 1ms RTT.
    return;
  }

  // Pacing rate = cwnd / RTT * pacing_gain.
  // This spreads cwnd bytes over one RTT, with some overhead.
  const std::size_t base_rate = (cwnd_ * 1000) / static_cast<std::size_t>(rtt.count());
  pacing_rate_ = static_cast<std::size_t>(static_cast<double>(base_rate) * config_.pacing_gain);

  LOG_DEBUG("Pacing rate updated: {} bytes/sec (cwnd={}, rtt={}ms, gain={})",
            pacing_rate_, cwnd_, rtt.count(), config_.pacing_gain);
}

void CongestionController::set_srtt(std::chrono::milliseconds srtt) {
  update_pacing_rate(srtt);
}

void CongestionController::reset() {
  cwnd_ = config_.initial_cwnd;
  ssthresh_ = config_.initial_ssthresh;
  state_ = CongestionState::kSlowStart;
  dup_ack_count_ = 0;
  pacing_burst_remaining_ = config_.max_pacing_burst;
  last_send_time_ = now_fn_();
  update_pacing_rate(srtt_);

  LOG_DEBUG("CongestionController reset: cwnd={}, ssthresh={}", cwnd_, ssthresh_);
}

void CongestionController::enter_slow_start() {
  if (state_ != CongestionState::kSlowStart) {
    state_ = CongestionState::kSlowStart;
    ++stats_.state_transitions;
    LOG_DEBUG("Entered slow start state");
  }
}

void CongestionController::enter_congestion_avoidance() {
  if (state_ != CongestionState::kCongestionAvoidance) {
    if (state_ == CongestionState::kSlowStart) {
      ++stats_.slow_start_exits;
    }
    state_ = CongestionState::kCongestionAvoidance;
    ++stats_.state_transitions;
    LOG_DEBUG("Entered congestion avoidance state");
  }
}

void CongestionController::enter_fast_recovery() {
  if (state_ != CongestionState::kFastRecovery) {
    state_ = CongestionState::kFastRecovery;
    ++stats_.state_transitions;
    LOG_DEBUG("Entered fast recovery state");
  }
}

std::chrono::microseconds CongestionController::calculate_pacing_interval() const {
  if (pacing_rate_ == 0) {
    return config_.min_pacing_interval;
  }

  // Interval = MSS / pacing_rate (in seconds), convert to microseconds.
  // interval_us = (MSS * 1000000) / pacing_rate
  const auto interval_us = (config_.mss * 1000000) / pacing_rate_;
  const auto interval = std::chrono::microseconds(interval_us);

  // Clamp to minimum.
  return std::max(interval, config_.min_pacing_interval);
}

}  // namespace veil::mux
