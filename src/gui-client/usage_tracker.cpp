#include "usage_tracker.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace veil::gui {

// ===================== DailyUsage Implementation =====================

QJsonObject DailyUsage::toJson() const {
  QJsonObject json;
  json["date"] = date.toString(Qt::ISODate);
  json["tx_bytes"] = static_cast<qint64>(totalTxBytes);
  json["rx_bytes"] = static_cast<qint64>(totalRxBytes);
  json["connection_count"] = connectionCount;
  json["duration_sec"] = totalDurationSec;
  return json;
}

DailyUsage DailyUsage::fromJson(const QJsonObject& json) {
  DailyUsage usage;
  usage.date = QDate::fromString(json["date"].toString(), Qt::ISODate);
  usage.totalTxBytes = static_cast<uint64_t>(json["tx_bytes"].toDouble());
  usage.totalRxBytes = static_cast<uint64_t>(json["rx_bytes"].toDouble());
  usage.connectionCount = json["connection_count"].toInt();
  usage.totalDurationSec = json["duration_sec"].toInt();
  return usage;
}

// ===================== MonthlyUsage Implementation =====================

QJsonObject MonthlyUsage::toJson() const {
  QJsonObject json;
  json["year"] = year;
  json["month"] = month;
  json["tx_bytes"] = static_cast<qint64>(totalTxBytes);
  json["rx_bytes"] = static_cast<qint64>(totalRxBytes);
  json["connection_count"] = connectionCount;
  json["duration_sec"] = totalDurationSec;
  return json;
}

MonthlyUsage MonthlyUsage::fromJson(const QJsonObject& json) {
  MonthlyUsage usage;
  usage.year = json["year"].toInt();
  usage.month = json["month"].toInt();
  usage.totalTxBytes = static_cast<uint64_t>(json["tx_bytes"].toDouble());
  usage.totalRxBytes = static_cast<uint64_t>(json["rx_bytes"].toDouble());
  usage.connectionCount = json["connection_count"].toInt();
  usage.totalDurationSec = json["duration_sec"].toInt();
  return usage;
}

QString MonthlyUsage::monthKey() const {
  return QString("%1-%2").arg(year, 4, 10, QChar('0')).arg(month, 2, 10, QChar('0'));
}

// ===================== UsageAlert Implementation =====================

QJsonObject UsageAlert::toJson() const {
  QJsonObject json;
  json["enabled"] = enabled;
  json["daily_limit_bytes"] = static_cast<qint64>(dailyLimitBytes);
  json["monthly_limit_bytes"] = static_cast<qint64>(monthlyLimitBytes);
  json["warning_percentage"] = warningPercentage;
  json["auto_disconnect"] = autoDisconnect;
  return json;
}

UsageAlert UsageAlert::fromJson(const QJsonObject& json) {
  UsageAlert alert;
  alert.enabled = json["enabled"].toBool(false);
  alert.dailyLimitBytes = static_cast<uint64_t>(json["daily_limit_bytes"].toDouble());
  alert.monthlyLimitBytes = static_cast<uint64_t>(json["monthly_limit_bytes"].toDouble());
  alert.warningPercentage = json["warning_percentage"].toInt(80);
  alert.autoDisconnect = json["auto_disconnect"].toBool(false);
  return alert;
}

// ===================== UsageTracker Implementation =====================

UsageTracker::UsageTracker(QObject* parent) : QObject(parent) {
  loadFromStorage();
}

UsageTracker::~UsageTracker() {
  saveToStorage();
}

void UsageTracker::loadFromStorage() {
  QSettings settings("VEIL", "VPN Client");

  // Load daily usage
  dailyUsage_.clear();
  int dailySize = settings.beginReadArray("usage/daily");
  for (int i = 0; i < dailySize; ++i) {
    settings.setArrayIndex(i);
    QJsonObject json = QJsonDocument::fromJson(
        settings.value("data").toString().toUtf8()).object();
    DailyUsage usage = DailyUsage::fromJson(json);
    if (usage.date.isValid()) {
      dailyUsage_[usage.date.toString(Qt::ISODate)] = usage;
    }
  }
  settings.endArray();

  // Load monthly usage
  monthlyUsage_.clear();
  int monthlySize = settings.beginReadArray("usage/monthly");
  for (int i = 0; i < monthlySize; ++i) {
    settings.setArrayIndex(i);
    QJsonObject json = QJsonDocument::fromJson(
        settings.value("data").toString().toUtf8()).object();
    MonthlyUsage usage = MonthlyUsage::fromJson(json);
    if (usage.year > 0 && usage.month >= 1 && usage.month <= 12) {
      monthlyUsage_[usage.monthKey()] = usage;
    }
  }
  settings.endArray();

  // Load alert config
  QJsonObject alertJson = QJsonDocument::fromJson(
      settings.value("usage/alert_config").toString().toUtf8()).object();
  if (!alertJson.isEmpty()) {
    alertConfig_ = UsageAlert::fromJson(alertJson);
  }
}

void UsageTracker::saveToStorage() {
  QSettings settings("VEIL", "VPN Client");

  // Save daily usage
  settings.beginWriteArray("usage/daily", dailyUsage_.size());
  int i = 0;
  for (const auto& usage : dailyUsage_) {
    settings.setArrayIndex(i++);
    QJsonDocument doc(usage.toJson());
    settings.setValue("data", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  }
  settings.endArray();

  // Save monthly usage
  settings.beginWriteArray("usage/monthly", monthlyUsage_.size());
  i = 0;
  for (const auto& usage : monthlyUsage_) {
    settings.setArrayIndex(i++);
    QJsonDocument doc(usage.toJson());
    settings.setValue("data", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  }
  settings.endArray();

  // Save alert config
  QJsonDocument alertDoc(alertConfig_.toJson());
  settings.setValue("usage/alert_config",
                   QString::fromUtf8(alertDoc.toJson(QJsonDocument::Compact)));

  settings.sync();
}

void UsageTracker::recordSession(const QDateTime& startTime, const QDateTime& endTime,
                                uint64_t txBytes, uint64_t rxBytes) {
  if (!startTime.isValid() || !endTime.isValid() || startTime > endTime) {
    return;
  }

  const QDate startDate = startTime.date();
  const QDate endDate = endTime.date();
  const qint64 totalDuration = startTime.secsTo(endTime);

  // Handle sessions that span multiple days
  if (startDate == endDate) {
    // Single day session
    const QString dateKey = startDate.toString(Qt::ISODate);
    auto& usage = dailyUsage_[dateKey];
    usage.date = startDate;
    usage.totalTxBytes += txBytes;
    usage.totalRxBytes += rxBytes;
    usage.connectionCount++;
    usage.totalDurationSec += static_cast<int>(totalDuration);

    updateMonthlyStats(startDate);
  } else {
    // Multi-day session - split proportionally by time
    qint64 secondsInFirstDay = startDate.daysTo(endDate) > 0
        ? startTime.secsTo(QDateTime(startDate.addDays(1), QTime(0, 0)))
        : totalDuration;
    qint64 secondsInLastDay = totalDuration - secondsInFirstDay;

    // First day
    const QString startDateKey = startDate.toString(Qt::ISODate);
    auto& startUsage = dailyUsage_[startDateKey];
    startUsage.date = startDate;
    double firstDayRatio = static_cast<double>(secondsInFirstDay) / totalDuration;
    startUsage.totalTxBytes += static_cast<uint64_t>(txBytes * firstDayRatio);
    startUsage.totalRxBytes += static_cast<uint64_t>(rxBytes * firstDayRatio);
    startUsage.connectionCount++;
    startUsage.totalDurationSec += static_cast<int>(secondsInFirstDay);
    updateMonthlyStats(startDate);

    // Last day (simplified - assumes only 2 days max)
    const QString endDateKey = endDate.toString(Qt::ISODate);
    auto& endUsage = dailyUsage_[endDateKey];
    endUsage.date = endDate;
    double lastDayRatio = static_cast<double>(secondsInLastDay) / totalDuration;
    endUsage.totalTxBytes += static_cast<uint64_t>(txBytes * lastDayRatio);
    endUsage.totalRxBytes += static_cast<uint64_t>(rxBytes * lastDayRatio);
    endUsage.connectionCount++;
    endUsage.totalDurationSec += static_cast<int>(secondsInLastDay);
    updateMonthlyStats(endDate);
  }

  // Clean up old data
  if (dailyUsage_.size() > kMaxDailyRecords * 2) {  // Give some buffer
    clearOldData(kMaxDailyRecords);
  }

  checkAndTriggerAlerts();
  emit usageUpdated();
  saveToStorage();
}

void UsageTracker::updateMonthlyStats(const QDate& date) {
  const int year = date.year();
  const int month = date.month();
  const QString monthKey = QString("%1-%2").arg(year, 4, 10, QChar('0'))
                                          .arg(month, 2, 10, QChar('0'));

  // Aggregate all daily data for this month
  MonthlyUsage monthlyUsage;
  monthlyUsage.year = year;
  monthlyUsage.month = month;

  QDate firstDay(year, month, 1);
  QDate lastDay(year, month, firstDay.daysInMonth());

  for (QDate d = firstDay; d <= lastDay; d = d.addDays(1)) {
    const QString dateKey = d.toString(Qt::ISODate);
    if (dailyUsage_.contains(dateKey)) {
      const auto& daily = dailyUsage_[dateKey];
      monthlyUsage.totalTxBytes += daily.totalTxBytes;
      monthlyUsage.totalRxBytes += daily.totalRxBytes;
      monthlyUsage.connectionCount += daily.connectionCount;
      monthlyUsage.totalDurationSec += daily.totalDurationSec;
    }
  }

  monthlyUsage_[monthKey] = monthlyUsage;

  // Clean up old monthly records
  if (monthlyUsage_.size() > kMaxMonthlyRecords * 2) {
    QList<QString> keys = monthlyUsage_.keys();
    std::sort(keys.begin(), keys.end());
    while (keys.size() > kMaxMonthlyRecords) {
      monthlyUsage_.remove(keys.first());
      keys.removeFirst();
    }
  }
}

DailyUsage UsageTracker::getDailyUsage(const QDate& date) const {
  const QString dateKey = date.toString(Qt::ISODate);
  if (dailyUsage_.contains(dateKey)) {
    return dailyUsage_[dateKey];
  }
  DailyUsage empty;
  empty.date = date;
  return empty;
}

QList<DailyUsage> UsageTracker::getAllDailyUsage() const {
  QList<DailyUsage> result = dailyUsage_.values();
  std::sort(result.begin(), result.end(), [](const DailyUsage& a, const DailyUsage& b) {
    return a.date > b.date;  // Newest first
  });
  return result;
}

QList<DailyUsage> UsageTracker::getDailyUsageRange(const QDate& startDate,
                                                   const QDate& endDate) const {
  QList<DailyUsage> result;
  for (QDate d = startDate; d <= endDate; d = d.addDays(1)) {
    const QString dateKey = d.toString(Qt::ISODate);
    if (dailyUsage_.contains(dateKey)) {
      result.append(dailyUsage_[dateKey]);
    }
  }
  return result;
}

MonthlyUsage UsageTracker::getMonthlyUsage(int year, int month) const {
  const QString key = QString("%1-%2").arg(year, 4, 10, QChar('0'))
                                     .arg(month, 2, 10, QChar('0'));
  if (monthlyUsage_.contains(key)) {
    return monthlyUsage_[key];
  }
  MonthlyUsage empty;
  empty.year = year;
  empty.month = month;
  return empty;
}

QList<MonthlyUsage> UsageTracker::getAllMonthlyUsage() const {
  QList<MonthlyUsage> result = monthlyUsage_.values();
  std::sort(result.begin(), result.end(), [](const MonthlyUsage& a, const MonthlyUsage& b) {
    if (a.year != b.year) return a.year > b.year;
    return a.month > b.month;  // Newest first
  });
  return result;
}

MonthlyUsage UsageTracker::getCurrentMonthUsage() const {
  const QDate today = QDate::currentDate();
  return getMonthlyUsage(today.year(), today.month());
}

DailyUsage UsageTracker::getTodayUsage() const {
  return getDailyUsage(QDate::currentDate());
}

void UsageTracker::setAlertConfig(const UsageAlert& config) {
  alertConfig_ = config;
  saveToStorage();
  checkAndTriggerAlerts();
}

UsageTracker::AlertStatus UsageTracker::checkAlerts() const {
  AlertStatus status;

  if (!alertConfig_.enabled) {
    return status;
  }

  const auto todayUsage = getTodayUsage();
  const auto monthUsage = getCurrentMonthUsage();

  // Check daily limit
  if (alertConfig_.dailyLimitBytes > 0) {
    const uint64_t todayTotal = todayUsage.totalBytes();
    const uint64_t warningThreshold =
        alertConfig_.dailyLimitBytes * alertConfig_.warningPercentage / 100;

    if (todayTotal >= alertConfig_.dailyLimitBytes) {
      status.exceeded = true;
      status.isWarning = false;
      status.message = QString("Daily data limit reached: %1 / %2")
          .arg(formatBytes(todayTotal))
          .arg(formatBytes(alertConfig_.dailyLimitBytes));
      return status;
    } else if (todayTotal >= warningThreshold) {
      status.exceeded = true;
      status.isWarning = true;
      status.message = QString("Daily data usage at %1%: %2 / %3")
          .arg(todayTotal * 100 / alertConfig_.dailyLimitBytes)
          .arg(formatBytes(todayTotal))
          .arg(formatBytes(alertConfig_.dailyLimitBytes));
      return status;
    }
  }

  // Check monthly limit
  if (alertConfig_.monthlyLimitBytes > 0) {
    const uint64_t monthTotal = monthUsage.totalBytes();
    const uint64_t warningThreshold =
        alertConfig_.monthlyLimitBytes * alertConfig_.warningPercentage / 100;

    if (monthTotal >= alertConfig_.monthlyLimitBytes) {
      status.exceeded = true;
      status.isWarning = false;
      status.message = QString("Monthly data limit reached: %1 / %2")
          .arg(formatBytes(monthTotal))
          .arg(formatBytes(alertConfig_.monthlyLimitBytes));
      return status;
    } else if (monthTotal >= warningThreshold) {
      status.exceeded = true;
      status.isWarning = true;
      status.message = QString("Monthly data usage at %1%: %2 / %3")
          .arg(monthTotal * 100 / alertConfig_.monthlyLimitBytes)
          .arg(formatBytes(monthTotal))
          .arg(formatBytes(alertConfig_.monthlyLimitBytes));
      return status;
    }
  }

  return status;
}

void UsageTracker::checkAndTriggerAlerts() {
  const auto status = checkAlerts();
  if (status.exceeded) {
    emit alertTriggered(status.message, status.isWarning);
  }
}

void UsageTracker::clearAllData() {
  dailyUsage_.clear();
  monthlyUsage_.clear();
  saveToStorage();
  emit usageUpdated();
}

void UsageTracker::clearOldData(int daysToKeep) {
  const QDate cutoffDate = QDate::currentDate().addDays(-daysToKeep);

  // Remove old daily records
  QList<QString> keysToRemove;
  for (auto it = dailyUsage_.constBegin(); it != dailyUsage_.constEnd(); ++it) {
    if (it.value().date < cutoffDate) {
      keysToRemove.append(it.key());
    }
  }
  for (const auto& key : keysToRemove) {
    dailyUsage_.remove(key);
  }

  // Recalculate affected monthly stats
  QSet<QString> affectedMonths;
  for (const auto& key : keysToRemove) {
    QDate date = QDate::fromString(key, Qt::ISODate);
    if (date.isValid()) {
      affectedMonths.insert(QString("%1-%2").arg(date.year(), 4, 10, QChar('0'))
                                           .arg(date.month(), 2, 10, QChar('0')));
    }
  }
  for (const auto& monthKey : affectedMonths) {
    if (monthlyUsage_.contains(monthKey)) {
      const auto& monthly = monthlyUsage_[monthKey];
      updateMonthlyStats(QDate(monthly.year, monthly.month, 1));
    }
  }

  if (!keysToRemove.isEmpty()) {
    saveToStorage();
    emit usageUpdated();
  }
}

QString UsageTracker::exportToJson() const {
  QJsonObject root;

  // Export daily usage
  QJsonArray dailyArray;
  for (const auto& usage : dailyUsage_) {
    dailyArray.append(usage.toJson());
  }
  root["daily_usage"] = dailyArray;

  // Export monthly usage
  QJsonArray monthlyArray;
  for (const auto& usage : monthlyUsage_) {
    monthlyArray.append(usage.toJson());
  }
  root["monthly_usage"] = monthlyArray;

  // Export alert config
  root["alert_config"] = alertConfig_.toJson();

  root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  root["version"] = "1.0";

  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString UsageTracker::exportDailyToCsv() const {
  QString csv;
  csv += "Date,TX Bytes,RX Bytes,Total Bytes,Connections,Duration (s)\n";

  auto dailyList = getAllDailyUsage();
  for (const auto& usage : dailyList) {
    csv += QString("%1,%2,%3,%4,%5,%6\n")
        .arg(usage.date.toString(Qt::ISODate))
        .arg(usage.totalTxBytes)
        .arg(usage.totalRxBytes)
        .arg(usage.totalBytes())
        .arg(usage.connectionCount)
        .arg(usage.totalDurationSec);
  }

  return csv;
}

QString UsageTracker::exportMonthlyToCsv() const {
  QString csv;
  csv += "Year,Month,TX Bytes,RX Bytes,Total Bytes,Connections,Duration (s)\n";

  auto monthlyList = getAllMonthlyUsage();
  for (const auto& usage : monthlyList) {
    csv += QString("%1,%2,%3,%4,%5,%6,%7\n")
        .arg(usage.year)
        .arg(usage.month)
        .arg(usage.totalTxBytes)
        .arg(usage.totalRxBytes)
        .arg(usage.totalBytes())
        .arg(usage.connectionCount)
        .arg(usage.totalDurationSec);
  }

  return csv;
}

// Helper function to format bytes
QString formatBytes(uint64_t bytes) {
  if (bytes >= 1073741824ULL) {
    return QString("%1 GB").arg(static_cast<double>(bytes) / 1073741824.0, 0, 'f', 2);
  } else if (bytes >= 1048576ULL) {
    return QString("%1 MB").arg(static_cast<double>(bytes) / 1048576.0, 0, 'f', 1);
  } else if (bytes >= 1024ULL) {
    return QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  return QString("%1 B").arg(bytes);
}

}  // namespace veil::gui
