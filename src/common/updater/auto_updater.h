#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace veil::updater {

// ============================================================================
// Version Information
// ============================================================================

struct Version {
  int major{0};
  int minor{0};
  int patch{0};
  std::string prerelease;  // e.g., "beta.1", "rc.2"

  // Parse version string (e.g., "1.2.3" or "1.2.3-beta.1")
  static std::optional<Version> parse(const std::string& version_string);

  // Convert to string
  std::string to_string() const;

  // Comparison operators
  bool operator<(const Version& other) const;
  bool operator>(const Version& other) const;
  bool operator==(const Version& other) const;
  bool operator<=(const Version& other) const;
  bool operator>=(const Version& other) const;
  bool operator!=(const Version& other) const;
};

// ============================================================================
// Release Information
// ============================================================================

struct ReleaseAsset {
  std::string name;
  std::string download_url;
  std::string content_type;
  std::size_t size{0};
  std::string sha256_checksum;  // Optional SHA256 checksum for verification
};

struct ReleaseInfo {
  Version version;
  std::string tag_name;
  std::string name;
  std::string body;  // Release notes (Markdown)
  std::string published_at;
  std::string html_url;
  bool prerelease{false};
  bool draft{false};
  std::vector<ReleaseAsset> assets;

  // Find the installer asset for the current platform
  std::optional<ReleaseAsset> find_installer() const;
};

// ============================================================================
// Update Configuration
// ============================================================================

struct UpdateConfig {
  // GitHub repository information
  std::string github_owner{"VisageDvachevsky"};
  std::string github_repo{"veil-core"};

  // Update check settings
  bool check_on_startup{true};
  bool check_for_prereleases{false};
  int check_interval_hours{24};

  // Download settings
  std::string download_directory;  // Empty = temp directory
  bool auto_download{false};
  bool auto_install{false};

  // Custom update server (optional, overrides GitHub)
  std::string custom_update_url;
};

// ============================================================================
// Auto Updater
// ============================================================================

class AutoUpdater {
 public:
  // Callback types
  using CheckCallback = std::function<void(bool available, const ReleaseInfo& release)>;
  using DownloadProgressCallback = std::function<void(std::size_t downloaded, std::size_t total)>;
  using DownloadCompleteCallback = std::function<void(bool success, const std::string& path_or_error)>;
  using ErrorCallback = std::function<void(const std::string& error)>;
  using ShutdownCallback = std::function<void()>;  // Called before application exit for cleanup

  explicit AutoUpdater(UpdateConfig config = {});
  ~AutoUpdater();

  // Non-copyable
  AutoUpdater(const AutoUpdater&) = delete;
  AutoUpdater& operator=(const AutoUpdater&) = delete;

  // Get current version
  static Version current_version();

  // Check for updates (async)
  void check_for_updates(const CheckCallback& callback);

  // Check for updates (blocking)
  std::optional<ReleaseInfo> check_for_updates_sync();

  // Download update (async)
  void download_update(const ReleaseInfo& release,
                       const DownloadProgressCallback& progress_callback,
                       const DownloadCompleteCallback& complete_callback);

  // Install update
  // This will launch the installer and exit the current application
  bool install_update(const std::string& installer_path, std::string& error);

  // Get the latest cached release info
  std::optional<ReleaseInfo> get_cached_release() const;

  // Set error callback
  void on_error(ErrorCallback callback);

  // Set shutdown callback (called before application exit during update installation)
  void on_shutdown(ShutdownCallback callback);

  // Get/set configuration
  const UpdateConfig& config() const { return config_; }
  void set_config(UpdateConfig config) { config_ = std::move(config); }

  // Get last check time
  std::string get_last_check_time() const;

  // Ignore a specific version
  void ignore_version(const Version& version);
  bool is_version_ignored(const Version& version) const;

 private:
  // Implementation details
  struct Impl;
  std::unique_ptr<Impl> impl_;

  UpdateConfig config_;
  ErrorCallback error_callback_;
  ShutdownCallback shutdown_callback_;
  std::optional<ReleaseInfo> cached_release_;
  std::vector<Version> ignored_versions_;
};

// ============================================================================
// Update Dialog Helper
// ============================================================================
// Platform-independent update dialog for showing update availability.

struct UpdateDialogResult {
  enum class Action {
    kSkip,        // Skip this version
    kRemindLater, // Remind later
    kDownload,    // Download update
    kInstall      // Install now (if already downloaded)
  };

  Action action{Action::kRemindLater};
  bool dont_remind_again{false};
};

// Show update dialog (Qt implementation in GUI module)
// Returns nullopt if dialog was cancelled
std::optional<UpdateDialogResult> show_update_dialog(
    const ReleaseInfo& release,
    const Version& current_version,
    bool already_downloaded = false);

}  // namespace veil::updater
