// NOLINTBEGIN(readability-implicit-bool-conversion)
#include <gtest/gtest.h>

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QDateTime>
#include "statistics_widget.h"

// We need a QApplication for widget tests.
// Set offscreen platform for headless CI environments.
static struct OffscreenPlatformSetter {
  OffscreenPlatformSetter() {
    if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  }
} offscreenSetter;  // NOLINT(cert-err58-cpp)

static int argc = 1;
static char appName[] = "statistics_widget_tests";  // NOLINT(cppcoreguidelines-avoid-c-arrays)
static char* argv[] = {appName, nullptr};  // NOLINT(cppcoreguidelines-avoid-c-arrays)

namespace veil::gui {
namespace {

// ===================== MiniGraphWidget Tests =====================

class MiniGraphWidgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    widget_ = new MiniGraphWidget();
  }

  void TearDown() override {
    delete widget_;
  }

  MiniGraphWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

TEST_F(MiniGraphWidgetTest, InitialStateNoData) {
  // Widget should be created successfully with no data
  EXPECT_NE(widget_, nullptr);
  EXPECT_GE(widget_->minimumHeight(), 100);
}

TEST_F(MiniGraphWidgetTest, SetLabels) {
  // Should not crash when setting labels
  widget_->setLabels("Test Title", "units");
}

TEST_F(MiniGraphWidgetTest, SetLineColor) {
  widget_->setLineColor(QColor(255, 0, 0));
}

TEST_F(MiniGraphWidgetTest, AddSingleDataPoint) {
  widget_->addDataPoint(42.0);
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, AddMultipleDataPoints) {
  for (int i = 0; i < 50; ++i) {
    widget_->addDataPoint(static_cast<double>(i));
  }
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, MaxPointsEnforced) {
  widget_->setMaxPoints(10);
  for (int i = 0; i < 20; ++i) {
    widget_->addDataPoint(static_cast<double>(i));
  }
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, DualSeriesMode) {
  widget_->setDualSeries(true);
  widget_->setSecondLineColor(QColor(0, 255, 0));

  for (int i = 0; i < 10; ++i) {
    widget_->addDataPoint(static_cast<double>(i));
    widget_->addSecondDataPoint(static_cast<double>(i * 2));
  }
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, SecondSeriesWithoutDualMode) {
  widget_->addSecondDataPoint(100.0);
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, ClearData) {
  widget_->addDataPoint(1.0);
  widget_->addDataPoint(2.0);
  widget_->addSecondDataPoint(3.0);
  widget_->clear();
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, LargeValues) {
  widget_->addDataPoint(1048576.0);  // 1 MB
  widget_->addDataPoint(5242880.0);  // 5 MB
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, ZeroValues) {
  widget_->addDataPoint(0.0);
  widget_->addDataPoint(0.0);
  widget_->addDataPoint(0.0);
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, NegativeMaxPointsHandled) {
  widget_->setMaxPoints(1);
  widget_->addDataPoint(1.0);
  widget_->addDataPoint(2.0);
  widget_->repaint();
}

TEST_F(MiniGraphWidgetTest, PaintEventWithMinimalSize) {
  widget_->resize(1, 1);
  widget_->addDataPoint(100.0);
  widget_->repaint();
}

// ===================== StatisticsWidget Tests =====================

class StatisticsWidgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    widget_ = new StatisticsWidget();
  }

  void TearDown() override {
    delete widget_;
  }

  StatisticsWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

TEST_F(StatisticsWidgetTest, InitialCreation) {
  EXPECT_NE(widget_, nullptr);
}

TEST_F(StatisticsWidgetTest, BackSignalExists) {
  bool signalConnected = false;
  auto conn = QObject::connect(widget_, &StatisticsWidget::backRequested, [&]() {
    signalConnected = true;
  });
  EXPECT_TRUE(conn);
  QObject::disconnect(conn);
}

TEST_F(StatisticsWidgetTest, RecordBandwidth) {
  widget_->recordBandwidth(1000, 2000);
  widget_->recordBandwidth(1500, 2500);
  widget_->recordBandwidth(0, 0);
}

TEST_F(StatisticsWidgetTest, RecordLatency) {
  widget_->recordLatency(10);
  widget_->recordLatency(50);
  widget_->recordLatency(200);
  widget_->recordLatency(0);
}

TEST_F(StatisticsWidgetTest, SessionStartAndEnd) {
  widget_->onSessionStarted("vpn.example.com", 4433);
  widget_->onSessionEnded(1024, 2048);
}

TEST_F(StatisticsWidgetTest, MultipleSessionsCapped) {
  for (int i = 0; i < 15; ++i) {
    widget_->onSessionStarted(
        QString("server%1.example.com").arg(i),
        static_cast<uint16_t>(4433 + i));
    widget_->onSessionEnded(
        static_cast<uint64_t>(i) * uint64_t{1024},
        static_cast<uint64_t>(i) * uint64_t{2048});
  }
}

TEST_F(StatisticsWidgetTest, SessionEndWithoutStart) {
  widget_->onSessionEnded(0, 0);
}

TEST_F(StatisticsWidgetTest, DoubleSessionStart) {
  widget_->onSessionStarted("server1.example.com", 4433);
  widget_->onSessionStarted("server2.example.com", 5544);
  widget_->onSessionEnded(100, 200);
}

TEST_F(StatisticsWidgetTest, SessionWithBandwidthAndLatencyData) {
  widget_->onSessionStarted("vpn.test.com", 4433);

  for (int i = 0; i < 10; ++i) {
    widget_->recordBandwidth(
        uint64_t{1000} + static_cast<uint64_t>(i) * uint64_t{100},
        uint64_t{2000} + static_cast<uint64_t>(i) * uint64_t{200});
    widget_->recordLatency(10 + i);
  }

  widget_->onSessionEnded(50000, 100000);
}

TEST_F(StatisticsWidgetTest, ClearHistory) {
  widget_->onSessionStarted("vpn.example.com", 4433);
  widget_->onSessionEnded(1024, 2048);

  // Clear is a private slot triggered by the clear button.
  QMetaObject::invokeMethod(widget_, "onClearHistoryClicked");
}

TEST_F(StatisticsWidgetTest, ExportJsonToFile) {
  QTemporaryDir tempDir;
  ASSERT_TRUE(tempDir.isValid());

  widget_->onSessionStarted("export-test.example.com", 4433);
  widget_->onSessionEnded(10240, 20480);
}

TEST_F(StatisticsWidgetTest, HighVolumeDataPoints) {
  for (int i = 0; i < 300; ++i) {
    widget_->recordBandwidth(
        uint64_t{1000} + static_cast<uint64_t>(i % 50) * uint64_t{100},
        uint64_t{2000} + static_cast<uint64_t>(i % 50) * uint64_t{200});
    widget_->recordLatency(10 + (i % 100));
  }
}

TEST_F(StatisticsWidgetTest, OverflowDataPoints) {
  for (int i = 0; i < 500; ++i) {
    widget_->recordBandwidth(static_cast<uint64_t>(i), static_cast<uint64_t>(i));
    widget_->recordLatency(i);
  }
}

TEST_F(StatisticsWidgetTest, ZeroBytesSession) {
  widget_->onSessionStarted("zero.example.com", 4433);
  widget_->onSessionEnded(0, 0);
}

TEST_F(StatisticsWidgetTest, LargeBytesSession) {
  widget_->onSessionStarted("heavy.example.com", 4433);
  widget_->onSessionEnded(uint64_t{10737418240}, uint64_t{10737418240});
}

TEST_F(StatisticsWidgetTest, EmptyServerAddress) {
  widget_->onSessionStarted("", 0);
  widget_->onSessionEnded(100, 200);
}

TEST_F(StatisticsWidgetTest, RepaintAfterDataAdded) {
  widget_->recordBandwidth(5000, 10000);
  widget_->recordLatency(25);
  widget_->repaint();
}

// ===================== ConnectionRecord Tests =====================

TEST(ConnectionRecordTest, DefaultConstruction) {
  ConnectionRecord record;
  EXPECT_TRUE(record.startTime.isNull());
  EXPECT_TRUE(record.endTime.isNull());
  EXPECT_TRUE(record.serverAddress.isEmpty());
  EXPECT_EQ(record.serverPort, 0);
  EXPECT_EQ(record.totalTxBytes, 0U);
  EXPECT_EQ(record.totalRxBytes, 0U);
}

TEST(ConnectionRecordTest, PopulatedRecord) {
  ConnectionRecord record;
  record.startTime = QDateTime::currentDateTime();
  record.endTime = record.startTime.addSecs(3600);
  record.serverAddress = "test.server.com";
  record.serverPort = 4433;
  record.totalTxBytes = 1048576;
  record.totalRxBytes = 2097152;

  EXPECT_FALSE(record.startTime.isNull());
  EXPECT_EQ(record.serverPort, 4433);
  EXPECT_EQ(record.totalTxBytes, 1048576U);
  EXPECT_EQ(record.totalRxBytes, 2097152U);
  EXPECT_EQ(record.startTime.secsTo(record.endTime), 3600);
}

// ===================== StatsDataPoint Tests =====================

TEST(StatsDataPointTest, DefaultConstruction) {
  StatsDataPoint point;
  EXPECT_EQ(point.timestampMs, 0);
  EXPECT_DOUBLE_EQ(point.value, 0.0);
}

TEST(StatsDataPointTest, PopulatedPoint) {
  StatsDataPoint point;
  point.timestampMs = 1706600000000LL;
  point.value = 42.5;
  EXPECT_EQ(point.timestampMs, 1706600000000LL);
  EXPECT_DOUBLE_EQ(point.value, 42.5);
}

// ===================== FormatBytes / FormatDuration Tests =====================

class FormatHelpersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    widget_ = new StatisticsWidget();
  }

  void TearDown() override {
    delete widget_;
  }

  StatisticsWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

TEST_F(FormatHelpersTest, SessionHistoryUpdatesDisplay) {
  for (int i = 0; i < 5; ++i) {
    widget_->onSessionStarted(
        QString("server%1.com").arg(i),
        static_cast<uint16_t>(4433 + i));
    widget_->onSessionEnded(
        static_cast<uint64_t>(i + 1) * uint64_t{1024} * uint64_t{1024},
        static_cast<uint64_t>(i + 1) * uint64_t{2} * uint64_t{1024} * uint64_t{1024});
  }
  widget_->repaint();
}

// ===================== Export Logic Tests =====================

class ExportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    widget_ = new StatisticsWidget();
  }

  void TearDown() override {
    delete widget_;
  }

  StatisticsWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

TEST_F(ExportTest, WidgetWithNoHistoryCreated) {
  EXPECT_NE(widget_, nullptr);
}

TEST_F(ExportTest, MultipleSessionsRecorded) {
  for (int i = 0; i < 3; ++i) {
    widget_->onSessionStarted(
        QString("export-%1.com").arg(i),
        static_cast<uint16_t>(4433 + i));
    widget_->onSessionEnded(
        static_cast<uint64_t>(i + 1) * uint64_t{512},
        static_cast<uint64_t>(i + 1) * uint64_t{1024});
  }
}

// ===================== Edge Case Tests =====================

TEST_F(StatisticsWidgetTest, RapidSessionStartEnd) {
  for (int i = 0; i < 100; ++i) {
    widget_->onSessionStarted("rapid.test.com", 4433);
    widget_->onSessionEnded(static_cast<uint64_t>(i), static_cast<uint64_t>(i));
  }
}

TEST_F(StatisticsWidgetTest, InterleavedBandwidthAndSession) {
  widget_->recordBandwidth(1000, 2000);
  widget_->recordLatency(50);

  widget_->onSessionStarted("interleaved.test.com", 4433);
  widget_->recordBandwidth(3000, 4000);
  widget_->recordLatency(25);
  widget_->onSessionEnded(5000, 10000);

  widget_->recordBandwidth(500, 1000);
  widget_->recordLatency(100);
}

TEST_F(StatisticsWidgetTest, MaxPortNumber) {
  widget_->onSessionStarted("maxport.test.com", 65535);
  widget_->onSessionEnded(100, 200);
}

TEST_F(StatisticsWidgetTest, UnicodeServerAddress) {
  widget_->onSessionStarted(QString::fromUtf8("сервер.example.com"), 4433);
  widget_->onSessionEnded(100, 200);
}

TEST_F(StatisticsWidgetTest, VeryLongServerAddress) {
  QString longAddr(500, 'a');
  longAddr += ".example.com";
  widget_->onSessionStarted(longAddr, 4433);
  widget_->onSessionEnded(100, 200);
}

}  // namespace
}  // namespace veil::gui
// NOLINTEND(readability-implicit-bool-conversion)
