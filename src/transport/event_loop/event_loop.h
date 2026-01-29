#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "common/utils/thread_checker.h"
#include "common/utils/timer_heap.h"
#include "transport/udp_socket/udp_socket.h"

namespace veil::transport {

// Forward declarations.
class TransportSession;

// Session identifier type.
using SessionId = std::uint64_t;

// Callback types for event loop events.
using PacketHandler = std::function<void(SessionId, std::span<const std::uint8_t>, const UdpEndpoint&)>;
using TimerHandler = std::function<void(SessionId)>;
using ErrorHandler = std::function<void(SessionId, std::error_code)>;

// Configuration for the event loop.
struct EventLoopConfig {
  // Poll timeout in milliseconds per iteration.
  // On Linux: used for epoll_wait timeout.
  // On Windows: used for select timeout.
  int epoll_timeout_ms{10};
  // Maximum events to process per poll iteration.
  // On Linux: max events from epoll_wait.
  // On Windows: not used directly (select processes all ready sockets).
  int max_events{64};
  // Default ACK send interval.
  // Issue #79: Reduced from 50ms to 20ms to decrease retransmit buffer pending count.
  std::chrono::milliseconds ack_interval{20};
  // Retransmit check interval.
  std::chrono::milliseconds retransmit_interval{100};
  // Idle timeout for session cleanup.
  std::chrono::seconds idle_timeout{300};
  // Statistics log interval (0 = disabled).
  std::chrono::seconds stats_log_interval{60};
};

// Socket registration info.
struct SocketInfo {
  UdpSocket* socket{nullptr};
  SessionId session_id{0};
  UdpEndpoint remote;
  PacketHandler on_packet;
  TimerHandler on_ack_timeout;
  TimerHandler on_retransmit;
  TimerHandler on_idle_timeout;
  ErrorHandler on_error;
  // Timer IDs for this socket.
  utils::TimerId ack_timer_id{utils::kInvalidTimerId};
  utils::TimerId retransmit_timer_id{utils::kInvalidTimerId};
  utils::TimerId idle_timer_id{utils::kInvalidTimerId};
  // Last activity timestamp.
  std::chrono::steady_clock::time_point last_activity;
  // Pending outgoing packets (for write-ready handling).
  std::vector<UdpPacket> pending_sends;
  bool writable{true};
};

/**
 * Event loop for managing UDP sockets and timers.
 * Handles I/O events, timeouts, and session management.
 *
 * Platform Support:
 *   - Linux: Uses epoll for efficient I/O multiplexing.
 *   - Windows: Uses select for I/O multiplexing.
 *
 * Thread Safety:
 *   This class is designed for single-threaded operation. All methods except
 *   stop() and is_running() must be called from the thread that calls run().
 *   The stop() method is safe to call from any thread (uses atomic flag).
 *
 *   - add_socket(), remove_socket(): Must be called from event loop thread
 *   - send_packet(): Must be called from event loop thread
 *   - schedule_timer(), cancel_timer(): Must be called from event loop thread
 *   - run(): Blocking; establishes the "event loop thread"
 *   - stop(): Thread-safe (can be called from any thread, e.g., signal handler)
 *   - is_running(): Thread-safe (atomic read)
 *
 * @see docs/thread_model.md for the VEIL threading model documentation.
 */
class EventLoop {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit EventLoop(EventLoopConfig config = {}, std::function<TimePoint()> now_fn = Clock::now);
  ~EventLoop();

  // Non-copyable, non-movable.
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  // Register a socket for I/O and timer events.
  // Returns true on success.
  bool add_socket(UdpSocket* socket, SessionId session_id, const UdpEndpoint& remote,
                  PacketHandler on_packet, TimerHandler on_ack_timeout = {},
                  TimerHandler on_retransmit = {}, TimerHandler on_idle_timeout = {},
                  ErrorHandler on_error = {});

  // Remove a socket from the event loop.
  bool remove_socket(int fd);

  // Queue packet for sending (handles EAGAIN/EWOULDBLOCK).
  bool send_packet(int fd, std::span<const std::uint8_t> data, const UdpEndpoint& remote);

  // Schedule a one-shot timer.
  utils::TimerId schedule_timer(std::chrono::steady_clock::duration after, utils::TimerCallback callback);

  // Cancel a timer.
  bool cancel_timer(utils::TimerId id);

  // Reset idle timeout for a session.
  void reset_idle_timeout(int fd);

  // Run the event loop (blocking).
  void run();

  // Stop the event loop (can be called from another thread).
  void stop();

  // Check if event loop is running.
  bool is_running() const { return running_.load(); }

  // Get the number of registered sockets.
  std::size_t socket_count() const { return sockets_.size(); }

 private:
  void handle_read(int fd);
  void handle_write(int fd);
  void handle_timers();
  void setup_session_timers(SocketInfo& info);
  void cleanup_session_timers(SocketInfo& info);

  EventLoopConfig config_;
  std::function<TimePoint()> now_fn_;
  // Platform-specific poll handle:
  // - Linux: epoll file descriptor
  // - Windows: dummy value (0 = initialized, -1 = not initialized)
  int epoll_fd_{-1};
  std::atomic<bool> running_{false};
  utils::TimerHeap timer_heap_;
  std::unordered_map<int, SocketInfo> sockets_;

  // Thread safety: verifies single-threaded access in debug builds.
  // Bound to the thread that calls run().
  VEIL_THREAD_CHECKER(thread_checker_);
};

}  // namespace veil::transport
