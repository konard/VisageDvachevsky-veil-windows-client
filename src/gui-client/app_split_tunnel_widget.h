#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <memory>
#include <vector>
#include <string>

#ifdef _WIN32
#include "../windows/app_enumerator.h"
#endif

namespace veil::gui {

/// List item representing an installed or running application
class AppListItem : public QWidget {
  Q_OBJECT

 public:
  explicit AppListItem(std::string appName,
                       std::string exePath,
                       bool isSystemApp,
                       QWidget* parent = nullptr);

  /// Get application executable path
  std::string getExecutablePath() const { return exePath_; }

  /// Get application name
  std::string getAppName() const { return appName_; }

  /// Check if this is a system app
  bool isSystemApp() const { return isSystemApp_; }

 signals:
  void addToVpnRequested(const std::string& exePath);
  void addToBypassRequested(const std::string& exePath);

 private:
  void setupUi();

  std::string appName_;
  std::string exePath_;
  bool isSystemApp_;

  QLabel* nameLabel_;
  QLabel* pathLabel_;
  QLabel* badgeLabel_;
  QPushButton* addToVpnButton_;
  QPushButton* addToBypassButton_;
};

/// Widget for managing per-application split tunneling
class AppSplitTunnelWidget : public QWidget {
  Q_OBJECT

 public:
  explicit AppSplitTunnelWidget(QWidget* parent = nullptr);

  /// Load app lists from settings
  void loadFromSettings();

  /// Save app lists to settings
  void saveToSettings();

  /// Get list of apps that should always use VPN
  std::vector<std::string> getVpnApps() const;

  /// Get list of apps that should bypass VPN
  std::vector<std::string> getBypassApps() const;

  /// Set list of apps that should always use VPN
  void setVpnApps(const std::vector<std::string>& apps);

  /// Set list of apps that should bypass VPN
  void setBypassApps(const std::vector<std::string>& apps);

 signals:
  void settingsChanged();

 private slots:
  void onRefreshInstalledApps();
  void onRefreshRunningApps();
  void onSearchTextChanged(const QString& text);
  void onAppListTypeChanged(int index);
  void onAddToVpnList(const std::string& exePath);
  void onAddToBypassList(const std::string& exePath);
  void onRemoveFromVpnList();
  void onRemoveFromBypassList();
  void onAddCustomPath();
  void onShowSystemAppsToggled(bool checked);

 private:
  void setupUi();
  void populateInstalledApps();
  void populateRunningApps();
  void populateAppList(const std::vector<std::string>& apps, QListWidget* listWidget);
  void applySearchFilter();
  bool matchesSearch(const std::string& appName, const std::string& exePath) const;

#ifdef _WIN32
  std::vector<veil::windows::InstalledApp> installedApps_;
  std::vector<veil::windows::InstalledApp> runningApps_;
#endif

  std::vector<std::string> vpnApps_;
  std::vector<std::string> bypassApps_;

  // UI Elements - Browse Section
  QTabWidget* browseTabWidget_;
  QLineEdit* searchEdit_;
  QComboBox* appListTypeCombo_;
  QCheckBox* showSystemAppsCheck_;
  QListWidget* browsableAppsList_;
  QPushButton* refreshInstalledButton_;
  QPushButton* refreshRunningButton_;
  QProgressBar* loadingProgress_;
  QLabel* statusLabel_;

  // UI Elements - VPN Apps List
  QListWidget* vpnAppsList_;
  QPushButton* removeVpnButton_;
  QLabel* vpnAppsCountLabel_;

  // UI Elements - Bypass Apps List
  QListWidget* bypassAppsList_;
  QPushButton* removeBypassButton_;
  QLabel* bypassAppsCountLabel_;

  // UI Elements - Custom Path
  QLineEdit* customPathEdit_;
  QPushButton* addCustomButton_;
  QPushButton* browseCustomButton_;

  // State
  QString currentSearch_;
  bool showSystemApps_{false};
  bool isLoading_{false};
};

}  // namespace veil::gui
