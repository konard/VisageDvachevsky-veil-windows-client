#include "usage_tracker.h"

#include <QJsonDocument>
#include <QDebug>

namespace veil::gui {

UsageTracker::UsageTracker(QObject* parent) : QObject(parent) {
  // Auto-save every 30 seconds if data changed
  saveTimer_.setInterval(30000);
  connect(&saveTimer_, &QTimer::timeout, this, [this]() {
    if (dirty_) {
      save();
      dirty_ = false;
    }
  });
  saveTimer_.start();
}

UsageTracker::~UsageTracker() {
  // Save on destruction
  if (dirty_) {
    save();
  }
}

void UsageTracker::load() {
  QSettings settings("VEIL", "VPN Client");

  // Load daily records
  dailyRecords_.clear();
  QString recordsJson = settings.value("usage/daily_records", "[]").toString();
  QJsonDocument doc = QJsonDocument::fromJson(recordsJson.toUtf8());
  if (doc.isArray()) {
    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
      if (val.isObject()) {
        auto record = DailyUsageRecord::fromJson(val.toObject());
        if (record.date.isValid()) {
          dailyRecords_.push_back(record);
        }
      }
    }
  }

  // Load alert settings
  QString alertJson = settings.value("usage/alert_settings", "{}").toString();
  QJsonDocument alertDoc = QJsonDocument::fromJson(alertJson.toUtf8());
  if (alertDoc.isObject()) {
    alertSettings_ = UsageAlert::fromJson(alertDoc.object());
  }

  pruneOldRecords();

  qDebug() << "[UsageTracker] Loaded" << dailyRecords_.size() << "daily usage records";
}

void UsageTracker::save() {
  QSettings settings("VEIL", "VPN Client");

  // Save daily records
  QJsonArray arr;
  for (const auto& record : dailyRecords_) {
    arr.append(record.toJson());
  }
  settings.setValue("usage/daily_records",
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

  // Save alert settings
  settings.setValue("usage/alert_settings",
                    QString::fromUtf8(QJsonDocument(alertSettings_.toJson()).toJson(QJsonDocument::Compact)));

  settings.sync();
}

void UsageTracker::recordBytes(uint64_t txBytes, uint64_t rxBytes) {
  if (!sessionActive_) return;

  // Calculate delta from last known values
  uint64_t deltaTx = (txBytes > sessionTxBytes_) ? (txBytes - sessionTxBytes_) : 0;
  uint64_t deltaRx = (rxBytes > sessionRxBytes_) ? (rxBytes - sessionRxBytes_) : 0;

  sessionTxBytes_ = txBytes;
  sessionRxBytes_ = rxBytes;

  if (deltaTx == 0 && deltaRx == 0) return;

  ensureTodayRecord();

  // Add to today's record
  auto& today = dailyRecords_.back();
  today.txBytes += deltaTx;
  today.rxBytes += deltaRx;

  dirty_ = true;
  emit usageUpdated();
  checkAlerts();
}

void UsageTracker::onSessionStarted() {
  sessionActive_ = true;
  sessionStartTime_ = QDateTime::currentDateTime();
  sessionTxBytes_ = 0;
  sessionRxBytes_ = 0;

  ensureTodayRecord();
  dailyRecords_.back().sessionCount++;
  dirty_ = true;

  // Reset alert emission state for new session
  warningEmitted_ = false;
  limitEmitted_ = false;

  qDebug() << "[UsageTracker] Session started";
}

void UsageTracker::onSessionEnded() {
  if (!sessionActive_) return;
  sessionActive_ = false;

  // Record session duration
  if (sessionStartTime_.isValid()) {
    qint64 durationSec = sessionStartTime_.secsTo(QDateTime::currentDateTime());
    if (durationSec > 0) {
      ensureTodayRecord();
      dailyRecords_.back().totalDurationSec += static_cast<uint64_t>(durationSec);
    }
  }

  dirty_ = true;
  save();  // Force save on session end
  dirty_ = false;

  qDebug() << "[UsageTracker] Session ended";
}

std::vector<DailyUsageRecord> UsageTracker::getDailyUsage(const QDate& from,
                                                           const QDate& to) const {
  std::vector<DailyUsageRecord> result;
  for (const auto& record : dailyRecords_) {
    if (record.date >= from && record.date <= to) {
      result.push_back(record);
    }
  }
  return result;
}

DailyUsageRecord UsageTracker::getMonthlyUsage(int year, int month) const {
  DailyUsageRecord aggregate;
  aggregate.date = QDate(year, month, 1);

  for (const auto& record : dailyRecords_) {
    if (record.date.year() == year && record.date.month() == month) {
      aggregate.txBytes += record.txBytes;
      aggregate.rxBytes += record.rxBytes;
      aggregate.sessionCount += record.sessionCount;
      aggregate.totalDurationSec += record.totalDurationSec;
    }
  }
  return aggregate;
}

DailyUsageRecord UsageTracker::getTodayUsage() const {
  QDate today = QDate::currentDate();
  for (const auto& record : dailyRecords_) {
    if (record.date == today) {
      return record;
    }
  }
  DailyUsageRecord empty;
  empty.date = today;
  return empty;
}

DailyUsageRecord UsageTracker::getCurrentMonthUsage() const {
  QDate today = QDate::currentDate();
  return getMonthlyUsage(today.year(), today.month());
}

void UsageTracker::setAlertSettings(const UsageAlert& settings) {
  alertSettings_ = settings;
  warningEmitted_ = false;
  limitEmitted_ = false;
  dirty_ = true;
  save();
  dirty_ = false;
}

void UsageTracker::clearHistory() {
  dailyRecords_.clear();
  dirty_ = true;
  save();
  dirty_ = false;
  emit usageUpdated();
}

void UsageTracker::ensureTodayRecord() {
  QDate today = QDate::currentDate();

  if (dailyRecords_.empty() || dailyRecords_.back().date != today) {
    DailyUsageRecord record;
    record.date = today;
    dailyRecords_.push_back(record);
  }
}

void UsageTracker::pruneOldRecords() {
  QDate cutoff = QDate::currentDate().addDays(-kMaxDaysRetained);
  while (!dailyRecords_.empty() && dailyRecords_.front().date < cutoff) {
    dailyRecords_.pop_front();
  }
}

void UsageTracker::checkAlerts() {
  if (!alertSettings_.enabled) return;

  auto monthUsage = getCurrentMonthUsage();
  uint64_t totalUsage = monthUsage.totalBytes();

  if (alertSettings_.warningThresholdBytes > 0 &&
      totalUsage >= alertSettings_.warningThresholdBytes && !warningEmitted_) {
    warningEmitted_ = true;
    emit warningThresholdReached(totalUsage, alertSettings_.warningThresholdBytes);
  }

  if (alertSettings_.limitThresholdBytes > 0 &&
      totalUsage >= alertSettings_.limitThresholdBytes && !limitEmitted_) {
    limitEmitted_ = true;
    emit limitThresholdReached(totalUsage, alertSettings_.limitThresholdBytes);
  }
}

}  // namespace veil::gui
