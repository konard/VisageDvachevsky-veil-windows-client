#ifdef _WIN32

#include "windows/console_handler.h"

#include <windows.h>

#include "common/logging/logger.h"

namespace veil::windows {

// Static member initialization.
std::atomic<bool> ConsoleHandler::terminate_flag_{false};
ConsoleHandler::ControlCallback ConsoleHandler::control_callback_;

ConsoleHandler& ConsoleHandler::instance() {
  static ConsoleHandler instance;
  return instance;
}

ConsoleHandler::~ConsoleHandler() { restore(); }

bool ConsoleHandler::setup() {
  if (installed_) {
    return true;
  }

  if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(handler_routine), TRUE)) {
    LOG_ERROR("Failed to install console control handler: {}", GetLastError());
    return false;
  }

  installed_ = true;
  LOG_DEBUG("Console control handler installed");
  return true;
}

void ConsoleHandler::restore() {
  if (!installed_) {
    return;
  }

  SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(handler_routine), FALSE);
  installed_ = false;
  LOG_DEBUG("Console control handler removed");
}

bool ConsoleHandler::should_terminate() const { return terminate_flag_.load(); }

void ConsoleHandler::on_control(ControlCallback callback) {
  control_callback_ = std::move(callback);
}

void ConsoleHandler::reset() { terminate_flag_.store(false); }

int __stdcall ConsoleHandler::handler_routine(unsigned long ctrl_type) {
  switch (ctrl_type) {
    case CTRL_C_EVENT:
      LOG_INFO("Received Ctrl+C signal");
      terminate_flag_.store(true);
      if (control_callback_) {
        control_callback_();
      }
      return TRUE;

    case CTRL_BREAK_EVENT:
      LOG_INFO("Received Ctrl+Break signal");
      terminate_flag_.store(true);
      if (control_callback_) {
        control_callback_();
      }
      return TRUE;

    case CTRL_CLOSE_EVENT:
      LOG_INFO("Received console close signal");
      terminate_flag_.store(true);
      if (control_callback_) {
        control_callback_();
      }
      // Give time for cleanup before the system forcibly terminates.
      Sleep(10000);
      return TRUE;

    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      LOG_INFO("Received system shutdown signal");
      terminate_flag_.store(true);
      if (control_callback_) {
        control_callback_();
      }
      return TRUE;

    default:
      return FALSE;
  }
}

}  // namespace veil::windows

#endif  // _WIN32
