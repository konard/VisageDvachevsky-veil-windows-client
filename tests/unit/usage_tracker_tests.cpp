#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QSettings>

#include "gui-client/usage_tracker.h"

namespace veil::gui {

class UsageTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use test-specific settings to avoid interfering with user data
    QCoreApplication::setOrganizationName("VEIL-Test");
    QCoreApplication::setApplicationName("VPN Client Test");

    // Clear any existing test data
    QSettings settings("VEIL-Test", "VPN Client Test");
    settings.clear();
  }

  void TearDown() override {
    // Clean up test data
    QSettings settings("VEIL-Test", "VPN Client Test");
    settings.clear();
  }
};

TEST_F(UsageTrackerTest, RecordSingleSession) {
  UsageTracker tracker;

  QDateTime start(QDate(2024, 1, 15), QTime(10, 0, 0));
  QDateTime end(QDate(2024, 1, 15), QTime(11, 0, 0));
  uint64_t txBytes = 1048576;  // 1 MB
  uint64_t rxBytes = 2097152;  // 2 MB

  tracker.recordSession(start, end, txBytes, rxBytes);

  auto dailyUsage = tracker.getDailyUsage(QDate(2024, 1, 15));
  EXPECT_EQ(dailyUsage.date, QDate(2024, 1, 15));
  EXPECT_EQ(dailyUsage.totalTxBytes, txBytes);
  EXPECT_EQ(dailyUsage.totalRxBytes, rxBytes);
  EXPECT_EQ(dailyUsage.connectionCount, 1);
  EXPECT_EQ(dailyUsage.totalDurationSec, 3600);  // 1 hour
}

TEST_F(UsageTrackerTest, RecordMultipleSessions) {
  UsageTracker tracker;

  // Session 1
  QDateTime start1(QDate(2024, 1, 15), QTime(10, 0, 0));
  QDateTime end1(QDate(2024, 1, 15), QTime(11, 0, 0));
  tracker.recordSession(start1, end1, 1000000, 2000000);

  // Session 2 on same day
  QDateTime start2(QDate(2024, 1, 15), QTime(14, 0, 0));
  QDateTime end2(QDate(2024, 1, 15), QTime(15, 30, 0));
  tracker.recordSession(start2, end2, 500000, 1500000);

  auto dailyUsage = tracker.getDailyUsage(QDate(2024, 1, 15));
  EXPECT_EQ(dailyUsage.totalTxBytes, 1500000ULL);
  EXPECT_EQ(dailyUsage.totalRxBytes, 3500000ULL);
  EXPECT_EQ(dailyUsage.connectionCount, 2);
  EXPECT_EQ(dailyUsage.totalDurationSec, 9000);  // 1h + 1.5h
}

TEST_F(UsageTrackerTest, MonthlyAggregation) {
  UsageTracker tracker;

  // Record sessions on different days in January 2024
  for (int day = 1; day <= 5; ++day) {
    QDateTime start(QDate(2024, 1, day), QTime(10, 0, 0));
    QDateTime end(QDate(2024, 1, day), QTime(11, 0, 0));
    tracker.recordSession(start, end, 1000000, 2000000);
  }

  auto monthlyUsage = tracker.getMonthlyUsage(2024, 1);
  EXPECT_EQ(monthlyUsage.year, 2024);
  EXPECT_EQ(monthlyUsage.month, 1);
  EXPECT_EQ(monthlyUsage.totalTxBytes, 5000000ULL);
  EXPECT_EQ(monthlyUsage.totalRxBytes, 10000000ULL);
  EXPECT_EQ(monthlyUsage.connectionCount, 5);
}

TEST_F(UsageTrackerTest, PersistenceTest) {
  {
    UsageTracker tracker;
    QDateTime start(QDate(2024, 1, 15), QTime(10, 0, 0));
    QDateTime end(QDate(2024, 1, 15), QTime(11, 0, 0));
    tracker.recordSession(start, end, 1048576, 2097152);
    // Tracker destructor saves data
  }

  // Create new tracker instance and verify data is loaded
  {
    UsageTracker tracker;
    auto dailyUsage = tracker.getDailyUsage(QDate(2024, 1, 15));
    EXPECT_EQ(dailyUsage.totalTxBytes, 1048576ULL);
    EXPECT_EQ(dailyUsage.totalRxBytes, 2097152ULL);
    EXPECT_EQ(dailyUsage.connectionCount, 1);
  }
}

TEST_F(UsageTrackerTest, AlertsDisabled) {
  UsageTracker tracker;

  UsageAlert alert;
  alert.enabled = false;
  alert.dailyLimitBytes = 1000000;
  tracker.setAlertConfig(alert);

  // Record session exceeding limit
  QDateTime start = QDateTime::currentDateTime().addSecs(-3600);
  QDateTime end = QDateTime::currentDateTime();
  tracker.recordSession(start, end, 2000000, 2000000);

  auto status = tracker.checkAlerts();
  EXPECT_FALSE(status.exceeded);
}

TEST_F(UsageTrackerTest, DailyLimitWarning) {
  UsageTracker tracker;

  UsageAlert alert;
  alert.enabled = true;
  alert.dailyLimitBytes = 10000000;  // 10 MB
  alert.warningPercentage = 80;
  tracker.setAlertConfig(alert);

  // Record session at 85% of limit
  QDate today = QDate::currentDate();
  QDateTime start(today, QTime(10, 0, 0));
  QDateTime end(today, QTime(11, 0, 0));
  tracker.recordSession(start, end, 4500000, 4000000);  // 8.5 MB total

  auto status = tracker.checkAlerts();
  EXPECT_TRUE(status.exceeded);
  EXPECT_TRUE(status.isWarning);
  EXPECT_FALSE(status.message.isEmpty());
}

TEST_F(UsageTrackerTest, DailyLimitExceeded) {
  UsageTracker tracker;

  UsageAlert alert;
  alert.enabled = true;
  alert.dailyLimitBytes = 5000000;  // 5 MB
  tracker.setAlertConfig(alert);

  // Record session exceeding limit
  QDate today = QDate::currentDate();
  QDateTime start(today, QTime(10, 0, 0));
  QDateTime end(today, QTime(11, 0, 0));
  tracker.recordSession(start, end, 3000000, 3000000);  // 6 MB total

  auto status = tracker.checkAlerts();
  EXPECT_TRUE(status.exceeded);
  EXPECT_FALSE(status.isWarning);
  EXPECT_FALSE(status.message.isEmpty());
}

TEST_F(UsageTrackerTest, ExportToJson) {
  UsageTracker tracker;

  QDateTime start(QDate(2024, 1, 15), QTime(10, 0, 0));
  QDateTime end(QDate(2024, 1, 15), QTime(11, 0, 0));
  tracker.recordSession(start, end, 1048576, 2097152);

  QString json = tracker.exportToJson();
  EXPECT_FALSE(json.isEmpty());
  EXPECT_TRUE(json.contains("daily_usage"));
  EXPECT_TRUE(json.contains("monthly_usage"));
  EXPECT_TRUE(json.contains("2024-01-15"));
}

TEST_F(UsageTrackerTest, ExportToCsv) {
  UsageTracker tracker;

  QDateTime start(QDate(2024, 1, 15), QTime(10, 0, 0));
  QDateTime end(QDate(2024, 1, 15), QTime(11, 0, 0));
  tracker.recordSession(start, end, 1048576, 2097152);

  QString csv = tracker.exportDailyToCsv();
  EXPECT_FALSE(csv.isEmpty());
  EXPECT_TRUE(csv.contains("Date,TX Bytes,RX Bytes"));
  EXPECT_TRUE(csv.contains("2024-01-15"));
}

TEST_F(UsageTrackerTest, ClearOldData) {
  UsageTracker tracker;

  // Record old session (100 days ago)
  QDate oldDate = QDate::currentDate().addDays(-100);
  QDateTime oldStart(oldDate, QTime(10, 0, 0));
  QDateTime oldEnd(oldDate, QTime(11, 0, 0));
  tracker.recordSession(oldStart, oldEnd, 1000000, 2000000);

  // Record recent session
  QDate recentDate = QDate::currentDate().addDays(-1);
  QDateTime recentStart(recentDate, QTime(10, 0, 0));
  QDateTime recentEnd(recentDate, QTime(11, 0, 0));
  tracker.recordSession(recentStart, recentEnd, 500000, 1500000);

  // Clear data older than 90 days
  tracker.clearOldData(90);

  // Old data should be removed
  auto oldUsage = tracker.getDailyUsage(oldDate);
  EXPECT_EQ(oldUsage.totalBytes(), 0ULL);

  // Recent data should remain
  auto recentUsage = tracker.getDailyUsage(recentDate);
  EXPECT_GT(recentUsage.totalBytes(), 0ULL);
}

}  // namespace veil::gui

int main(int argc, char** argv) {
  // Initialize QCoreApplication for QSettings to work
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
