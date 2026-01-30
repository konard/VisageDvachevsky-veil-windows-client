#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace veil::windows {

/// Information about an installed Windows application
struct InstalledApp {
  std::string name;           // Display name of the application
  std::string executable;     // Full path to executable (if available)
  std::string publisher;      // Publisher/vendor name
  std::string version;        // Version string
  std::string installLocation; // Installation directory
  std::string uninstallKey;   // Registry key identifier (for uniqueness)
  bool isSystemApp{false};    // Whether this is a Windows system app
  bool isUWPApp{false};       // Whether this is a UWP/Store app
};

/// Utility class for enumerating installed Windows applications
class AppEnumerator {
 public:
  /// Get list of all installed applications
  /// @return Vector of installed applications
  static std::vector<InstalledApp> GetInstalledApplications();

  /// Get list of currently running processes with executable paths
  /// @return Vector of running applications
  static std::vector<InstalledApp> GetRunningProcesses();

  /// Validate if a path points to a valid executable
  /// @param path Path to check
  /// @return true if the path is a valid executable
  static bool IsValidExecutable(const std::string& path);

  /// Extract icon from executable (returns path to temp icon file)
  /// @param exePath Path to executable
  /// @return Optional path to extracted icon file
  static std::optional<std::string> ExtractIcon(const std::string& exePath);

 private:
  /// Enumerate applications from a specific registry hive
  static std::vector<InstalledApp> EnumerateFromRegistry(void* hKey, const std::string& subKey);

  /// Enumerate UWP/Store applications
  static std::vector<InstalledApp> EnumerateUWPApps();

  /// Get executable path for a UWP app
  static std::optional<std::string> GetUWPAppExecutable(const std::string& packageName);

  /// Check if an app should be filtered out (e.g., updates, system components)
  static bool ShouldFilterApp(const InstalledApp& app);

  /// Normalize executable path (resolve environment variables, etc.)
  static std::string NormalizeExecutablePath(const std::string& path);
};

}  // namespace veil::windows
