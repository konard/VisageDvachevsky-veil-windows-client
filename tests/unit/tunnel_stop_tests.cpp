#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "transport/event_loop/event_loop.h"

namespace veil::tests {

// ============================================================================
// EventLoop Stop Idempotency Tests
// ============================================================================

TEST(EventLoopStopTests, StopBeforeRunIsNoop) {
  transport::EventLoop loop;
  EXPECT_FALSE(loop.is_running());

  // Calling stop() before run() should not crash or cause issues
  loop.stop();
  EXPECT_FALSE(loop.is_running());
}

TEST(EventLoopStopTests, MultipleStopsAreIdempotent) {
  transport::EventLoop loop;

  // Call stop() multiple times — should be safe
  loop.stop();
  loop.stop();
  loop.stop();

  EXPECT_FALSE(loop.is_running());
}

TEST(EventLoopStopTests, StopFromAnotherThread) {
  transport::EventLoop loop;

  // Start the event loop in a background thread
  std::thread runner([&loop]() { loop.run(); });

  // Give event loop time to start
  while (!loop.is_running()) {
    std::this_thread::yield();
  }

  // Stop from another thread (mimics signal handler or stop_service())
  loop.stop();

  runner.join();
  EXPECT_FALSE(loop.is_running());
}

TEST(EventLoopStopTests, ConcurrentStopCalls) {
  transport::EventLoop loop;

  // Start the event loop
  std::thread runner([&loop]() { loop.run(); });

  while (!loop.is_running()) {
    std::this_thread::yield();
  }

  // Multiple threads call stop() concurrently — should not crash
  constexpr int kNumThreads = 4;
  std::vector<std::thread> stoppers;
  stoppers.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    stoppers.emplace_back([&loop]() { loop.stop(); });
  }

  for (auto& t : stoppers) {
    t.join();
  }

  runner.join();
  EXPECT_FALSE(loop.is_running());
}

// ============================================================================
// Atomic Exchange Pattern Tests (Tunnel::stop() logic)
// ============================================================================
// Tests the atomic exchange pattern used by Tunnel::stop() to ensure
// idempotent behavior. This validates the core synchronization mechanism
// without requiring a full Tunnel instance.

class AtomicStopGuardTest : public ::testing::Test {
 protected:
  // Mimics Tunnel's running_ + stop() pattern
  std::atomic<bool> running_{false};
  std::atomic<int> stop_count_{0};  // Tracks actual stop operations performed

  // Simulates the guarded stop() logic from Tunnel::stop()
  void stop() {
    if (!running_.exchange(false)) {
      return;  // Already stopped
    }
    stop_count_.fetch_add(1);
  }

  void start() { running_.store(true); }
};

TEST_F(AtomicStopGuardTest, StopWhenNotRunningIsNoop) {
  EXPECT_FALSE(running_.load());
  stop();
  EXPECT_EQ(stop_count_.load(), 0);
}

TEST_F(AtomicStopGuardTest, StopWhenRunningExecutesOnce) {
  start();
  stop();
  EXPECT_EQ(stop_count_.load(), 1);
  EXPECT_FALSE(running_.load());
}

TEST_F(AtomicStopGuardTest, DoubleStopExecutesOnce) {
  start();
  stop();
  stop();
  EXPECT_EQ(stop_count_.load(), 1);
}

TEST_F(AtomicStopGuardTest, TripleStopExecutesOnce) {
  start();
  stop();
  stop();
  stop();
  EXPECT_EQ(stop_count_.load(), 1);
}

TEST_F(AtomicStopGuardTest, ConcurrentStopsExecuteExactlyOnce) {
  start();

  constexpr int kNumThreads = 8;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([this]() { stop(); });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Exactly one thread should have performed the actual stop
  EXPECT_EQ(stop_count_.load(), 1);
  EXPECT_FALSE(running_.load());
}

TEST_F(AtomicStopGuardTest, RestartAndStopCycle) {
  // Verify the pattern works correctly across start/stop cycles
  for (int cycle = 0; cycle < 5; ++cycle) {
    start();
    stop();
  }
  EXPECT_EQ(stop_count_.load(), 5);
  EXPECT_FALSE(running_.load());
}

}  // namespace veil::tests
