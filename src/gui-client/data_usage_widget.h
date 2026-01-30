#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>

#include <deque>
#include <vector>

#include "usage_tracker.h"

namespace veil::gui {

/// Custom widget for painting a bar chart of usage data
class UsageBarChart : public QWidget {
  Q_OBJECT

 public:
  explicit UsageBarChart(QWidget* parent = nullptr);

  /// Set bar data: each entry is {label, txBytes, rxBytes}
  struct BarData {
    QString label;
    uint64_t txBytes{0};
    uint64_t rxBytes{0};
    uint64_t totalBytes() const { return txBytes + rxBytes; }
  };

  void setData(const std::vector<BarData>& data);
  void setTitle(const QString& title);
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString title_;
  std::vector<BarData> data_;

  QString formatBytes(uint64_t bytes) const;
};

/// Widget for displaying data usage tracking and statistics
class DataUsageWidget : public QWidget {
  Q_OBJECT

 public:
  explicit DataUsageWidget(UsageTracker* tracker, QWidget* parent = nullptr);

 signals:
  void backRequested();

 public slots:
  /// Refresh the display with latest data
  void refresh();

 private slots:
  void onPeriodChanged(int index);
  void onExportClicked();
  void onClearHistoryClicked();
  void onAlertSettingsChanged();

 private:
  void setupUi();
  void createSummarySection(QWidget* parent);
  void createChartSection(QWidget* parent);
  void createAlertSection(QWidget* parent);
  void updateSummary();
  void updateChart();

  QString formatBytes(uint64_t bytes) const;
  QString formatDuration(uint64_t seconds) const;

  UsageTracker* tracker_;

  // Summary labels
  QLabel* todayUploadLabel_;
  QLabel* todayDownloadLabel_;
  QLabel* todayTotalLabel_;
  QLabel* monthUploadLabel_;
  QLabel* monthDownloadLabel_;
  QLabel* monthTotalLabel_;
  QLabel* monthSessionsLabel_;
  QLabel* monthDurationLabel_;

  // Chart
  UsageBarChart* usageChart_;
  QComboBox* periodCombo_;

  // Alert controls
  QCheckBox* alertEnabledCheck_;
  QSpinBox* warningThresholdSpin_;
  QComboBox* warningUnitCombo_;
  QSpinBox* limitThresholdSpin_;
  QComboBox* limitUnitCombo_;
  QCheckBox* autoDisconnectCheck_;

  // Buttons
  QPushButton* exportButton_;
  QPushButton* clearButton_;
};

}  // namespace veil::gui
