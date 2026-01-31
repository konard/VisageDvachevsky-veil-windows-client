#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "common/logging/logger.h"
#include "common/utils/spsc_queue.h"
#include "common/utils/thread_pool.h"
#include "transport/mux/mux_codec.h"
#include "transport/udp_socket/udp_socket.h"

namespace veil::transport {

// Forward declarations
class TransportSession;

/**
 * Configuration for the pipeline processor.
 */
struct PipelineConfig {
  // Queue capacity for inter-stage communication
  std::size_t rx_queue_capacity{4096};   // RX -> Process queue
  std::size_t tx_queue_capacity{4096};   // Process -> TX queue

  // Batch sizes for processing
  std::size_t rx_batch_size{64};         // Packets to batch before processing
  std::size_t tx_batch_size{64};         // Packets to batch before sending

  // Timeouts for batch processing (microseconds)
  std::uint32_t rx_batch_timeout_us{100};
  std::uint32_t tx_batch_timeout_us{100};

  // Busy-wait vs sleep threshold
  std::size_t busy_wait_threshold{10};   // Busy-wait if queue > threshold

  // Statistics logging interval (0 = disabled)
  std::chrono::seconds stats_interval{60};

  // Enable verbose tracing (for debugging)
  bool enable_tracing{false};
};

/**
 * Statistics for pipeline performance monitoring.
 */
struct PipelineStats {
  // Packet counts
  std::atomic<std::uint64_t> rx_packets{0};
  std::atomic<std::uint64_t> tx_packets{0};
  std::atomic<std::uint64_t> processed_packets{0};

  // Byte counts
  std::atomic<std::uint64_t> rx_bytes{0};
  std::atomic<std::uint64_t> tx_bytes{0};

  // Error counts
  std::atomic<std::uint64_t> decrypt_errors{0};
  std::atomic<std::uint64_t> queue_full_drops{0};

  // Queue statistics
  std::atomic<std::uint64_t> rx_queue_max_size{0};
  std::atomic<std::uint64_t> tx_queue_max_size{0};

  // Timing statistics (nanoseconds)
  std::atomic<std::uint64_t> total_rx_time_ns{0};
  std::atomic<std::uint64_t> total_process_time_ns{0};
  std::atomic<std::uint64_t> total_tx_time_ns{0};

  void reset() {
    rx_packets.store(0);
    tx_packets.store(0);
    processed_packets.store(0);
    rx_bytes.store(0);
    tx_bytes.store(0);
    decrypt_errors.store(0);
    queue_full_drops.store(0);
    rx_queue_max_size.store(0);
    tx_queue_max_size.store(0);
    total_rx_time_ns.store(0);
    total_process_time_ns.store(0);
    total_tx_time_ns.store(0);
  }
};

/**
 * Packet data passed through the pipeline.
 */
struct PipelinePacket {
  // Raw packet data
  std::vector<std::uint8_t> data;

  // Source/destination info
  UdpEndpoint endpoint;

  // Session ID (for routing)
  std::uint64_t session_id{0};

  // Timestamp for latency tracking
  std::chrono::steady_clock::time_point timestamp;

  // Direction: true = outgoing (encrypt), false = incoming (decrypt)
  bool outgoing{false};
};

/**
 * Processed packet result from decryption/encryption.
 */
struct ProcessedPacket {
  // Decrypted frames (for incoming) or encrypted data (for outgoing)
  std::vector<std::vector<std::uint8_t>> packets;

  // Decoded frames (for incoming packets)
  std::vector<mux::MuxFrame> frames;

  // Original endpoint
  UdpEndpoint endpoint;

  // Session ID
  std::uint64_t session_id{0};

  // Direction
  bool outgoing{false};

  // Processing result
  bool success{true};
};

/**
 * Callback types for pipeline events.
 */
using RxCallback = std::function<void(std::uint64_t session_id,
                                       const std::vector<mux::MuxFrame>& frames,
                                       const UdpEndpoint& source)>;
using TxCompleteCallback = std::function<void(std::uint64_t session_id,
                                               std::size_t bytes_sent)>;
using ErrorCallback = std::function<void(std::uint64_t session_id,
                                         const std::string& error)>;

/**
 * Three-stage pipeline processor for high-throughput packet processing.
 *
 * Architecture (Issue #85 Phase 1):
 * ```
 * Thread 1 (RX):      UDP receive -> queue
 *        | (lock-free SPSC queue)
 * Thread 2 (Process): Decrypt/Encrypt -> queue
 *        | (lock-free SPSC queue)
 * Thread 3 (TX):      UDP send
 * ```
 *
 * This pipeline separates I/O from crypto processing, allowing:
 * - RX thread to saturate the UDP receive path
 * - Process thread to use CPU for crypto without blocking I/O
 * - TX thread to handle send completions independently
 *
 * Target throughput: 1-2 Gbps (vs ~500 Mbps single-threaded)
 *
 * Thread Safety:
 * - start(), stop() must be called from a single managing thread
 * - submit_rx(), submit_tx() are thread-safe (single producer each)
 * - Callbacks are invoked from the process/TX threads
 * - TransportSession access is protected by internal mutex (Issue #163)
 *
 * Session Synchronization (Issue #163):
 * The pipeline accesses the TransportSession from the process thread for both
 * encryption and decryption. Since TransportSession is NOT thread-safe, all
 * session method calls are protected by session_mutex_. This prevents concurrent
 * access to session state (sequence counters, replay window, retransmit buffer).
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 * @see Issue #85 for the multi-threading performance improvement initiative.
 * @see Issue #163 for the thread safety fix for TransportSession access.
 */
class PipelineProcessor {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  /**
   * Create a pipeline processor.
   *
   * @param session Pointer to the transport session for crypto operations.
   *                Must remain valid for the lifetime of the processor.
   * @param config Pipeline configuration
   */
  explicit PipelineProcessor(TransportSession* session,
                              PipelineConfig config = {});

  /**
   * Destructor. Stops all threads if running.
   */
  ~PipelineProcessor();

  // Non-copyable, non-movable
  PipelineProcessor(const PipelineProcessor&) = delete;
  PipelineProcessor& operator=(const PipelineProcessor&) = delete;
  PipelineProcessor(PipelineProcessor&&) = delete;
  PipelineProcessor& operator=(PipelineProcessor&&) = delete;

  /**
   * Start the pipeline threads.
   *
   * @param on_rx Callback for received and decrypted packets
   * @param on_tx_complete Callback for sent packet completion (optional)
   * @param on_error Callback for errors (optional)
   * @return true if started successfully
   */
  bool start(RxCallback on_rx,
             TxCompleteCallback on_tx_complete = {},
             ErrorCallback on_error = {});

  /**
   * Stop the pipeline threads.
   * Waits for all threads to complete.
   */
  void stop();

  /**
   * Check if the pipeline is running.
   */
  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

  /**
   * Submit a received packet for processing (decryption).
   * Called from the RX I/O thread.
   *
   * @param session_id The session ID
   * @param data The raw received packet data
   * @param source The source endpoint
   * @return true if queued, false if queue is full
   */
  bool submit_rx(std::uint64_t session_id,
                 std::span<const std::uint8_t> data,
                 const UdpEndpoint& source);

  /**
   * Submit data for transmission (encryption and send).
   * Called from the application thread.
   *
   * @param session_id The session ID
   * @param data The plaintext data to send
   * @param dest The destination endpoint
   * @param stream_id The stream ID for multiplexing
   * @return true if queued, false if queue is full
   */
  bool submit_tx(std::uint64_t session_id,
                 std::span<const std::uint8_t> data,
                 const UdpEndpoint& dest,
                 std::uint64_t stream_id = 0);

  /**
   * Get pipeline statistics.
   */
  [[nodiscard]] const PipelineStats& stats() const noexcept { return stats_; }

  /**
   * Reset statistics.
   */
  void reset_stats() { stats_.reset(); }

  /**
   * Set the UDP socket for sending packets.
   * Must be called before start() or when not running.
   *
   * @param socket Pointer to UDP socket. Must remain valid while pipeline is running.
   */
  void set_socket(UdpSocket* socket) { socket_ = socket; }

 private:
  // RX thread: receives packets and queues for processing
  void rx_thread_loop();

  // Process thread: encrypts/decrypts packets
  void process_thread_loop();

  // TX thread: sends encrypted packets
  void tx_thread_loop();

  // Helper to update max queue size statistics
  void update_queue_stats();

  // Configuration
  PipelineConfig config_;

  // Transport session for crypto operations
  TransportSession* session_;

  // UDP socket for sending
  UdpSocket* socket_{nullptr};

  // Mutex to protect session_ access from multiple threads.
  // THREAD-SAFETY (Issue #163): TransportSession is not thread-safe, so we must
  // serialize all accesses to it. The process_worker_ thread calls session methods
  // (encrypt_data, decrypt_packet) for both incoming and outgoing packets.
  // This mutex ensures that concurrent calls to these methods don't race.
  mutable std::mutex session_mutex_;

  // Lock-free queues for inter-thread communication
  std::unique_ptr<utils::SpscQueue<PipelinePacket>> rx_queue_;
  std::unique_ptr<utils::SpscQueue<ProcessedPacket>> tx_queue_;

  // Worker threads
  std::unique_ptr<utils::DedicatedWorker> process_worker_;
  std::unique_ptr<utils::DedicatedWorker> tx_worker_;

  // Running state
  std::atomic<bool> running_{false};

  // Callbacks
  RxCallback on_rx_;
  TxCompleteCallback on_tx_complete_;
  ErrorCallback on_error_;

  // Statistics
  PipelineStats stats_;
};

/**
 * Factory function to create a pipeline processor with default configuration.
 *
 * @param session The transport session
 * @return Unique pointer to the pipeline processor
 */
inline std::unique_ptr<PipelineProcessor> make_pipeline_processor(TransportSession* session) {
  return std::make_unique<PipelineProcessor>(session);
}

/**
 * Factory function to create a high-throughput pipeline processor.
 * Uses larger queues and batching for maximum throughput.
 *
 * @param session The transport session
 * @return Unique pointer to the pipeline processor
 */
inline std::unique_ptr<PipelineProcessor> make_high_throughput_pipeline(TransportSession* session) {
  PipelineConfig config;
  config.rx_queue_capacity = 16384;
  config.tx_queue_capacity = 16384;
  config.rx_batch_size = 128;
  config.tx_batch_size = 128;
  config.rx_batch_timeout_us = 50;
  config.tx_batch_timeout_us = 50;
  return std::make_unique<PipelineProcessor>(session, config);
}

}  // namespace veil::transport
