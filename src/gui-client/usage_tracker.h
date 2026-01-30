#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>

#include <deque>
#include <vector>

namespace veil::gui {

/// Record of data usage for a single day
struct DailyUsageRecord {
  QDate date;
  uint64_t txBytes{0};  ///< Total bytes uploaded
  uint64_t rxBytes{0};  ///< Total bytes downloaded
  uint64_t sessionCount{0};  ///< Number of VPN sessions
  uint64_t totalDurationSec{0};  ///< Total connected time in seconds

  uint64_t totalBytes() const { return txBytes + rxBytes; }

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["date"] = date.toString(Qt::ISODate);
    obj["tx_bytes"] = static_cast<qint64>(txBytes);
    obj["rx_bytes"] = static_cast<qint64>(rxBytes);
    obj["session_count"] = static_cast<qint64>(sessionCount);
    obj["total_duration_sec"] = static_cast<qint64>(totalDurationSec);
    return obj;
  }

  static DailyUsageRecord fromJson(const QJsonObject& obj) {
    DailyUsageRecord record;
    record.date = QDate::fromString(obj["date"].toString(), Qt::ISODate);
    record.txBytes = static_cast<uint64_t>(obj["tx_bytes"].toDouble());
    record.rxBytes = static_cast<uint64_t>(obj["rx_bytes"].toDouble());
    record.sessionCount = static_cast<uint64_t>(obj["session_count"].toDouble());
    record.totalDurationSec = static_cast<uint64_t>(obj["total_duration_sec"].toDouble());
    return record;
  }
};

/// Usage alert threshold configuration
struct UsageAlert {
  bool enabled{false};
  uint64_t warningThresholdBytes{0};    ///< Warn when usage exceeds this
  uint64_t limitThresholdBytes{0};      ///< Hard limit (optional auto-disconnect)
  bool autoDisconnectAtLimit{false};    ///< Whether to auto-disconnect at limit
  bool monthlyReset{true};             ///< Reset counters at month start

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["warning_threshold_bytes"] = static_cast<qint64>(warningThresholdBytes);
    obj["limit_threshold_bytes"] = static_cast<qint64>(limitThresholdBytes);
    obj["auto_disconnect_at_limit"] = autoDisconnectAtLimit;
    obj["monthly_reset"] = monthlyReset;
    return obj;
  }

  static UsageAlert fromJson(const QJsonObject& obj) {
    UsageAlert alert;
    alert.enabled = obj["enabled"].toBool();
    alert.warningThresholdBytes = static_cast<uint64_t>(obj["warning_threshold_bytes"].toDouble());
    alert.limitThresholdBytes = static_cast<uint64_t>(obj["limit_threshold_bytes"].toDouble());
    alert.autoDisconnectAtLimit = obj["auto_disconnect_at_limit"].toBool();
    alert.monthlyReset = obj["monthly_reset"].toBool();
    return alert;
  }
};

/// Persistent usage tracker that stores daily usage data
class UsageTracker : public QObject {
  Q_OBJECT

 public:
  explicit UsageTracker(QObject* parent = nullptr);
  ~UsageTracker() override;

  /// Load usage data from persistent storage
  void load();

  /// Save usage data to persistent storage
  void save();

  /// Record bytes transferred during current session
  void recordBytes(uint64_t txBytes, uint64_t rxBytes);

  /// Called when a VPN session starts
  void onSessionStarted();

  /// Called when a VPN session ends
  void onSessionEnded();

  /// Get daily usage records for a date range (inclusive)
  std::vector<DailyUsageRecord> getDailyUsage(const QDate& from, const QDate& to) const;

  /// Get aggregated monthly usage for a given month (year, month)
  DailyUsageRecord getMonthlyUsage(int year, int month) const;

  /// Get today's usage
  DailyUsageRecord getTodayUsage() const;

  /// Get current month's total usage
  DailyUsageRecord getCurrentMonthUsage() const;

  /// Get usage alert settings
  const UsageAlert& alertSettings() const { return alertSettings_; }

  /// Set usage alert settings
  void setAlertSettings(const UsageAlert& settings);

  /// Clear all usage history
  void clearHistory();

  /// Maximum number of days to retain (default 365 = 1 year)
  static constexpr int kMaxDaysRetained = 365;

 signals:
  /// Emitted when usage data is updated
  void usageUpdated();

  /// Emitted when warning threshold is reached
  void warningThresholdReached(uint64_t currentUsage, uint64_t threshold);

  /// Emitted when limit threshold is reached
  void limitThresholdReached(uint64_t currentUsage, uint64_t limit);

 private:
  void ensureTodayRecord();
  void pruneOldRecords();
  void checkAlerts();

  std::deque<DailyUsageRecord> dailyRecords_;
  UsageAlert alertSettings_;

  // Current session tracking
  bool sessionActive_{false};
  QDateTime sessionStartTime_;
  uint64_t sessionTxBytes_{0};
  uint64_t sessionRxBytes_{0};

  // Throttle saves (avoid writing to disk every second)
  QTimer saveTimer_;
  bool dirty_{false};

  // Alert state tracking
  bool warningEmitted_{false};
  bool limitEmitted_{false};
};

}  // namespace veil::gui
