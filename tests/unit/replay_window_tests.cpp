#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

#include "common/session/replay_window.h"
#include "common/session/session_rotator.h"

namespace veil::tests {

TEST(ReplayWindowTests, RejectsDuplicatesAndOldSequences) {
  session::ReplayWindow window(64);
  EXPECT_TRUE(window.mark_and_check(10));
  EXPECT_FALSE(window.mark_and_check(10));
  EXPECT_TRUE(window.mark_and_check(11));
  EXPECT_FALSE(window.mark_and_check(11));
  EXPECT_FALSE(window.mark_and_check(10));
}

TEST(ReplayWindowTests, SlidesWindowForward) {
  session::ReplayWindow window(8);
  EXPECT_TRUE(window.mark_and_check(1));
  EXPECT_TRUE(window.mark_and_check(2));
  EXPECT_TRUE(window.mark_and_check(9));
  EXPECT_TRUE(window.mark_and_check(10));
  EXPECT_FALSE(window.mark_and_check(1));
}

// Issue #78: Test unmark functionality for retransmission after decryption failure
TEST(ReplayWindowTests, UnmarkAllowsRetransmission) {
  session::ReplayWindow window(64);

  // Mark sequence 100
  EXPECT_TRUE(window.mark_and_check(100));

  // Duplicate should be rejected
  EXPECT_FALSE(window.mark_and_check(100));

  // Unmark (simulating decryption failure)
  // Issue #233: unmark() now returns true on success, false on blacklist
  EXPECT_TRUE(window.unmark(100));

  // Should now be accepted again
  EXPECT_TRUE(window.mark_and_check(100));

  // And rejected again as duplicate
  EXPECT_FALSE(window.mark_and_check(100));
}

// Issue #78: Test unmark with window advancement
TEST(ReplayWindowTests, UnmarkWithWindowAdvancement) {
  session::ReplayWindow window(64);

  // Mark several sequences
  EXPECT_TRUE(window.mark_and_check(100));
  EXPECT_TRUE(window.mark_and_check(101));
  EXPECT_TRUE(window.mark_and_check(102));

  // Advance window
  EXPECT_TRUE(window.mark_and_check(130));

  // Sequence 100 is within window but already marked
  EXPECT_FALSE(window.mark_and_check(100));

  // Unmark and verify retransmission works
  EXPECT_TRUE(window.unmark(100));
  EXPECT_TRUE(window.mark_and_check(100));
}

// Issue #233: Test that unmark has a retry limit to prevent DoS
TEST(ReplayWindowTests, UnmarkRetryLimitPreventsDos) {
  session::ReplayWindow window(64);

  // Mark sequence 100
  EXPECT_TRUE(window.mark_and_check(100));

  // First unmark - should succeed (failure count = 1)
  EXPECT_TRUE(window.unmark(100));
  EXPECT_TRUE(window.mark_and_check(100));

  // Second unmark - should succeed (failure count = 2)
  EXPECT_TRUE(window.unmark(100));
  EXPECT_TRUE(window.mark_and_check(100));

  // Third unmark - should succeed (failure count = 3)
  EXPECT_TRUE(window.unmark(100));
  EXPECT_TRUE(window.mark_and_check(100));

  // Fourth unmark - should FAIL (failure count would exceed MAX_UNMARK_RETRIES=3)
  // Sequence is now blacklisted
  EXPECT_FALSE(window.unmark(100));

  // Even after blacklist, the bit should still be set from the last mark_and_check
  // So attempting to mark_and_check again should fail (already marked)
  EXPECT_FALSE(window.mark_and_check(100));
}

// Issue #233: Test DoS scenario with repeated attack packets
TEST(ReplayWindowTests, DosAttackMitigationWithRepeatedPackets) {
  session::ReplayWindow window(1024);

  const std::uint64_t attack_seq = 500;

  // Simulate DoS attack: attacker sends same malformed packet repeatedly
  // Each packet: mark -> fail decryption -> unmark -> repeat

  // First 3 attempts should allow unmark (legitimate retransmission scenario)
  for (int attempt = 1; attempt <= 3; ++attempt) {
    EXPECT_TRUE(window.mark_and_check(attack_seq)) << "Attempt " << attempt << " mark failed";
    EXPECT_TRUE(window.unmark(attack_seq)) << "Attempt " << attempt << " unmark failed";
  }

  // 4th attempt: mark succeeds, but unmark should fail (blacklisted)
  EXPECT_TRUE(window.mark_and_check(attack_seq));
  EXPECT_FALSE(window.unmark(attack_seq)) << "Should blacklist after 3 retries";

  // Further attempts to unmark should continue to fail
  EXPECT_FALSE(window.unmark(attack_seq));
  EXPECT_FALSE(window.unmark(attack_seq));

  // The sequence is still marked (not unmarked), so mark_and_check fails
  EXPECT_FALSE(window.mark_and_check(attack_seq));
}

// Issue #233: Test that failure tracking cleans up old sequences
TEST(ReplayWindowTests, FailureTrackingCleanupPreventsMemoryLeak) {
  session::ReplayWindow window(64);

  // Fill window with sequences, each failing once
  for (std::uint64_t seq = 100; seq < 200; ++seq) {
    EXPECT_TRUE(window.mark_and_check(seq));
    EXPECT_TRUE(window.unmark(seq));  // Failure count = 1
  }

  // Advance window far beyond initial sequences (200 sequences away)
  // This should trigger cleanup of old failure tracking entries
  EXPECT_TRUE(window.mark_and_check(400));

  // Now sequences 100-335 are outside the window (400 - 64 = 336)
  // Their failure counts should have been cleaned up

  // Verify that sequence 100 (now outside window) cannot be unmarked
  // because it's outside the window range, not because of failure count
  EXPECT_FALSE(window.mark_and_check(100));  // Too old, outside window

  // Verify sequences just within window (e.g., 350) still work normally
  EXPECT_TRUE(window.mark_and_check(350));
  EXPECT_TRUE(window.unmark(350));  // Should succeed (was already tracked, count = 1)
  EXPECT_TRUE(window.mark_and_check(350));  // Should succeed after unmark
}

// Issue #233: Test mixed legitimate and attack traffic
TEST(ReplayWindowTests, MixedLegitimateAndAttackTraffic) {
  session::ReplayWindow window(128);

  // Legitimate traffic: sequences 1000-1010
  for (std::uint64_t seq = 1000; seq <= 1010; ++seq) {
    EXPECT_TRUE(window.mark_and_check(seq));
  }

  // Attacker tries to exploit sequence 1005 (DoS attempt)
  // First mark_and_check will fail (already marked), but unmark it first
  EXPECT_FALSE(window.mark_and_check(1005));  // Already marked

  // Attacker causes 3 decryption failures
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(window.unmark(1005));
    EXPECT_TRUE(window.mark_and_check(1005));
  }

  // 4th failure attempt - unmark should fail
  EXPECT_FALSE(window.unmark(1005));

  // Meanwhile, other legitimate sequences should work normally
  EXPECT_TRUE(window.mark_and_check(1011));
  EXPECT_TRUE(window.mark_and_check(1012));

  // Even a single legitimate retransmission should work
  EXPECT_TRUE(window.unmark(1011));
  EXPECT_TRUE(window.mark_and_check(1011));
}

// Issue #233: Test unmark return value semantics
TEST(ReplayWindowTests, UnmarkReturnValueSemantics) {
  session::ReplayWindow window(64);

  // unmark on uninitialized window returns false
  EXPECT_FALSE(window.unmark(100));

  // Initialize window
  EXPECT_TRUE(window.mark_and_check(100));

  // unmark for sequence > highest returns false
  EXPECT_FALSE(window.unmark(200));

  // unmark for sequence outside window returns false
  EXPECT_TRUE(window.mark_and_check(200));  // highest = 200
  EXPECT_FALSE(window.unmark(100));  // 200 - 100 = 100 > window_size(64)

  // Valid unmark returns true
  EXPECT_TRUE(window.mark_and_check(150));
  EXPECT_TRUE(window.unmark(150));

  // After blacklist, unmark returns false
  for (int i = 0; i < 3; ++i) {
    window.mark_and_check(160);
    window.unmark(160);
  }
  window.mark_and_check(160);
  EXPECT_FALSE(window.unmark(160));  // Blacklisted
}

TEST(SessionRotatorTests, RotatesAfterThresholds) {
  using namespace std::chrono_literals;
  session::SessionRotator rotator(1s, 2);
  const auto first = rotator.current();
  EXPECT_FALSE(rotator.should_rotate(1, std::chrono::steady_clock::now()));
  EXPECT_TRUE(rotator.should_rotate(2, std::chrono::steady_clock::now()));
  const auto second = rotator.rotate(std::chrono::steady_clock::now());
  EXPECT_NE(first, second);
  EXPECT_FALSE(rotator.should_rotate(0, std::chrono::steady_clock::now()));
  // With jitter the interval can be up to ~1.67x base (1s), so wait long enough
  // to exceed the maximum jittered interval.
  std::this_thread::sleep_for(2000ms);
  EXPECT_TRUE(rotator.should_rotate(0, std::chrono::steady_clock::now()));
}

// Issue #83: Verify jittered rotation intervals are non-uniform.
TEST(SessionRotatorTests, JitteredIntervalsAreNonUniform) {
  using namespace std::chrono_literals;
  // Create multiple rotators and check rotation status at the base interval.
  // With jitter, some intervals will be shorter than base (rotated) and some
  // longer (not rotated). We use 100 trials to reduce flakiness.
  constexpr int kTrials = 100;
  int rotated_at_base = 0;

  for (int i = 0; i < kTrials; ++i) {
    session::SessionRotator rotator(1s, 1000000);
    // Check exactly at base interval (1000ms). With jitter range [~667ms, ~1667ms],
    // intervals shorter than 1000ms should have expired, and longer ones should not.
    auto start = std::chrono::steady_clock::now();
    auto check_point = start + 1000ms;
    if (rotator.should_rotate(0, check_point)) {
      ++rotated_at_base;
    }
  }

  // With jitter, we expect SOME variation: not all true and not all false.
  // ~33% of intervals are shorter than base (subtract path).
  // Allow the test to pass if at least 1 rotated and at least 1 did not.
  EXPECT_GT(rotated_at_base, 0)
      << "No intervals were below 1000ms — jitter may not be applied";
  EXPECT_LT(rotated_at_base, kTrials)
      << "All intervals were below 1000ms — jitter may not be applied";
}

// Issue #83: Verify jittered interval stays within bounds.
TEST(SessionRotatorTests, JitteredIntervalBounds) {
  using namespace std::chrono_literals;
  // With a 3s base interval, jitter range is 1s (base/3).
  // Minimum: base * 0.67 = ~2s, Maximum: base * 1.67 = ~5s.
  // Safety floor: base * 0.25 = 0.75s.
  // So interval should be in [~2s, ~5s].
  constexpr int kTrials = 50;
  for (int i = 0; i < kTrials; ++i) {
    session::SessionRotator rotator(3s, 1000000);
    auto start = std::chrono::steady_clock::now();

    // Should NOT rotate at 1.5s (well below minimum ~2s).
    EXPECT_FALSE(rotator.should_rotate(0, start + 1500ms))
        << "Rotated too early at 1.5s with 3s base";

    // Should rotate at 6s (well above maximum ~5s).
    EXPECT_TRUE(rotator.should_rotate(0, start + 6000ms))
        << "Did not rotate at 6s with 3s base";
  }
}

// Issue #83: Verify each rotation recomputes a new jittered interval.
TEST(SessionRotatorTests, RotationRecomputesJitter) {
  using namespace std::chrono_literals;
  session::SessionRotator rotator(10s, 1000000);

  // Perform multiple rotations and check that rotation always resets the timer.
  auto now = std::chrono::steady_clock::now();
  for (int i = 0; i < 10; ++i) {
    rotator.rotate(now);
    // Immediately after rotation, should not need to rotate.
    EXPECT_FALSE(rotator.should_rotate(0, now))
        << "Should not rotate immediately after rotation (iteration " << i << ")";
    // Advance well past maximum jittered interval (base * 1.67 = ~16.7s).
    now += 20s;
    EXPECT_TRUE(rotator.should_rotate(0, now))
        << "Should rotate after 20s with 10s base (iteration " << i << ")";
  }
}

}  // namespace veil::tests
