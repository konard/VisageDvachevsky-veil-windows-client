#ifdef _WIN32

#include "service_manager.h"

#include <sddl.h>
#include <shellapi.h>

#include <array>
#include <stdexcept>

#include "../common/logging/logger.h"

namespace veil::windows {

// ============================================================================
// ServiceManager Implementation
// ============================================================================

bool ServiceManager::install(const std::string& executable_path,
                             std::string& error) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
    error = "Failed to open Service Control Manager: " +
            std::to_string(GetLastError());
    return false;
  }

  // Create the service
  SC_HANDLE service = CreateServiceA(
      scm,
      kServiceName,
      kServiceDisplayName,
      SERVICE_ALL_ACCESS,
      SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START,  // Start automatically on boot
      SERVICE_ERROR_NORMAL,
      executable_path.c_str(),
      nullptr,   // No load ordering group
      nullptr,   // No tag identifier
      nullptr,   // No dependencies
      nullptr,   // Run as LocalSystem
      nullptr);  // No password

  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);

    if (err == ERROR_SERVICE_EXISTS) {
      error = "Service already exists";
      return false;
    }

    error = "Failed to create service: " + std::to_string(err);
    return false;
  }

  // Set the service description
  SERVICE_DESCRIPTIONA desc;
  desc.lpDescription = const_cast<char*>(kServiceDescription);
  ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &desc);

  // Configure delayed auto-start to reduce boot time impact
  // The service will start shortly after other auto-start services
  SERVICE_DELAYED_AUTO_START_INFO delayed_info = {};
  delayed_info.fDelayedAutostart = TRUE;
  ChangeServiceConfig2A(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                        &delayed_info);

  // Configure service recovery options (restart on failure)
  SC_ACTION actions[3] = {
      {SC_ACTION_RESTART, 5000},   // Restart after 5 seconds
      {SC_ACTION_RESTART, 10000},  // Restart after 10 seconds
      {SC_ACTION_RESTART, 30000}   // Restart after 30 seconds
  };

  SERVICE_FAILURE_ACTIONSA failure_actions = {};
  failure_actions.dwResetPeriod = 86400;  // Reset failure count after 1 day
  failure_actions.lpRebootMsg = nullptr;
  failure_actions.lpCommand = nullptr;
  failure_actions.cActions = 3;
  failure_actions.lpsaActions = actions;

  ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS,
                        &failure_actions);

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  LOG_INFO("Service '{}' installed successfully", kServiceName);
  return true;
}

bool ServiceManager::uninstall(std::string& error) {
  // First, stop the service if running
  if (is_running()) {
    if (!stop(error)) {
      LOG_WARN("Failed to stop service before uninstall: {}", error);
      // Continue with uninstall anyway
    }
  }

  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    error = "Failed to open Service Control Manager: " +
            std::to_string(GetLastError());
    return false;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName, DELETE);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);

    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      error = "Service does not exist";
      return false;
    }

    error = "Failed to open service: " + std::to_string(err);
    return false;
  }

  if (!DeleteService(service)) {
    error = "Failed to delete service: " + std::to_string(GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return false;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  LOG_INFO("Service '{}' uninstalled successfully", kServiceName);
  return true;
}

bool ServiceManager::start(std::string& error) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    error = "Failed to open Service Control Manager: " +
            std::to_string(GetLastError());
    return false;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_START);
  if (!service) {
    error = "Failed to open service: " + std::to_string(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  if (!StartServiceA(service, 0, nullptr)) {
    DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (err == ERROR_SERVICE_ALREADY_RUNNING) {
      error = "Service is already running";
      return false;
    }

    error = "Failed to start service: " + std::to_string(err);
    return false;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  LOG_INFO("Service '{}' started", kServiceName);
  return true;
}

bool ServiceManager::start_and_wait(std::string& error, DWORD timeout_ms) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    error = "Failed to open Service Control Manager: " +
            std::to_string(GetLastError());
    return false;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName,
                                   SERVICE_START | SERVICE_QUERY_STATUS);
  if (!service) {
    error = "Failed to open service: " + std::to_string(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  // Check if already running
  SERVICE_STATUS_PROCESS status_process;
  DWORD bytes_needed;
  if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                           reinterpret_cast<LPBYTE>(&status_process),
                           sizeof(status_process), &bytes_needed)) {
    if (status_process.dwCurrentState == SERVICE_RUNNING) {
      LOG_INFO("Service '{}' is already running", kServiceName);
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return true;
    }
  }

  // Start the service
  if (!StartServiceA(service, 0, nullptr)) {
    DWORD err = GetLastError();
    if (err != ERROR_SERVICE_ALREADY_RUNNING) {
      error = "Failed to start service: " + std::to_string(err);
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return false;
    }
    // If already running, that's fine - continue to wait
  }

  LOG_INFO("Service '{}' start initiated, waiting for it to become running...",
           kServiceName);

  // Wait for the service to reach SERVICE_RUNNING state
  DWORD start_tick = GetTickCount();
  DWORD wait_time = 250;  // Start with 250ms poll interval
  DWORD old_checkpoint = 0;

  while (true) {
    // Query current status
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status_process),
                              sizeof(status_process), &bytes_needed)) {
      error = "Failed to query service status: " +
              std::to_string(GetLastError());
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return false;
    }

    // Check if running
    if (status_process.dwCurrentState == SERVICE_RUNNING) {
      LOG_INFO("Service '{}' is now running (waited {}ms)", kServiceName,
               GetTickCount() - start_tick);
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return true;
    }

    // Check if failed to start
    if (status_process.dwCurrentState != SERVICE_START_PENDING) {
      error = "Service failed to start (state: " +
              std::to_string(status_process.dwCurrentState) + ")";
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return false;
    }

    // Check timeout
    if (GetTickCount() - start_tick > timeout_ms) {
      error = "Timeout waiting for service to start (timeout: " +
              std::to_string(timeout_ms) + "ms)";
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return false;
    }

    // Calculate wait time based on service hint
    if (status_process.dwWaitHint > 0) {
      wait_time = status_process.dwWaitHint / 10;
      // Clamp wait time between 100ms and 5000ms
      if (wait_time < 100) wait_time = 100;
      if (wait_time > 5000) wait_time = 5000;
    }

    // Check if checkpoint is progressing
    if (status_process.dwCheckPoint > old_checkpoint) {
      // Service is making progress, reset our timeout tracking
      old_checkpoint = status_process.dwCheckPoint;
    }

    LOG_DEBUG("Service '{}' starting (checkpoint: {}, waiting {}ms)...",
              kServiceName, status_process.dwCheckPoint, wait_time);
    Sleep(wait_time);
  }
}

bool ServiceManager::stop(std::string& error) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    error = "Failed to open Service Control Manager: " +
            std::to_string(GetLastError());
    return false;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_STOP);
  if (!service) {
    error = "Failed to open service: " + std::to_string(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  SERVICE_STATUS status;
  if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
    DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (err == ERROR_SERVICE_NOT_ACTIVE) {
      error = "Service is not running";
      return false;
    }

    error = "Failed to stop service: " + std::to_string(err);
    return false;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  LOG_INFO("Service '{}' stopped", kServiceName);
  return true;
}

bool ServiceManager::is_installed() {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    return false;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_QUERY_STATUS);
  bool installed = (service != nullptr);

  if (service) {
    CloseServiceHandle(service);
  }
  CloseServiceHandle(scm);

  return installed;
}

bool ServiceManager::is_running() {
  return get_status() == SERVICE_RUNNING;
}

DWORD ServiceManager::get_status() {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    return 0;
  }

  SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return 0;
  }

  SERVICE_STATUS status;
  DWORD state = 0;
  if (QueryServiceStatus(service, &status)) {
    state = status.dwCurrentState;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  return state;
}

std::string ServiceManager::get_status_string() {
  switch (get_status()) {
    case SERVICE_STOPPED:
      return "Stopped";
    case SERVICE_START_PENDING:
      return "Starting";
    case SERVICE_STOP_PENDING:
      return "Stopping";
    case SERVICE_RUNNING:
      return "Running";
    case SERVICE_CONTINUE_PENDING:
      return "Resuming";
    case SERVICE_PAUSE_PENDING:
      return "Pausing";
    case SERVICE_PAUSED:
      return "Paused";
    default:
      return "Unknown";
  }
}

// ============================================================================
// ServiceControlHandler Implementation
// ============================================================================

SERVICE_STATUS_HANDLE ServiceControlHandler::status_handle_ = nullptr;
SERVICE_STATUS ServiceControlHandler::service_status_ = {};
ServiceControlHandler::StopHandler ServiceControlHandler::stop_handler_;
ServiceControlHandler::PauseHandler ServiceControlHandler::pause_handler_;
ServiceControlHandler::ContinueHandler ServiceControlHandler::continue_handler_;

bool ServiceControlHandler::init(const std::string& service_name) {
  status_handle_ = RegisterServiceCtrlHandlerExA(
      service_name.c_str(), control_handler, nullptr);

  if (!status_handle_) {
    LOG_ERROR("Failed to register service control handler: {}",
              GetLastError());
    return false;
  }

  // Initialize service status
  service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status_.dwServiceSpecificExitCode = 0;
  service_status_.dwControlsAccepted = 0;
  service_status_.dwCurrentState = SERVICE_START_PENDING;
  service_status_.dwWin32ExitCode = NO_ERROR;
  service_status_.dwCheckPoint = 0;
  service_status_.dwWaitHint = 0;

  return true;
}

void ServiceControlHandler::set_status(DWORD state, DWORD exit_code,
                                       DWORD wait_hint) {
  static DWORD checkpoint = 1;

  service_status_.dwCurrentState = state;
  service_status_.dwWin32ExitCode = exit_code;
  service_status_.dwWaitHint = wait_hint;

  if (state == SERVICE_START_PENDING) {
    service_status_.dwControlsAccepted = 0;
  } else {
    service_status_.dwControlsAccepted =
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  }

  if (state == SERVICE_RUNNING || state == SERVICE_STOPPED) {
    service_status_.dwCheckPoint = 0;
  } else {
    service_status_.dwCheckPoint = checkpoint++;
  }

  SetServiceStatus(status_handle_, &service_status_);
}

void ServiceControlHandler::report_starting(DWORD checkpoint,
                                            DWORD wait_hint) {
  service_status_.dwCurrentState = SERVICE_START_PENDING;
  service_status_.dwControlsAccepted = 0;
  service_status_.dwCheckPoint = checkpoint;
  service_status_.dwWaitHint = wait_hint;
  SetServiceStatus(status_handle_, &service_status_);
}

void ServiceControlHandler::report_running() {
  service_status_.dwCurrentState = SERVICE_RUNNING;
  service_status_.dwControlsAccepted =
      SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  service_status_.dwCheckPoint = 0;
  service_status_.dwWaitHint = 0;
  SetServiceStatus(status_handle_, &service_status_);
  LOG_INFO("Service is now running");
}

void ServiceControlHandler::report_stopping(DWORD checkpoint,
                                            DWORD wait_hint) {
  service_status_.dwCurrentState = SERVICE_STOP_PENDING;
  service_status_.dwControlsAccepted = 0;
  service_status_.dwCheckPoint = checkpoint;
  service_status_.dwWaitHint = wait_hint;
  SetServiceStatus(status_handle_, &service_status_);
}

void ServiceControlHandler::report_stopped(DWORD exit_code) {
  service_status_.dwCurrentState = SERVICE_STOPPED;
  service_status_.dwControlsAccepted = 0;
  service_status_.dwWin32ExitCode = exit_code;
  service_status_.dwCheckPoint = 0;
  service_status_.dwWaitHint = 0;
  SetServiceStatus(status_handle_, &service_status_);
  LOG_INFO("Service stopped with exit code {}", exit_code);
}

void ServiceControlHandler::on_stop(StopHandler handler) {
  stop_handler_ = std::move(handler);
}

void ServiceControlHandler::on_pause(PauseHandler handler) {
  pause_handler_ = std::move(handler);
}

void ServiceControlHandler::on_continue(ContinueHandler handler) {
  continue_handler_ = std::move(handler);
}

DWORD WINAPI ServiceControlHandler::control_handler(DWORD control,
                                                    DWORD /*event_type*/,
                                                    LPVOID /*event_data*/,
                                                    LPVOID /*context*/) {
  switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      LOG_INFO("Service control: STOP/SHUTDOWN");
      if (stop_handler_) {
        report_stopping();
        stop_handler_();
      }
      return NO_ERROR;

    case SERVICE_CONTROL_PAUSE:
      LOG_INFO("Service control: PAUSE");
      if (pause_handler_) {
        pause_handler_();
      }
      return NO_ERROR;

    case SERVICE_CONTROL_CONTINUE:
      LOG_INFO("Service control: CONTINUE");
      if (continue_handler_) {
        continue_handler_();
      }
      return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
      // Return current status
      SetServiceStatus(status_handle_, &service_status_);
      return NO_ERROR;

    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

// ============================================================================
// Elevation Helper Implementation
// ============================================================================

namespace elevation {

bool is_elevated() {
  BOOL elevated = FALSE;
  HANDLE token = nullptr;

  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(TOKEN_ELEVATION);

    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation),
                            &size)) {
      elevated = elevation.TokenIsElevated;
    }

    CloseHandle(token);
  }

  return elevated != FALSE;
}

bool request_elevation(const std::string& arguments) {
  std::array<char, MAX_PATH> path{};
  GetModuleFileNameA(nullptr, path.data(), MAX_PATH);

  SHELLEXECUTEINFOA sei = {};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = "runas";
  sei.lpFile = path.data();
  sei.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  sei.nShow = SW_NORMAL;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  if (!ShellExecuteExA(&sei)) {
    DWORD err = GetLastError();
    if (err == ERROR_CANCELLED) {
      LOG_INFO("User declined elevation request");
    } else {
      LOG_ERROR("Failed to request elevation: {}", err);
    }
    return false;
  }

  // Wait for the elevated process to complete
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, INFINITE);
    CloseHandle(sei.hProcess);
  }

  return true;
}

bool run_elevated(const std::string& executable, const std::string& arguments,
                  bool wait) {
  SHELLEXECUTEINFOA sei = {};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = "runas";
  sei.lpFile = executable.c_str();
  sei.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  sei.nShow = SW_HIDE;
  sei.fMask = wait ? SEE_MASK_NOCLOSEPROCESS : 0;

  if (!ShellExecuteExA(&sei)) {
    LOG_ERROR("Failed to run elevated command: {}", GetLastError());
    return false;
  }

  if (wait && sei.hProcess) {
    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);

    return exit_code == 0;
  }

  return true;
}

}  // namespace elevation

}  // namespace veil::windows

#endif  // _WIN32
