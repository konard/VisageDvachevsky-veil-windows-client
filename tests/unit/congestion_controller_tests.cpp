#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

#include "transport/mux/congestion_controller.h"

namespace veil::mux::tests {

using namespace std::chrono_literals;

class CongestionControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = std::chrono::steady_clock::now();
    config_.initial_cwnd = static_cast<std::size_t>(10) * 1400;      // 10 MSS
    config_.min_cwnd = 1400;                // 1 MSS
    config_.max_cwnd = static_cast<std::size_t>(64) * 1024 * 1024;    // 64 MB
    config_.initial_ssthresh = static_cast<std::size_t>(64) * 1024;   // 64 KB for testing
    config_.mss = 1400;
    config_.fast_retransmit_threshold = 3;
    config_.enable_pacing = false;          // Disable pacing for most tests
    config_.aimd_alpha = 0.5;
  }

  std::chrono::steady_clock::time_point now_;
  CongestionConfig config_;
};

// ========== Slow Start Tests ==========

TEST_F(CongestionControllerTest, InitialStateIsSlowStart) {
  CongestionController cc(config_, [this]() { return now_; });

  EXPECT_EQ(cc.state(), CongestionState::kSlowStart);
  EXPECT_EQ(cc.cwnd(), config_.initial_cwnd);
  EXPECT_EQ(cc.ssthresh(), config_.initial_ssthresh);
}

TEST_F(CongestionControllerTest, SlowStartExponentialGrowth) {
  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t initial_cwnd = cc.cwnd();

  // Simulate ACK for 1 MSS worth of data.
  cc.on_ack(config_.mss);

  // In slow start, cwnd should increase by min(acked_bytes, MSS).
  EXPECT_EQ(cc.cwnd(), initial_cwnd + config_.mss);
  EXPECT_EQ(cc.state(), CongestionState::kSlowStart);
  EXPECT_EQ(cc.stats().cwnd_increases, 1U);
}

TEST_F(CongestionControllerTest, SlowStartExitAtSsthresh) {
  config_.initial_cwnd = 1400;  // Start small
  config_.initial_ssthresh = static_cast<std::size_t>(5) * 1400;  // 5 MSS

  CongestionController cc(config_, [this]() { return now_; });

  // ACK enough to exceed ssthresh.
  for (int i = 0; i < 10; ++i) {
    cc.on_ack(config_.mss);
    if (cc.cwnd() >= config_.initial_ssthresh) {
      break;
    }
  }

  EXPECT_GE(cc.cwnd(), config_.initial_ssthresh);
  EXPECT_EQ(cc.state(), CongestionState::kCongestionAvoidance);
  EXPECT_EQ(cc.stats().slow_start_exits, 1U);
}

// ========== Congestion Avoidance Tests ==========

TEST_F(CongestionControllerTest, CongestionAvoidanceLinearGrowth) {
  config_.initial_cwnd = static_cast<std::size_t>(10) * 1400;      // Start above ssthresh
  config_.initial_ssthresh = static_cast<std::size_t>(5) * 1400;   // Lower ssthresh

  CongestionController cc(config_, [this]() { return now_; });

  // Should already be in congestion avoidance since cwnd > ssthresh.
  cc.on_ack(config_.mss);  // This pushes it to CA state.
  EXPECT_EQ(cc.state(), CongestionState::kCongestionAvoidance);

  const std::size_t cwnd_before = cc.cwnd();

  // In CA, cwnd increases by MSS * acked_bytes / cwnd per ACK.
  // This results in approximately 1 MSS increase per RTT.
  cc.on_ack(config_.mss);

  // Growth should be smaller than in slow start.
  const std::size_t increase = cc.cwnd() - cwnd_before;
  EXPECT_LT(increase, config_.mss);  // Linear growth is slower.
  EXPECT_GT(increase, 0U);
}

// ========== Fast Retransmit Tests ==========

TEST_F(CongestionControllerTest, DuplicateAckCounting) {
  CongestionController cc(config_, [this]() { return now_; });

  // First two dup ACKs should not trigger fast retransmit.
  EXPECT_FALSE(cc.on_duplicate_ack());
  EXPECT_FALSE(cc.on_duplicate_ack());
  EXPECT_EQ(cc.stats().duplicate_acks, 2U);

  // Third dup ACK should trigger fast retransmit.
  EXPECT_TRUE(cc.on_duplicate_ack());
  EXPECT_EQ(cc.stats().duplicate_acks, 3U);
  EXPECT_EQ(cc.stats().fast_retransmits, 1U);
}

TEST_F(CongestionControllerTest, FastRetransmitReducesCwnd) {
  config_.initial_cwnd = static_cast<std::size_t>(20) * 1400;  // 20 MSS

  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t cwnd_before = cc.cwnd();

  // Trigger fast retransmit.
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  EXPECT_TRUE(cc.on_duplicate_ack());

  cc.on_fast_retransmit_loss();

  // ssthresh should be cwnd / 2.
  EXPECT_EQ(cc.ssthresh(), cwnd_before / 2);

  // cwnd should be ssthresh + 3 * MSS.
  EXPECT_EQ(cc.cwnd(), cc.ssthresh() + 3 * config_.mss);
  EXPECT_EQ(cc.state(), CongestionState::kFastRecovery);
}

TEST_F(CongestionControllerTest, FastRecoveryInflation) {
  config_.initial_cwnd = static_cast<std::size_t>(20) * 1400;

  CongestionController cc(config_, [this]() { return now_; });

  // Enter fast recovery.
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  cc.on_fast_retransmit_loss();

  const std::size_t cwnd_in_recovery = cc.cwnd();

  // Additional dup ACKs during fast recovery should inflate cwnd.
  cc.on_duplicate_ack();
  EXPECT_EQ(cc.cwnd(), cwnd_in_recovery + config_.mss);
}

TEST_F(CongestionControllerTest, FastRecoveryDeflation) {
  config_.initial_cwnd = static_cast<std::size_t>(20) * 1400;

  CongestionController cc(config_, [this]() { return now_; });

  // Enter fast recovery.
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  cc.on_fast_retransmit_loss();

  const std::size_t ssthresh = cc.ssthresh();

  // Exit fast recovery.
  cc.on_recovery_complete();

  // cwnd should be deflated to ssthresh.
  EXPECT_EQ(cc.cwnd(), ssthresh);
  EXPECT_EQ(cc.state(), CongestionState::kCongestionAvoidance);
}

// ========== Timeout Tests ==========

TEST_F(CongestionControllerTest, TimeoutReducesCwndToMinimum) {
  config_.initial_cwnd = static_cast<std::size_t>(20) * 1400;

  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t cwnd_before = cc.cwnd();

  cc.on_timeout_loss();

  // ssthresh should be cwnd / 2.
  EXPECT_EQ(cc.ssthresh(), cwnd_before / 2);

  // cwnd should be reduced to 1 MSS.
  EXPECT_EQ(cc.cwnd(), config_.mss);
  EXPECT_EQ(cc.state(), CongestionState::kSlowStart);
  EXPECT_EQ(cc.stats().timeout_retransmits, 1U);
}

// ========== Send Permission Tests ==========

TEST_F(CongestionControllerTest, CanSendWhenCwndAvailable) {
  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t cwnd = cc.cwnd();

  // Can send when bytes_in_flight < cwnd.
  EXPECT_TRUE(cc.can_send(0));
  EXPECT_TRUE(cc.can_send(cwnd - 1));

  // Cannot send when bytes_in_flight >= cwnd.
  EXPECT_FALSE(cc.can_send(cwnd));
  EXPECT_FALSE(cc.can_send(cwnd + 1));
}

TEST_F(CongestionControllerTest, SendableBytesCalculation) {
  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t cwnd = cc.cwnd();

  EXPECT_EQ(cc.sendable_bytes(0), cwnd);
  EXPECT_EQ(cc.sendable_bytes(cwnd / 2), cwnd / 2);
  EXPECT_EQ(cc.sendable_bytes(cwnd), 0U);
  EXPECT_EQ(cc.sendable_bytes(cwnd + 100), 0U);
}

// ========== Pacing Tests ==========

TEST_F(CongestionControllerTest, PacingDisabledAllowsImmediate) {
  config_.enable_pacing = false;

  CongestionController cc(config_, [this]() { return now_; });

  // Should always be able to send when pacing is disabled.
  EXPECT_TRUE(cc.check_pacing());
  EXPECT_TRUE(cc.check_pacing());
  EXPECT_TRUE(cc.check_pacing());

  EXPECT_FALSE(cc.time_until_next_send().has_value());
}

TEST_F(CongestionControllerTest, PacingBurstAllowed) {
  config_.enable_pacing = true;
  config_.max_pacing_burst = 3;

  CongestionController cc(config_, [this]() { return now_; });

  // Initial burst should be allowed.
  EXPECT_TRUE(cc.check_pacing());
  EXPECT_TRUE(cc.check_pacing());
  EXPECT_TRUE(cc.check_pacing());

  // After burst exhausted, pacing should kick in.
  EXPECT_FALSE(cc.check_pacing());
  EXPECT_TRUE(cc.time_until_next_send().has_value());
}

TEST_F(CongestionControllerTest, PacingIntervalRespected) {
  config_.enable_pacing = true;
  config_.max_pacing_burst = 1;  // Minimal burst.
  config_.min_pacing_interval = std::chrono::microseconds(1000);  // 1ms minimum.

  CongestionController cc(config_, [this]() { return now_; });

  // Use initial burst.
  EXPECT_TRUE(cc.check_pacing());

  // Cannot send immediately.
  EXPECT_FALSE(cc.check_pacing());

  // Get time until next send.
  auto wait_time = cc.time_until_next_send();
  ASSERT_TRUE(wait_time.has_value());
  EXPECT_GT(wait_time->count(), 0);

  // Advance time past pacing interval.
  now_ += std::chrono::milliseconds(100);

  // Should be able to send now.
  EXPECT_TRUE(cc.check_pacing());
}

TEST_F(CongestionControllerTest, PacingRateUpdatedWithRtt) {
  config_.enable_pacing = true;

  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t initial_rate = cc.pacing_rate();

  // Update with higher RTT should decrease pacing rate.
  cc.set_srtt(200ms);
  EXPECT_LT(cc.pacing_rate(), initial_rate);

  // Update with lower RTT should increase pacing rate.
  cc.set_srtt(50ms);
  EXPECT_GT(cc.pacing_rate(), initial_rate);
}

// ========== Reset Tests ==========

TEST_F(CongestionControllerTest, ResetRestoresInitialState) {
  CongestionController cc(config_, [this]() { return now_; });

  // Modify state.
  cc.on_ack(config_.mss * 10);
  cc.on_timeout_loss();

  const std::size_t cwnd_after_loss = cc.cwnd();
  EXPECT_NE(cwnd_after_loss, config_.initial_cwnd);

  // Reset.
  cc.reset();

  EXPECT_EQ(cc.cwnd(), config_.initial_cwnd);
  EXPECT_EQ(cc.ssthresh(), config_.initial_ssthresh);
  EXPECT_EQ(cc.state(), CongestionState::kSlowStart);
}

// ========== Statistics Tests ==========

TEST_F(CongestionControllerTest, PeakCwndTracked) {
  CongestionController cc(config_, [this]() { return now_; });

  // Grow cwnd.
  for (int i = 0; i < 5; ++i) {
    cc.on_ack(config_.mss);
  }

  const std::size_t peak = cc.stats().peak_cwnd;
  EXPECT_GE(peak, config_.initial_cwnd);

  // After loss, peak should remain unchanged.
  cc.on_timeout_loss();
  EXPECT_EQ(cc.stats().peak_cwnd, peak);
}

TEST_F(CongestionControllerTest, AckResetssDupAckCount) {
  CongestionController cc(config_, [this]() { return now_; });

  // Accumulate some dup ACKs.
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();
  EXPECT_EQ(cc.stats().duplicate_acks, 2U);

  // A normal ACK should reset the dup ACK count.
  cc.on_ack(config_.mss);

  // More dup ACKs should count fresh.
  cc.on_duplicate_ack();
  cc.on_duplicate_ack();

  // Third dup ACK should trigger fast retransmit.
  EXPECT_TRUE(cc.on_duplicate_ack());
}

// ========== Edge Cases ==========

TEST_F(CongestionControllerTest, ZeroAckBytesIgnored) {
  CongestionController cc(config_, [this]() { return now_; });

  const std::size_t cwnd_before = cc.cwnd();

  cc.on_ack(0);

  EXPECT_EQ(cc.cwnd(), cwnd_before);
  EXPECT_EQ(cc.stats().cwnd_increases, 0U);
}

TEST_F(CongestionControllerTest, SsthreshMinimum) {
  config_.initial_cwnd = static_cast<std::size_t>(2) * 1400;  // 2 MSS

  CongestionController cc(config_, [this]() { return now_; });

  cc.on_timeout_loss();

  // ssthresh should be at least 2 * MSS.
  EXPECT_GE(cc.ssthresh(), 2 * config_.mss);
}

TEST_F(CongestionControllerTest, CwndMaxEnforced) {
  config_.max_cwnd = static_cast<std::size_t>(5) * 1400;  // Small max for testing.

  CongestionController cc(config_, [this]() { return now_; });

  // Try to grow cwnd beyond max.
  for (int i = 0; i < 100; ++i) {
    cc.on_ack(config_.mss);
  }

  EXPECT_LE(cc.cwnd(), config_.max_cwnd);
}

}  // namespace veil::mux::tests
