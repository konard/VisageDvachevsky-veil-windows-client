#pragma once

#ifdef _WIN32

#include <atomic>
#include <functional>

namespace veil::windows {

// Windows console control handler for graceful shutdown.
// Handles Ctrl+C, Ctrl+Break, and console close events.
class ConsoleHandler {
 public:
  using ControlCallback = std::function<void()>;

  // Get singleton instance.
  static ConsoleHandler& instance();

  // Setup the console control handler.
  // Returns true on success, false on failure.
  bool setup();

  // Remove the console control handler.
  void restore();

  // Check if a termination signal was received.
  bool should_terminate() const;

  // Set a callback to be invoked when a control signal is received.
  void on_control(ControlCallback callback);

  // Reset the termination flag (for testing).
  void reset();

 private:
  ConsoleHandler() = default;
  ~ConsoleHandler();

  // Non-copyable, non-movable.
  ConsoleHandler(const ConsoleHandler&) = delete;
  ConsoleHandler& operator=(const ConsoleHandler&) = delete;
  ConsoleHandler(ConsoleHandler&&) = delete;
  ConsoleHandler& operator=(ConsoleHandler&&) = delete;

  // Windows console control handler function.
  static int __stdcall handler_routine(unsigned long ctrl_type);

  static std::atomic<bool> terminate_flag_;
  static ControlCallback control_callback_;
  bool installed_{false};
};

}  // namespace veil::windows

#endif  // _WIN32
