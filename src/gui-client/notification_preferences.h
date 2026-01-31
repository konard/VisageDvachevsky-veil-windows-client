#pragma once

#include <QSettings>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <utility>

namespace veil::gui {

/// Represents a single notification event in history
struct NotificationEvent {
  QDateTime timestamp;
  QString title;
  QString message;
  QString eventType;  // "connection", "minimized", "update", "error"

  NotificationEvent() = default;
  NotificationEvent(QString title_, QString message_, QString eventType_)
      : timestamp(QDateTime::currentDateTime()),
        title(std::move(title_)),
        message(std::move(message_)),
        eventType(std::move(eventType_)) {}
};

/// Manages notification preferences and history
class NotificationPreferences {
 public:
  /// Get the singleton instance
  static NotificationPreferences& instance();

  /// Load preferences from QSettings
  void load();

  /// Save preferences to QSettings
  void save();

  // Global notification settings
  bool isNotificationsEnabled() const { return notificationsEnabled_; }
  void setNotificationsEnabled(bool enabled);

  bool isNotificationSoundEnabled() const { return notificationSoundEnabled_; }
  void setNotificationSoundEnabled(bool enabled);

  bool isShowDetailsEnabled() const { return showDetails_; }
  void setShowDetailsEnabled(bool enabled);

  // Per-event notification toggles
  bool isConnectionEstablishedEnabled() const { return connectionEstablished_; }
  void setConnectionEstablishedEnabled(bool enabled);

  bool isConnectionLostEnabled() const { return connectionLost_; }
  void setConnectionLostEnabled(bool enabled);

  bool isMinimizeToTrayEnabled() const { return minimizeToTray_; }
  void setMinimizeToTrayEnabled(bool enabled);

  bool isUpdatesAvailableEnabled() const { return updatesAvailable_; }
  void setUpdatesAvailableEnabled(bool enabled);

  bool isErrorNotificationsEnabled() const { return errorNotifications_; }
  void setErrorNotificationsEnabled(bool enabled);

  // Notification history
  const QVector<NotificationEvent>& getHistory() const { return history_; }
  void addToHistory(const QString& title, const QString& message, const QString& eventType);
  void clearHistory();

  // Maximum number of items to keep in history
  static constexpr int kMaxHistorySize = 100;

  /// Check if a specific notification type should be shown
  bool shouldShowNotification(const QString& eventType) const;

  NotificationPreferences(const NotificationPreferences&) = delete;
  NotificationPreferences& operator=(const NotificationPreferences&) = delete;

 private:
  NotificationPreferences() = default;

  void loadHistory();
  void saveHistory();
  void trimHistory();

  // Global settings
  bool notificationsEnabled_{true};
  bool notificationSoundEnabled_{true};
  bool showDetails_{true};

  // Per-event toggles
  bool connectionEstablished_{true};
  bool connectionLost_{true};
  bool minimizeToTray_{true};
  bool updatesAvailable_{true};
  bool errorNotifications_{true};

  // Notification history
  QVector<NotificationEvent> history_;
};

}  // namespace veil::gui
