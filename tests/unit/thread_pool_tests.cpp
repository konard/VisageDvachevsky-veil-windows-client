#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "common/utils/thread_pool.h"

namespace veil::utils {
namespace {

class ThreadPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic ThreadPool tests

TEST_F(ThreadPoolTest, DefaultConstruction) {
  ThreadPool pool;
  EXPECT_TRUE(pool.is_running());
  EXPECT_GT(pool.num_threads(), 0);
  EXPECT_EQ(pool.pending_tasks(), 0);
  EXPECT_EQ(pool.active_tasks(), 0);
}

TEST_F(ThreadPoolTest, CustomThreadCount) {
  ThreadPool pool(4);
  EXPECT_EQ(pool.num_threads(), 4);
}

TEST_F(ThreadPoolTest, SubmitAndGetResult) {
  ThreadPool pool(2);

  auto future = pool.submit([]() { return 42; });
  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, SubmitWithArguments) {
  ThreadPool pool(2);

  auto future = pool.submit([](int a, int b) { return a + b; }, 10, 32);
  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, SubmitVoidTask) {
  ThreadPool pool(2);
  std::atomic<bool> executed{false};

  auto future = pool.submit([&executed]() { executed.store(true); });
  future.get();

  EXPECT_TRUE(executed.load());
}

TEST_F(ThreadPoolTest, SubmitDetached) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  pool.submit_detached([&counter]() { counter.fetch_add(1); });
  pool.submit_detached([&counter]() { counter.fetch_add(1); });
  pool.submit_detached([&counter]() { counter.fetch_add(1); });

  pool.wait_all();
  EXPECT_EQ(counter.load(), 3);
}

TEST_F(ThreadPoolTest, MultipleTasks) {
  ThreadPool pool(4);
  std::vector<std::future<int>> futures;
  futures.reserve(100);

  for (int i = 0; i < 100; ++i) {
    futures.push_back(pool.submit([i]() { return i * 2; }));
  }

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(futures[static_cast<std::size_t>(i)].get(), i * 2);
  }
}

TEST_F(ThreadPoolTest, WaitAll) {
  ThreadPool pool(2);
  std::atomic<int> completed{0};

  for (int i = 0; i < 10; ++i) {
    pool.submit_detached([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      completed.fetch_add(1);
    });
  }

  pool.wait_all();
  EXPECT_EQ(completed.load(), 10);
}

TEST_F(ThreadPoolTest, StopAcceptsNoNewTasks) {
  ThreadPool pool(2);

  pool.stop();
  EXPECT_FALSE(pool.is_running());

  EXPECT_THROW(pool.submit([]() { return 1; }), std::runtime_error);
}

TEST_F(ThreadPoolTest, TaskException) {
  ThreadPool pool(2);

  auto future = pool.submit([]() -> int {
    throw std::runtime_error("test exception");
    return 0;
  });

  EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(ThreadPoolTest, TaskExceptionDoesNotCrash) {
  ThreadPool pool(2);
  std::atomic<int> completed{0};

  // Submit a task that throws
  pool.submit_detached([]() { throw std::runtime_error("test"); });

  // Submit more tasks after the exception
  for (int i = 0; i < 5; ++i) {
    pool.submit_detached([&completed]() { completed.fetch_add(1); });
  }

  pool.wait_all();
  EXPECT_EQ(completed.load(), 5);
}

TEST_F(ThreadPoolTest, ConcurrentSubmit) {
  ThreadPool pool(4);
  std::atomic<int> sum{0};
  std::vector<std::thread> submitters;
  submitters.reserve(4);

  for (int t = 0; t < 4; ++t) {
    submitters.emplace_back([&pool, &sum]() {
      for (int i = 0; i < 100; ++i) {
        pool.submit_detached([&sum]() { sum.fetch_add(1); });
      }
    });
  }

  for (auto& t : submitters) {
    t.join();
  }

  pool.wait_all();
  EXPECT_EQ(sum.load(), 400);
}

// DedicatedWorker tests

class DedicatedWorkerTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(DedicatedWorkerTest, DefaultConstruction) {
  DedicatedWorker worker("TestWorker");
  EXPECT_FALSE(worker.is_running());
  EXPECT_EQ(worker.name(), "TestWorker");
}

TEST_F(DedicatedWorkerTest, StartAndStop) {
  DedicatedWorker worker("TestWorker");
  std::atomic<int> iterations{0};

  worker.start([&worker, &iterations]() {
    while (worker.is_running()) {
      iterations.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  EXPECT_TRUE(worker.is_running());

  // Let it run a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  worker.stop();
  worker.join();

  EXPECT_FALSE(worker.is_running());
  EXPECT_GT(iterations.load(), 0);
}

TEST_F(DedicatedWorkerTest, StopWithoutStart) {
  DedicatedWorker worker("TestWorker");
  worker.stop();  // Should not crash
  worker.join();  // Should not block
}

TEST_F(DedicatedWorkerTest, DoubleStart) {
  DedicatedWorker worker("TestWorker");

  bool started1 = worker.start([&worker]() {
    while (worker.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
  EXPECT_TRUE(started1);

  bool started2 = worker.start([]() {});
  EXPECT_FALSE(started2);  // Already running

  worker.stop();
  worker.join();
}

TEST_F(DedicatedWorkerTest, WorkerException) {
  DedicatedWorker worker("TestWorker");

  worker.start([]() { throw std::runtime_error("test exception"); });

  // Worker should exit but not crash the process
  worker.join();
  EXPECT_FALSE(worker.is_running());
}

TEST_F(DedicatedWorkerTest, DestructorStopsWorker) {
  std::atomic<bool> worker_stopped{false};

  {
    DedicatedWorker worker("TestWorker");
    worker.start([&worker, &worker_stopped]() {
      while (worker.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      worker_stopped.store(true);
    });
    // Destructor called here
  }

  // Give it a moment to clean up
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_TRUE(worker_stopped.load());
}

TEST_F(DedicatedWorkerTest, ProducerConsumerPattern) {
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<bool> done{false};

  DedicatedWorker producer("Producer");
  DedicatedWorker consumer("Consumer");

  // Simple shared state (in practice, use a queue)
  std::atomic<int> shared_value{-1};

  producer.start([&]() {
    for (int i = 0; i < 100 && producer.is_running(); ++i) {
      while (shared_value.load() != -1 && producer.is_running()) {
        std::this_thread::yield();
      }
      if (producer.is_running()) {
        shared_value.store(i);
        produced.fetch_add(1);
      }
    }
    done.store(true);
  });

  consumer.start([&]() {
    while (consumer.is_running()) {
      int val = shared_value.exchange(-1);
      if (val != -1) {
        consumed.fetch_add(1);
      }
      if (done.load() && shared_value.load() == -1) {
        break;
      }
      std::this_thread::yield();
    }
  });

  producer.join();
  consumer.stop();
  consumer.join();

  EXPECT_EQ(produced.load(), 100);
  EXPECT_EQ(consumed.load(), 100);
}

}  // namespace
}  // namespace veil::utils
