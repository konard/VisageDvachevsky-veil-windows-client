// Unified launcher for VEIL VPN
// Requests elevation once, ensures the service is installed and running,
// waits for IPC readiness, then launches the GUI client.

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "service_manager.h"

namespace {

// Get directory containing the current executable.
std::string get_app_directory() {
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string s(path);
  auto pos = s.find_last_of('\\');
  return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// Wait for a named pipe to become available.
bool wait_for_named_pipe(const char* pipe_name, int timeout_ms) {
  DWORD start = GetTickCount();
  while (GetTickCount() - start < static_cast<DWORD>(timeout_ms)) {
    if (WaitNamedPipeA(pipe_name, 0)) {
      return true;
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND) {
      return true;  // Pipe exists but may be busy
    }
    Sleep(100);
  }
  return false;
}

}  // namespace

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
  // 1. Request elevation if not running as administrator.
  if (!veil::windows::elevation::is_elevated()) {
    return veil::windows::elevation::request_elevation("") ? 0 : 1;
  }

  std::string app_dir = get_app_directory();
  std::string service_path = app_dir + "\\veil-service.exe";
  std::string gui_path = app_dir + "\\veil-client-gui.exe";

  // 2. Install the service if it is not yet installed.
  if (!veil::windows::ServiceManager::is_installed()) {
    std::string error;
    if (!veil::windows::ServiceManager::install(service_path, error)) {
      MessageBoxA(nullptr,
                  ("Failed to install VEIL service: " + error).c_str(),
                  "VEIL VPN - Error", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  // 3. Start the service if it is not running.
  if (!veil::windows::ServiceManager::is_running()) {
    std::string error;
    if (!veil::windows::ServiceManager::start_and_wait(error, 10000)) {
      MessageBoxA(nullptr,
                  ("Failed to start VEIL service: " + error).c_str(),
                  "VEIL VPN - Error", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  // 4. Wait for the IPC named pipe to be ready.
  if (!wait_for_named_pipe("\\\\.\\pipe\\veil-client", 5000)) {
    MessageBoxA(nullptr,
                "Service started but IPC not ready. Please try again.",
                "VEIL VPN - Warning", MB_OK | MB_ICONWARNING);
    // Continue anyway — the GUI will retry the connection.
  }

  // 5. Launch the GUI client (without elevation — drop to user context).
  SHELLEXECUTEINFOA sei = {};
  sei.cbSize = sizeof(sei);
  sei.lpFile = gui_path.c_str();
  sei.nShow = SW_SHOW;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  if (!ShellExecuteExA(&sei)) {
    MessageBoxA(nullptr,
                "Failed to launch VEIL VPN GUI.",
                "VEIL VPN - Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  // Close the process handle — the launcher's job is done.
  if (sei.hProcess != nullptr) {
    CloseHandle(sei.hProcess);
  }

  return 0;
}

#endif  // _WIN32
