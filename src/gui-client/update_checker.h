#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>

namespace veil::gui {

/// Information about an available update
struct UpdateInfo {
  QString version;           // e.g., "1.1.0"
  QString releaseUrl;        // URL to the release page
  QString downloadUrl;       // Direct download URL for the installer
  QString releaseNotes;      // Brief release notes
  QString publishedAt;       // Release date
  bool isPrerelease{false};  // Whether it's a prerelease
};

/// Checks for application updates via GitHub Releases API
class UpdateChecker : public QObject {
  Q_OBJECT

 public:
  explicit UpdateChecker(QObject* parent = nullptr);
  ~UpdateChecker() override;

  /// Check for updates asynchronously
  void checkForUpdates();

  /// Get the current application version
  static QString currentVersion();

  /// Compare two version strings (returns: -1 if v1<v2, 0 if equal, 1 if v1>v2)
  static int compareVersions(const QString& v1, const QString& v2);

 signals:
  /// Emitted when an update is available
  void updateAvailable(const UpdateInfo& info);

  /// Emitted when no update is available (current version is latest)
  void noUpdateAvailable();

  /// Emitted when update check fails
  void checkFailed(const QString& error);

 private slots:
  void onNetworkReply(QNetworkReply* reply);

 private:
  std::unique_ptr<QNetworkAccessManager> networkManager_;
  bool checkInProgress_{false};
};

}  // namespace veil::gui
