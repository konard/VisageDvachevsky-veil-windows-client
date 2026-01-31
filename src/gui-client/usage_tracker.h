#pragma once

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QJsonObject>
#include <cstdint>

namespace veil::gui {

/// Daily usage statistics for a specific date
struct DailyUsage {
  QDate date;
  uint64_t totalTxBytes{0};
  uint64_t totalRxBytes{0};
  int connectionCount{0};
  int totalDurationSec{0};

  /// Convert to JSON for storage
  QJsonObject toJson() const;

  /// Create from JSON
  static DailyUsage fromJson(const QJsonObject& json);

  /// Get total bytes (tx + rx)
  uint64_t totalBytes() const { return totalTxBytes + totalRxBytes; }
};

/// Monthly usage statistics for a specific month
struct MonthlyUsage {
  int year{0};
  int month{0};  // 1-12
  uint64_t totalTxBytes{0};
  uint64_t totalRxBytes{0};
  int connectionCount{0};
  int totalDurationSec{0};

  /// Convert to JSON for storage
  QJsonObject toJson() const;

  /// Create from JSON
  static MonthlyUsage fromJson(const QJsonObject& json);

  /// Get total bytes (tx + rx)
  uint64_t totalBytes() const { return totalTxBytes + totalRxBytes; }

  /// Get month key for storage (YYYY-MM format)
  QString monthKey() const;
};

/// Usage alert configuration
struct UsageAlert {
  bool enabled{false};
  uint64_t dailyLimitBytes{0};     // 0 = no limit
  uint64_t monthlyLimitBytes{0};   // 0 = no limit
  int warningPercentage{80};       // Show warning at 80% by default
  bool autoDisconnect{false};      // Auto-disconnect when limit reached

  /// Convert to JSON for storage
  QJsonObject toJson() const;

  /// Create from JSON
  static UsageAlert fromJson(const QJsonObject& json);
};

/// Usage tracker for persistent data usage statistics
///
/// This class tracks and persists daily and monthly VPN data usage.
/// It provides aggregation, alerting, and export functionality.
class UsageTracker : public QObject {
  Q_OBJECT

 public:
  explicit UsageTracker(QObject* parent = nullptr);
  ~UsageTracker() override;

  /// Load usage data from persistent storage
  void loadFromStorage();

  /// Save usage data to persistent storage
  void saveToStorage();

  /// Record a completed connection session
  void recordSession(const QDateTime& startTime, const QDateTime& endTime,
                    uint64_t txBytes, uint64_t rxBytes);

  /// Get daily usage for a specific date
  DailyUsage getDailyUsage(const QDate& date) const;

  /// Get all daily usage records (sorted by date, newest first)
  QList<DailyUsage> getAllDailyUsage() const;

  /// Get daily usage for a date range
  QList<DailyUsage> getDailyUsageRange(const QDate& startDate, const QDate& endDate) const;

  /// Get monthly usage for a specific month
  MonthlyUsage getMonthlyUsage(int year, int month) const;

  /// Get all monthly usage records (sorted by date, newest first)
  QList<MonthlyUsage> getAllMonthlyUsage() const;

  /// Get current month's usage
  MonthlyUsage getCurrentMonthUsage() const;

  /// Get current day's usage
  DailyUsage getTodayUsage() const;

  /// Get usage alert configuration
  UsageAlert getAlertConfig() const { return alertConfig_; }

  /// Set usage alert configuration
  void setAlertConfig(const UsageAlert& config);

  /// Check if current usage exceeds alert thresholds
  /// Returns {exceeded, isWarning, message}
  struct AlertStatus {
    bool exceeded{false};
    bool isWarning{false};  // true = warning level, false = limit reached
    QString message;
  };
  AlertStatus checkAlerts() const;

  /// Clear all usage data (with confirmation)
  void clearAllData();

  /// Clear data older than specified days
  void clearOldData(int daysToKeep);

  /// Export usage data to JSON
  QString exportToJson() const;

  /// Export daily usage to CSV
  QString exportDailyToCsv() const;

  /// Export monthly usage to CSV
  QString exportMonthlyToCsv() const;

 signals:
  /// Emitted when usage alert threshold is reached
  void alertTriggered(const QString& message, bool isWarning);

  /// Emitted when usage data is updated
  void usageUpdated();

 private:
  /// Aggregate daily data into monthly stats
  void updateMonthlyStats(const QDate& date);

  /// Check and trigger alerts if needed
  void checkAndTriggerAlerts();

  /// Map of date string (YYYY-MM-DD) to daily usage
  QMap<QString, DailyUsage> dailyUsage_;

  /// Map of month key (YYYY-MM) to monthly usage
  QMap<QString, MonthlyUsage> monthlyUsage_;

  /// Alert configuration
  UsageAlert alertConfig_;

  /// Maximum number of daily records to keep (default: 90 days)
  static constexpr int kMaxDailyRecords = 90;

  /// Maximum number of monthly records to keep (default: 24 months)
  static constexpr int kMaxMonthlyRecords = 24;
};

}  // namespace veil::gui
