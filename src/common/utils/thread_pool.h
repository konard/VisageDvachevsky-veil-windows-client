#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/logging/logger.h"

namespace veil::utils {

/**
 * A simple thread pool for executing tasks asynchronously.
 *
 * This thread pool is designed to support the multi-threaded pipeline
 * architecture (Issue #85). It provides:
 * - Fixed number of worker threads
 * - Task submission with futures
 * - Graceful shutdown
 *
 * Thread Safety: All public methods are thread-safe.
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 * @see Issue #85 for the multi-threading performance improvement initiative.
 */
class ThreadPool {
 public:
  /**
   * Create a thread pool with the specified number of worker threads.
   *
   * @param num_threads Number of worker threads. If 0, uses hardware_concurrency().
   */
  explicit ThreadPool(std::size_t num_threads = 0)
      : running_(true), active_tasks_(0) {
    if (num_threads == 0) {
      num_threads = std::thread::hardware_concurrency();
      if (num_threads == 0) {
        num_threads = 4;  // Fallback
      }
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
      workers_.emplace_back([this, i] { worker_loop(i); });
    }
    LOG_DEBUG("ThreadPool created with {} worker threads", num_threads);
  }

  /**
   * Destructor. Stops all worker threads gracefully.
   */
  ~ThreadPool() {
    stop();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    LOG_DEBUG("ThreadPool destroyed");
  }

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  /**
   * Submit a task for execution.
   *
   * @tparam F Callable type
   * @tparam Args Argument types
   * @param f The callable to execute
   * @param args Arguments to pass to the callable
   * @return std::future for the result
   */
  template <typename F, typename... Args>
  auto submit(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

    // Use lambda instead of std::bind per clang-tidy modernize-avoid-bind
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
          return func(std::move(captured_args)...);
        });

    std::future<ReturnType> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_.load()) {
        throw std::runtime_error("ThreadPool is stopped");
      }
      tasks_.emplace([task] { (*task)(); });
    }
    condition_.notify_one();

    return result;
  }

  /**
   * Submit a task without waiting for result.
   * More efficient than submit() when you don't need the result.
   *
   * @tparam F Callable type
   * @param f The callable to execute
   */
  template <typename F>
  void submit_detached(F&& f) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_.load()) {
        return;
      }
      tasks_.emplace(std::forward<F>(f));
    }
    condition_.notify_one();
  }

  /**
   * Stop accepting new tasks and wait for all pending tasks to complete.
   */
  void stop() {
    running_.store(false);
    condition_.notify_all();
  }

  /**
   * Check if the thread pool is running.
   */
  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

  /**
   * Get the number of worker threads.
   */
  [[nodiscard]] std::size_t num_threads() const noexcept { return workers_.size(); }

  /**
   * Get the number of pending tasks.
   */
  [[nodiscard]] std::size_t pending_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
  }

  /**
   * Get the number of currently active (executing) tasks.
   */
  [[nodiscard]] std::size_t active_tasks() const noexcept { return active_tasks_.load(); }

  /**
   * Wait until all submitted tasks are completed.
   * Does not prevent new task submission.
   */
  void wait_all() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_condition_.wait(lock, [this] {
      return tasks_.empty() && active_tasks_.load() == 0;
    });
  }

 private:
  void worker_loop(std::size_t thread_id) {
    LOG_DEBUG("ThreadPool worker {} started", thread_id);

    while (true) {
      std::function<void()> task;

      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !running_.load() || !tasks_.empty(); });

        if (!running_.load() && tasks_.empty()) {
          LOG_DEBUG("ThreadPool worker {} stopping", thread_id);
          return;
        }

        if (!tasks_.empty()) {
          task = std::move(tasks_.front());
          tasks_.pop();
          ++active_tasks_;
        }
      }

      if (task) {
        try {
          task();
        } catch (const std::exception& e) {
          // NOLINTNEXTLINE(bugprone-lambda-function-name) - LOG_ERROR macro uses __FUNCTION__
          LOG_ERROR("ThreadPool worker {} caught exception: {}", thread_id, e.what());
        } catch (...) {
          // NOLINTNEXTLINE(bugprone-lambda-function-name) - LOG_ERROR macro uses __FUNCTION__
          LOG_ERROR("ThreadPool worker {} caught unknown exception", thread_id);
        }

        --active_tasks_;

        // Notify wait_all() if we're idle
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty() && active_tasks_.load() == 0) {
          idle_condition_.notify_all();
        }
      }
    }
  }

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::condition_variable idle_condition_;
  std::atomic<bool> running_;
  std::atomic<std::size_t> active_tasks_;
};

/**
 * A dedicated worker thread for continuous processing.
 *
 * Unlike ThreadPool which executes discrete tasks, DedicatedWorker runs
 * a single function continuously until stopped. This is useful for
 * pipeline stages that process a stream of data.
 *
 * Thread Safety:
 * - start(), stop(), join() must be called from a single managing thread
 * - is_running() is thread-safe
 */
class DedicatedWorker {
 public:
  /**
   * Create a dedicated worker (does not start automatically).
   *
   * @param name Name for logging/debugging
   */
  explicit DedicatedWorker(std::string name = "Worker") : name_(std::move(name)), running_(false) {}

  /**
   * Destructor. Stops and joins the worker thread if running.
   */
  ~DedicatedWorker() {
    stop();
    join();
  }

  // Non-copyable, non-movable
  DedicatedWorker(const DedicatedWorker&) = delete;
  DedicatedWorker& operator=(const DedicatedWorker&) = delete;
  DedicatedWorker(DedicatedWorker&&) = delete;
  DedicatedWorker& operator=(DedicatedWorker&&) = delete;

  /**
   * Start the worker thread with the given function.
   * The function should check is_running() periodically and exit when false.
   *
   * @tparam F Callable type (should have signature void())
   * @param work_fn The function to run continuously
   * @return true if started, false if already running
   */
  template <typename F>
  bool start(F&& work_fn) {
    if (running_.load()) {
      return false;
    }

    running_.store(true);
    thread_ = std::thread([this, fn = std::forward<F>(work_fn)]() mutable {
      LOG_DEBUG("{} thread started", name_);
      try {
        fn();
      } catch (const std::exception& e) {
        // NOLINTNEXTLINE(bugprone-lambda-function-name) - LOG_ERROR macro uses __FUNCTION__
        LOG_ERROR("{} thread caught exception: {}", name_, e.what());
      } catch (...) {
        // NOLINTNEXTLINE(bugprone-lambda-function-name) - LOG_ERROR macro uses __FUNCTION__
        LOG_ERROR("{} thread caught unknown exception", name_);
      }
      // Ensure running_ is false when thread exits (even on exception)
      running_.store(false);
      LOG_DEBUG("{} thread stopped", name_);
    });

    return true;
  }

  /**
   * Signal the worker to stop.
   * The worker function should check is_running() and exit.
   */
  void stop() noexcept { running_.store(false); }

  /**
   * Wait for the worker thread to finish.
   */
  void join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /**
   * Check if the worker should continue running.
   * Worker functions should check this periodically.
   */
  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

  /**
   * Get the worker name.
   */
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

 private:
  std::string name_;
  std::atomic<bool> running_;
  std::thread thread_;
};

}  // namespace veil::utils
