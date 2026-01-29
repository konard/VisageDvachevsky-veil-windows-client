#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "common/logging/logger.h"
#include "common/utils/spsc_queue.h"
#include "common/utils/thread_pool.h"
#include "transport/event_loop/event_loop.h"
#include "transport/pipeline/pipeline_processor.h"

namespace veil::transport {

/**
 * Threading mode for the event loop.
 */
enum class ThreadingMode {
  // Original single-threaded mode (default)
  kSingleThreaded,

  // Pipeline mode: separate threads for RX, processing, TX
  kPipeline,
};

/**
 * Configuration for the threaded event loop.
 */
struct ThreadedEventLoopConfig {
  // Base event loop configuration
  EventLoopConfig event_loop_config{};

  // Threading mode
  ThreadingMode threading_mode{ThreadingMode::kSingleThreaded};

  // Pipeline configuration (only used in kPipeline mode)
  PipelineConfig pipeline_config{};

  // Enable detailed performance logging
  bool enable_perf_logging{false};

  // Performance log interval
  std::chrono::seconds perf_log_interval{60};
};

/**
 * Performance metrics for the threaded event loop.
 */
struct ThreadedEventLoopMetrics {
  // Throughput metrics
  std::atomic<std::uint64_t> rx_packets_per_sec{0};
  std::atomic<std::uint64_t> tx_packets_per_sec{0};
  std::atomic<std::uint64_t> rx_bytes_per_sec{0};
  std::atomic<std::uint64_t> tx_bytes_per_sec{0};

  // Latency metrics (microseconds)
  std::atomic<std::uint64_t> avg_rx_latency_us{0};
  std::atomic<std::uint64_t> avg_process_latency_us{0};
  std::atomic<std::uint64_t> avg_tx_latency_us{0};

  // CPU utilization (percentage, 0-100)
  std::atomic<std::uint32_t> rx_thread_cpu{0};
  std::atomic<std::uint32_t> process_thread_cpu{0};
  std::atomic<std::uint32_t> tx_thread_cpu{0};

  void reset() {
    rx_packets_per_sec.store(0);
    tx_packets_per_sec.store(0);
    rx_bytes_per_sec.store(0);
    tx_bytes_per_sec.store(0);
    avg_rx_latency_us.store(0);
    avg_process_latency_us.store(0);
    avg_tx_latency_us.store(0);
    rx_thread_cpu.store(0);
    process_thread_cpu.store(0);
    tx_thread_cpu.store(0);
  }
};

/**
 * Threaded event loop wrapper that supports multiple threading modes.
 *
 * This class wraps the base EventLoop and adds support for multi-threaded
 * packet processing as described in Issue #85.
 *
 * Threading Modes:
 *
 * 1. kSingleThreaded (default):
 *    - Same as the original EventLoop
 *    - All processing on a single thread
 *    - ~500 Mbps throughput
 *
 * 2. kPipeline:
 *    - Three-stage pipeline with separate threads
 *    - RX thread -> Process thread -> TX thread
 *    - Target: 1-2 Gbps throughput
 *
 * Usage:
 * @code
 * ThreadedEventLoopConfig config;
 * config.threading_mode = ThreadingMode::kPipeline;
 *
 * ThreadedEventLoop loop(config);
 * loop.add_session(session, socket);
 * loop.run();  // Blocks until stopped
 * @endcode
 *
 * Thread Safety:
 * - In single-threaded mode: same guarantees as EventLoop
 * - In pipeline mode:
 *   - add_session(), remove_session() must be called before run()
 *   - run(), stop() must be called from a single managing thread
 *   - Internal packet flow is thread-safe via lock-free queues
 *
 * Performance Considerations:
 * - Pipeline mode adds ~10-20 microseconds latency per packet
 * - Pipeline mode significantly increases throughput for crypto-bound workloads
 * - For low-latency requirements, use single-threaded mode
 * - For high-throughput requirements, use pipeline mode
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 * @see Issue #85 for the multi-threading performance improvement initiative.
 */
class ThreadedEventLoop {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  /**
   * Create a threaded event loop.
   *
   * @param config Configuration for the event loop
   */
  explicit ThreadedEventLoop(ThreadedEventLoopConfig config = {});

  /**
   * Destructor. Stops all threads if running.
   */
  ~ThreadedEventLoop();

  // Non-copyable, non-movable
  ThreadedEventLoop(const ThreadedEventLoop&) = delete;
  ThreadedEventLoop& operator=(const ThreadedEventLoop&) = delete;
  ThreadedEventLoop(ThreadedEventLoop&&) = delete;
  ThreadedEventLoop& operator=(ThreadedEventLoop&&) = delete;

  /**
   * Add a transport session to the event loop.
   *
   * In pipeline mode, this creates a PipelineProcessor for the session.
   *
   * @param session The transport session
   * @param socket The UDP socket for the session
   * @param remote The remote endpoint
   * @param on_packet Callback for received packets (after decryption)
   * @param on_error Callback for errors
   * @return true if added successfully
   */
  bool add_session(TransportSession* session,
                   UdpSocket* socket,
                   const UdpEndpoint& remote,
                   PacketHandler on_packet,
                   ErrorHandler on_error = {});

  /**
   * Remove a session from the event loop.
   *
   * @param session_id The session ID to remove
   * @return true if removed
   */
  bool remove_session(SessionId session_id);

  /**
   * Send data through a session.
   *
   * In pipeline mode, this queues the data for encryption and transmission.
   *
   * @param session_id The session ID
   * @param data The plaintext data to send
   * @param stream_id The stream ID for multiplexing
   * @return true if queued successfully
   */
  bool send_data(SessionId session_id,
                 std::span<const std::uint8_t> data,
                 std::uint64_t stream_id = 0);

  /**
   * Run the event loop (blocking).
   *
   * In single-threaded mode, this runs on the calling thread.
   * In pipeline mode, this starts worker threads and blocks until stopped.
   */
  void run();

  /**
   * Stop the event loop.
   *
   * Thread-safe: can be called from any thread.
   */
  void stop();

  /**
   * Check if the event loop is running.
   */
  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

  /**
   * Get the threading mode.
   */
  [[nodiscard]] ThreadingMode threading_mode() const noexcept {
    return config_.threading_mode;
  }

  /**
   * Get performance metrics.
   *
   * Only available in pipeline mode; returns zeros in single-threaded mode.
   */
  [[nodiscard]] const ThreadedEventLoopMetrics& metrics() const noexcept {
    return metrics_;
  }

  /**
   * Get the underlying event loop.
   *
   * Use with caution - direct access bypasses threading guarantees.
   */
  [[nodiscard]] EventLoop& event_loop() noexcept { return *event_loop_; }
  [[nodiscard]] const EventLoop& event_loop() const noexcept { return *event_loop_; }

  /**
   * Schedule a timer (delegates to underlying event loop).
   */
  utils::TimerId schedule_timer(std::chrono::steady_clock::duration after,
                                 utils::TimerCallback callback);

  /**
   * Cancel a timer (delegates to underlying event loop).
   */
  bool cancel_timer(utils::TimerId id);

 private:
  // Session info for pipeline mode
  struct PipelineSessionInfo {
    TransportSession* session{nullptr};
    UdpSocket* socket{nullptr};
    UdpEndpoint remote;
    std::unique_ptr<PipelineProcessor> pipeline;
    PacketHandler on_packet;
    ErrorHandler on_error;
  };

  // Initialize pipeline mode resources
  void init_pipeline_mode();

  // Clean up pipeline mode resources
  void cleanup_pipeline_mode();

  // Performance logging
  void log_performance();

  // Configuration
  ThreadedEventLoopConfig config_;

  // Base event loop (always created)
  std::unique_ptr<EventLoop> event_loop_;

  // Running state
  std::atomic<bool> running_{false};

  // Pipeline mode resources
  std::unordered_map<SessionId, PipelineSessionInfo> pipeline_sessions_;

  // Performance metrics
  ThreadedEventLoopMetrics metrics_;

  // Performance logging state
  TimePoint last_perf_log_;
  std::uint64_t last_rx_packets_{0};
  std::uint64_t last_tx_packets_{0};
  std::uint64_t last_rx_bytes_{0};
  std::uint64_t last_tx_bytes_{0};
};

/**
 * Factory function to create an event loop with the appropriate threading mode.
 *
 * @param mode Threading mode
 * @return Unique pointer to the threaded event loop
 */
inline std::unique_ptr<ThreadedEventLoop> make_event_loop(ThreadingMode mode = ThreadingMode::kSingleThreaded) {
  ThreadedEventLoopConfig config;
  config.threading_mode = mode;
  return std::make_unique<ThreadedEventLoop>(config);
}

/**
 * Factory function to create a high-performance event loop.
 *
 * Uses pipeline mode with optimized settings for maximum throughput.
 *
 * @return Unique pointer to the threaded event loop
 */
inline std::unique_ptr<ThreadedEventLoop> make_high_performance_event_loop() {
  ThreadedEventLoopConfig config;
  config.threading_mode = ThreadingMode::kPipeline;
  config.pipeline_config.rx_queue_capacity = 16384;
  config.pipeline_config.tx_queue_capacity = 16384;
  config.pipeline_config.rx_batch_size = 128;
  config.pipeline_config.tx_batch_size = 128;
  config.enable_perf_logging = true;
  return std::make_unique<ThreadedEventLoop>(config);
}

}  // namespace veil::transport
