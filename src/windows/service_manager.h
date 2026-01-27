#pragma once

#ifdef _WIN32

#include <windows.h>

#include <functional>
#include <string>

namespace veil::windows {

// ============================================================================
// Windows Service Manager
// ============================================================================
// Provides functionality to install, uninstall, start, and stop the VEIL VPN
// Windows service. The service runs as SYSTEM and manages the VPN connection.

class ServiceManager {
 public:
  // Service name and display name
  static constexpr const char* kServiceName = "VeilVPN";
  static constexpr const char* kServiceDisplayName = "VEIL VPN Service";
  static constexpr const char* kServiceDescription =
      "Provides secure VPN connectivity through the VEIL protocol";

  // Install the service
  // executable_path: Full path to veil-service.exe
  static bool install(const std::string& executable_path, std::string& error);

  // Uninstall the service
  static bool uninstall(std::string& error);

  // Start the service (returns immediately after initiating start)
  static bool start(std::string& error);

  // Start the service and wait for it to reach SERVICE_RUNNING state
  // timeout_ms: Maximum time to wait for the service to start (default: 30 seconds)
  // Returns true if service reaches SERVICE_RUNNING within the timeout
  static bool start_and_wait(std::string& error, DWORD timeout_ms = 30000);

  // Stop the service
  static bool stop(std::string& error);

  // Query service status
  static bool is_installed();
  static bool is_running();

  // Get the current status
  static DWORD get_status();

  // Get status as string
  static std::string get_status_string();
};

// ============================================================================
// Service Control Handler
// ============================================================================
// Used by the service executable to handle service control requests.

class ServiceControlHandler {
 public:
  using StopHandler = std::function<void()>;
  using PauseHandler = std::function<void()>;
  using ContinueHandler = std::function<void()>;

  // Initialize the service control handler
  static bool init(const std::string& service_name);

  // Set the current service status
  static void set_status(DWORD state, DWORD exit_code = NO_ERROR,
                         DWORD wait_hint = 0);

  // Report that the service is starting (with progress)
  static void report_starting(DWORD checkpoint, DWORD wait_hint = 3000);

  // Report that the service is running
  static void report_running();

  // Report that the service is stopping
  static void report_stopping(DWORD checkpoint = 0, DWORD wait_hint = 3000);

  // Report that the service has stopped
  static void report_stopped(DWORD exit_code = NO_ERROR);

  // Set handlers for service control events
  static void on_stop(StopHandler handler);
  static void on_pause(PauseHandler handler);
  static void on_continue(ContinueHandler handler);

  // Service control handler callback (called by Windows)
  static DWORD WINAPI control_handler(DWORD control, DWORD event_type,
                                      LPVOID event_data, LPVOID context);

 private:
  static SERVICE_STATUS_HANDLE status_handle_;
  static SERVICE_STATUS service_status_;
  static StopHandler stop_handler_;
  static PauseHandler pause_handler_;
  static ContinueHandler continue_handler_;
};

// ============================================================================
// Elevation Helper
// ============================================================================
// Helper functions for checking and requesting administrator privileges.

namespace elevation {

// Check if the current process is running with administrator privileges
bool is_elevated();

// Restart the current process with administrator privileges
// Returns false if the user declined the UAC prompt or an error occurred
bool request_elevation(const std::string& arguments = "");

// Run a command with administrator privileges
bool run_elevated(const std::string& executable, const std::string& arguments,
                  bool wait = true);

}  // namespace elevation

}  // namespace veil::windows

#endif  // _WIN32
