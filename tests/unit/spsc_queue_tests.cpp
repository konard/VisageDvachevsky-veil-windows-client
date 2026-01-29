#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "common/utils/spsc_queue.h"

namespace veil::utils {
namespace {

class SpscQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic functionality tests

TEST_F(SpscQueueTest, DefaultConstruction) {
  SpscQueue<int> queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size_approx(), 0);
  EXPECT_GE(queue.capacity(), 1024);  // Default capacity
}

TEST_F(SpscQueueTest, CustomCapacity) {
  SpscQueue<int> queue(100);
  // Capacity is rounded up to power of 2
  EXPECT_GE(queue.capacity(), 100);
  // Should be 128 - 1 = 127 (next power of 2 minus sentinel)
  EXPECT_LE(queue.capacity(), 256);
}

TEST_F(SpscQueueTest, PushAndPop) {
  SpscQueue<int> queue(16);

  EXPECT_TRUE(queue.try_push(42));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size_approx(), 1);

  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(queue.empty());
}

TEST_F(SpscQueueTest, PushMultipleAndPop) {
  SpscQueue<int> queue(16);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.try_push(i));
  }
  EXPECT_EQ(queue.size_approx(), 10);

  for (int i = 0; i < 10; ++i) {
    auto result = queue.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, i);
  }
  EXPECT_TRUE(queue.empty());
}

TEST_F(SpscQueueTest, PopFromEmpty) {
  SpscQueue<int> queue(16);
  auto result = queue.try_pop();
  EXPECT_FALSE(result.has_value());
}

TEST_F(SpscQueueTest, PushToFull) {
  SpscQueue<int> queue(8);  // Actually 8 - 1 = 7 capacity due to sentinel
  const auto capacity = queue.capacity();

  // Fill the queue
  for (std::size_t i = 0; i < capacity; ++i) {
    EXPECT_TRUE(queue.try_push(static_cast<int>(i))) << "Failed at i=" << i;
  }

  // Next push should fail
  EXPECT_FALSE(queue.try_push(999));

  // Pop one and try again
  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0);

  // Now push should succeed
  EXPECT_TRUE(queue.try_push(999));
}

TEST_F(SpscQueueTest, MoveOnlyType) {
  SpscQueue<std::unique_ptr<int>> queue(16);

  auto ptr = std::make_unique<int>(42);
  EXPECT_TRUE(queue.try_push(std::move(ptr)));
  EXPECT_EQ(ptr, nullptr);  // Moved

  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(**result, 42);
}

TEST_F(SpscQueueTest, CopyableType) {
  SpscQueue<std::string> queue(16);

  std::string s = "hello";
  EXPECT_TRUE(queue.try_push(s));  // Copy
  EXPECT_EQ(s, "hello");  // Original unchanged

  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "hello");
}

TEST_F(SpscQueueTest, VectorType) {
  SpscQueue<std::vector<int>> queue(16);

  std::vector<int> v = {1, 2, 3, 4, 5};
  EXPECT_TRUE(queue.try_push(std::move(v)));

  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 5);
  EXPECT_EQ((*result)[0], 1);
  EXPECT_EQ((*result)[4], 5);
}

// Concurrency tests

TEST_F(SpscQueueTest, SingleProducerSingleConsumer) {
  SpscQueue<int> queue(1024);
  constexpr int num_items = 10000;
  std::atomic<bool> producer_done{false};
  std::atomic<int> consumed_count{0};
  std::vector<int> consumed_values;
  consumed_values.reserve(num_items);

  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < num_items; ++i) {
      while (!queue.try_push(i)) {
        // Spin until push succeeds
        std::this_thread::yield();
      }
    }
    producer_done.store(true);
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (true) {
      auto result = queue.try_pop();
      if (result) {
        consumed_values.push_back(*result);
        consumed_count.fetch_add(1);
      } else if (producer_done.load() && queue.empty()) {
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  // Verify all items were consumed in order
  EXPECT_EQ(consumed_values.size(), num_items);
  for (int i = 0; i < num_items; ++i) {
    EXPECT_EQ(consumed_values[static_cast<std::size_t>(i)], i) << "Mismatch at index " << i;
  }
}

TEST_F(SpscQueueTest, HighThroughput) {
  SpscQueue<int> queue(8192);
  constexpr int num_items = 1000000;
  std::atomic<bool> producer_done{false};
  std::atomic<int> consumed_count{0};

  auto start = std::chrono::steady_clock::now();

  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < num_items; ++i) {
      while (!queue.try_push(i)) {
        // Spin
      }
    }
    producer_done.store(true);
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (true) {
      auto result = queue.try_pop();
      if (result) {
        consumed_count.fetch_add(1);
      } else if (producer_done.load() && queue.empty()) {
        break;
      }
    }
  });

  producer.join();
  consumer.join();

  auto end = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  EXPECT_EQ(consumed_count.load(), num_items);

  // Log throughput (not a hard requirement, just informational)
  if (duration_ms > 0) {
    double throughput = static_cast<double>(num_items) / static_cast<double>(duration_ms) * 1000.0;
    std::cout << "SPSC Queue throughput: " << throughput << " items/sec ("
              << duration_ms << "ms for " << num_items << " items)\n";
  }
}

// MPMC Queue tests

TEST_F(SpscQueueTest, MpmcBasicOperations) {
  MpmcQueue<int> queue(16);

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);

  EXPECT_TRUE(queue.try_push(42));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1);

  auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
}

TEST_F(SpscQueueTest, MpmcMultipleProducers) {
  MpmcQueue<int> queue(1024);
  constexpr int items_per_producer = 1000;
  constexpr int num_producers = 4;
  std::atomic<int> produced_count{0};
  std::atomic<int> consumed_count{0};
  std::atomic<bool> done{false};

  // Multiple producer threads
  std::vector<std::thread> producers;
  producers.reserve(static_cast<std::size_t>(num_producers));
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;
        while (!queue.try_push(value)) {
          std::this_thread::yield();
        }
        produced_count.fetch_add(1);
      }
    });
  }

  // Single consumer thread
  std::thread consumer([&]() {
    while (true) {
      auto result = queue.try_pop();
      if (result) {
        consumed_count.fetch_add(1);
      } else if (done.load() && queue.empty()) {
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  for (auto& p : producers) {
    p.join();
  }
  done.store(true);
  consumer.join();

  EXPECT_EQ(produced_count.load(), num_producers * items_per_producer);
  EXPECT_EQ(consumed_count.load(), num_producers * items_per_producer);
}

// Power of 2 calculation test

TEST_F(SpscQueueTest, PowerOf2Capacity) {
  // Test various capacities and verify they're rounded up correctly
  SpscQueue<int> q1(1);
  EXPECT_GE(q1.capacity(), 1);

  SpscQueue<int> q2(7);
  EXPECT_GE(q2.capacity(), 7);

  SpscQueue<int> q3(17);
  EXPECT_GE(q3.capacity(), 17);

  SpscQueue<int> q4(100);
  EXPECT_GE(q4.capacity(), 100);

  SpscQueue<int> q5(1000);
  EXPECT_GE(q5.capacity(), 1000);
}

}  // namespace
}  // namespace veil::utils
