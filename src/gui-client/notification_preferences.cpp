#include "notification_preferences.h"

namespace veil::gui {

NotificationPreferences& NotificationPreferences::instance() {
  static NotificationPreferences instance;
  return instance;
}

void NotificationPreferences::load() {
  QSettings settings("VEIL", "VPN Client");

  // Load global settings
  notificationsEnabled_ = settings.value("notifications/enabled", true).toBool();
  notificationSoundEnabled_ = settings.value("notifications/sound", true).toBool();
  showDetails_ = settings.value("notifications/showDetails", true).toBool();

  // Load per-event toggles
  connectionEstablished_ = settings.value("notifications/connectionEstablished", true).toBool();
  connectionLost_ = settings.value("notifications/connectionLost", true).toBool();
  minimizeToTray_ = settings.value("notifications/minimizeToTray", true).toBool();
  updatesAvailable_ = settings.value("notifications/updatesAvailable", true).toBool();
  errorNotifications_ = settings.value("notifications/errors", true).toBool();

  // Load history
  loadHistory();
}

void NotificationPreferences::save() {
  QSettings settings("VEIL", "VPN Client");

  // Save global settings
  settings.setValue("notifications/enabled", notificationsEnabled_);
  settings.setValue("notifications/sound", notificationSoundEnabled_);
  settings.setValue("notifications/showDetails", showDetails_);

  // Save per-event toggles
  settings.setValue("notifications/connectionEstablished", connectionEstablished_);
  settings.setValue("notifications/connectionLost", connectionLost_);
  settings.setValue("notifications/minimizeToTray", minimizeToTray_);
  settings.setValue("notifications/updatesAvailable", updatesAvailable_);
  settings.setValue("notifications/errors", errorNotifications_);

  // Save history
  saveHistory();
}

void NotificationPreferences::setNotificationsEnabled(bool enabled) {
  notificationsEnabled_ = enabled;
}

void NotificationPreferences::setNotificationSoundEnabled(bool enabled) {
  notificationSoundEnabled_ = enabled;
}

void NotificationPreferences::setShowDetailsEnabled(bool enabled) {
  showDetails_ = enabled;
}

void NotificationPreferences::setConnectionEstablishedEnabled(bool enabled) {
  connectionEstablished_ = enabled;
}

void NotificationPreferences::setConnectionLostEnabled(bool enabled) {
  connectionLost_ = enabled;
}

void NotificationPreferences::setMinimizeToTrayEnabled(bool enabled) {
  minimizeToTray_ = enabled;
}

void NotificationPreferences::setUpdatesAvailableEnabled(bool enabled) {
  updatesAvailable_ = enabled;
}

void NotificationPreferences::setErrorNotificationsEnabled(bool enabled) {
  errorNotifications_ = enabled;
}

void NotificationPreferences::addToHistory(const QString& title, const QString& message, const QString& eventType) {
  history_.prepend(NotificationEvent(title, message, eventType));
  trimHistory();
}

void NotificationPreferences::clearHistory() {
  history_.clear();
  saveHistory();
}

bool NotificationPreferences::shouldShowNotification(const QString& eventType) const {
  // Check if notifications are globally disabled
  if (!notificationsEnabled_) {
    return false;
  }

  // Check specific event type
  if (eventType == "connection_established") {
    return connectionEstablished_;
  } else if (eventType == "connection_lost") {
    return connectionLost_;
  } else if (eventType == "minimized") {
    return minimizeToTray_;
  } else if (eventType == "update") {
    return updatesAvailable_;
  } else if (eventType == "error") {
    return errorNotifications_;
  }

  // Unknown event types are shown by default if notifications are enabled
  return true;
}

void NotificationPreferences::loadHistory() {
  QSettings settings("VEIL", "VPN Client");

  int size = settings.beginReadArray("notifications/history");
  history_.clear();
  history_.reserve(size);

  for (int i = 0; i < size; ++i) {
    settings.setArrayIndex(i);
    NotificationEvent event;
    event.timestamp = settings.value("timestamp").toDateTime();
    event.title = settings.value("title").toString();
    event.message = settings.value("message").toString();
    event.eventType = settings.value("eventType").toString();
    history_.append(event);
  }

  settings.endArray();
}

void NotificationPreferences::saveHistory() {
  QSettings settings("VEIL", "VPN Client");

  settings.beginWriteArray("notifications/history");
  for (int i = 0; i < history_.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue("timestamp", history_[i].timestamp);
    settings.setValue("title", history_[i].title);
    settings.setValue("message", history_[i].message);
    settings.setValue("eventType", history_[i].eventType);
  }
  settings.endArray();
}

void NotificationPreferences::trimHistory() {
  while (history_.size() > kMaxHistorySize) {
    history_.removeLast();
  }
}

}  // namespace veil::gui
