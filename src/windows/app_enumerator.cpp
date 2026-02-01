#include "app_enumerator.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <unordered_set>
#include <filesystem>
#include <cctype>
#endif

namespace veil::windows {

#ifdef _WIN32

namespace {

/// Helper to read registry string value
std::string ReadRegistryString(HKEY hKey, const std::string& valueName) {
  char buffer[1024] = {0};
  DWORD bufferSize = sizeof(buffer);
  DWORD type = REG_SZ;

  LONG result = RegQueryValueExA(hKey, valueName.c_str(), nullptr, &type,
                                  reinterpret_cast<LPBYTE>(buffer), &bufferSize);

  if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
    if (type == REG_EXPAND_SZ) {
      char expanded[1024] = {0};
      ExpandEnvironmentStringsA(buffer, expanded, sizeof(expanded));
      return std::string(expanded);
    }
    return std::string(buffer);
  }

  return "";
}

/// Helper to check if a string contains another (case-insensitive)
bool ContainsIgnoreCase(const std::string& str, const std::string& substr) {
  auto it = std::search(
    str.begin(), str.end(),
    substr.begin(), substr.end(),
    [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
  );
  return it != str.end();
}

}  // anonymous namespace

std::vector<InstalledApp> AppEnumerator::GetInstalledApplications() {
  std::vector<InstalledApp> apps;
  std::unordered_set<std::string> seenKeys;  // Avoid duplicates

  // Enumerate from HKEY_LOCAL_MACHINE
  auto localMachineApps = EnumerateFromRegistry(
    HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
  );
  for (auto& app : localMachineApps) {
    if (seenKeys.find(app.uninstallKey) == seenKeys.end()) {
      seenKeys.insert(app.uninstallKey);
      apps.push_back(std::move(app));
    }
  }

  // Enumerate from HKEY_CURRENT_USER
  auto currentUserApps = EnumerateFromRegistry(
    HKEY_CURRENT_USER,
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
  );
  for (auto& app : currentUserApps) {
    if (seenKeys.find(app.uninstallKey) == seenKeys.end()) {
      seenKeys.insert(app.uninstallKey);
      apps.push_back(std::move(app));
    }
  }

  // Enumerate from HKEY_LOCAL_MACHINE Wow6432Node (32-bit apps on 64-bit Windows)
  auto wow64Apps = EnumerateFromRegistry(
    HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
  );
  for (auto& app : wow64Apps) {
    if (seenKeys.find(app.uninstallKey) == seenKeys.end()) {
      seenKeys.insert(app.uninstallKey);
      apps.push_back(std::move(app));
    }
  }

  // TODO: Enumerate UWP apps (requires Windows Runtime APIs)
  // This would need additional implementation with WinRT

  // Filter out unwanted entries
  apps.erase(
    std::remove_if(apps.begin(), apps.end(),
                   [](const InstalledApp& app) { return ShouldFilterApp(app); }),
    apps.end()
  );

  // Sort by name
  std::sort(apps.begin(), apps.end(),
            [](const InstalledApp& a, const InstalledApp& b) {
              return a.name < b.name;
            });

  return apps;
}

std::vector<InstalledApp> AppEnumerator::GetRunningProcesses() {
  std::vector<InstalledApp> processes;
  std::unordered_set<std::string> seenExecutables;

  // Create snapshot of all processes
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }

  PROCESSENTRY32 entry;
  entry.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(snapshot, &entry)) {
    do {
      // Open process to get executable path
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (process) {
        char exePath[MAX_PATH] = {0};
        DWORD size = MAX_PATH;

        if (QueryFullProcessImageNameA(process, 0, exePath, &size)) {
          std::string exe(exePath);

          // Avoid duplicates
          if (seenExecutables.find(exe) == seenExecutables.end()) {
            seenExecutables.insert(exe);

            InstalledApp app;
            app.name = entry.szExeFile;
            app.executable = exe;
            app.isSystemApp = ContainsIgnoreCase(exe, "\\Windows\\") ||
                              ContainsIgnoreCase(exe, "\\System32\\");

            // Try to extract more info from file
            std::filesystem::path p(exe);
            if (p.has_parent_path()) {
              app.installLocation = p.parent_path().string();
            }

            processes.push_back(std::move(app));
          }
        }

        CloseHandle(process);
      }
    } while (Process32Next(snapshot, &entry));
  }

  CloseHandle(snapshot);

  // Sort by name
  std::sort(processes.begin(), processes.end(),
            [](const InstalledApp& a, const InstalledApp& b) {
              return a.name < b.name;
            });

  return processes;
}

bool AppEnumerator::IsValidExecutable(const std::string& path) {
  if (path.empty()) return false;

  try {
    std::filesystem::path p(path);
    std::error_code ec;

    // Check if file exists (use error_code overload to avoid throwing on
    // unavailable drives, network paths, etc.)
    if (!std::filesystem::exists(p, ec) || ec) return false;

    // Check if it's a file (not a directory)
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;

    // Check extension
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
  } catch (...) {
    // Guard against any unexpected filesystem exceptions (e.g., device not ready)
    return false;
  }
}

std::optional<std::string> AppEnumerator::ExtractIcon(const std::string& exePath) {
  // This is a placeholder - full implementation would:
  // 1. Use ExtractIconEx to get the icon from the executable
  // 2. Save it to a temporary file
  // 3. Return the path to that file
  // For now, we return empty optional
  return std::nullopt;
}

std::vector<InstalledApp> AppEnumerator::EnumerateFromRegistry(void* hKeyVoid, const std::string& subKey) {
  std::vector<InstalledApp> apps;
  HKEY hKey = reinterpret_cast<HKEY>(hKeyVoid);
  HKEY hUninstallKey = nullptr;

  LONG result = RegOpenKeyExA(hKey, subKey.c_str(), 0, KEY_READ, &hUninstallKey);
  if (result != ERROR_SUCCESS) {
    return apps;
  }

  DWORD index = 0;
  char keyName[256];
  DWORD keyNameSize = sizeof(keyName);

  while (RegEnumKeyExA(hUninstallKey, index++, keyName, &keyNameSize,
                       nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
    HKEY hAppKey = nullptr;
    std::string appKeyPath = subKey + "\\" + keyName;

    if (RegOpenKeyExA(hKey, appKeyPath.c_str(), 0, KEY_READ, &hAppKey) == ERROR_SUCCESS) {
      InstalledApp app;

      app.name = ReadRegistryString(hAppKey, "DisplayName");
      app.publisher = ReadRegistryString(hAppKey, "Publisher");
      app.version = ReadRegistryString(hAppKey, "DisplayVersion");
      app.installLocation = ReadRegistryString(hAppKey, "InstallLocation");
      app.uninstallKey = keyName;

      // Try to find executable
      std::string displayIcon = ReadRegistryString(hAppKey, "DisplayIcon");
      if (!displayIcon.empty()) {
        // DisplayIcon often has format: "C:\Path\app.exe,0" or just "C:\Path\app.exe"
        size_t commaPos = displayIcon.find(',');
        if (commaPos != std::string::npos) {
          displayIcon = displayIcon.substr(0, commaPos);
        }
        // Remove quotes
        displayIcon.erase(std::remove(displayIcon.begin(), displayIcon.end(), '\"'), displayIcon.end());

        if (IsValidExecutable(displayIcon)) {
          app.executable = NormalizeExecutablePath(displayIcon);
        }
      }

      // If no executable found from DisplayIcon, try InstallLocation
      if (app.executable.empty() && !app.installLocation.empty()) {
        try {
          std::filesystem::path installPath(app.installLocation);
          std::error_code ec;
          // Use error_code overloads to avoid throwing on unavailable drives
          if (std::filesystem::exists(installPath, ec) && !ec &&
              std::filesystem::is_directory(installPath, ec) && !ec) {
            for (const auto& entry : std::filesystem::directory_iterator(installPath, ec)) {
              if (ec) break;
              std::error_code ec2;
              if (entry.is_regular_file(ec2) && !ec2 &&
                  entry.path().extension() == ".exe") {
                app.executable = entry.path().string();
                break;
              }
            }
          }
        } catch (...) {
          // Skip this install location if filesystem access fails
        }
      }

      // Check if it's a system app
      app.isSystemApp = ContainsIgnoreCase(app.publisher, "Microsoft Corporation") &&
                        (ContainsIgnoreCase(app.name, "Windows") ||
                         ContainsIgnoreCase(app.name, "Update") ||
                         ContainsIgnoreCase(app.name, "Security"));

      if (!app.name.empty()) {
        apps.push_back(std::move(app));
      }

      RegCloseKey(hAppKey);
    }

    keyNameSize = sizeof(keyName);
  }

  RegCloseKey(hUninstallKey);
  return apps;
}

std::vector<InstalledApp> AppEnumerator::EnumerateUWPApps() {
  // TODO: Implement UWP app enumeration
  // This requires Windows Runtime APIs (WinRT) which need additional setup
  // For Phase 1, we'll focus on traditional Win32 applications
  return {};
}

std::optional<std::string> AppEnumerator::GetUWPAppExecutable(const std::string& packageName) {
  // TODO: Implement UWP app executable resolution
  return std::nullopt;
}

bool AppEnumerator::ShouldFilterApp(const InstalledApp& app) {
  // Filter out entries with no name
  if (app.name.empty()) return true;

  // Filter out Windows updates and components
  if (ContainsIgnoreCase(app.name, "KB") && app.name.length() < 15) return true;
  if (ContainsIgnoreCase(app.name, "Update for")) return true;
  if (ContainsIgnoreCase(app.name, "Hotfix for")) return true;
  if (ContainsIgnoreCase(app.name, "Security Update")) return true;

  // Filter out apps with no executable and no install location
  if (app.executable.empty() && app.installLocation.empty()) return true;

  return false;
}

std::string AppEnumerator::NormalizeExecutablePath(const std::string& path) {
  if (path.empty()) return path;

  try {
    std::filesystem::path p(path);
    return std::filesystem::canonical(p).string();
  } catch (...) {
    // If canonical fails, return original path
    return path;
  }
}

#else  // Non-Windows platforms

std::vector<InstalledApp> AppEnumerator::GetInstalledApplications() {
  return {};
}

std::vector<InstalledApp> AppEnumerator::GetRunningProcesses() {
  return {};
}

bool AppEnumerator::IsValidExecutable(const std::string& path) {
  return false;
}

std::optional<std::string> AppEnumerator::ExtractIcon(const std::string& exePath) {
  return std::nullopt;
}

std::vector<InstalledApp> AppEnumerator::EnumerateFromRegistry(void* hKey, const std::string& subKey) {
  return {};
}

std::vector<InstalledApp> AppEnumerator::EnumerateUWPApps() {
  return {};
}

std::optional<std::string> AppEnumerator::GetUWPAppExecutable(const std::string& packageName) {
  return std::nullopt;
}

bool AppEnumerator::ShouldFilterApp(const InstalledApp& app) {
  return true;
}

std::string AppEnumerator::NormalizeExecutablePath(const std::string& path) {
  return path;
}

#endif  // _WIN32

}  // namespace veil::windows
