#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>

#include <deque>
#include <vector>

namespace veil::gui {

/// A single data point for time-series graphs
struct StatsDataPoint {
  qint64 timestampMs{0};   ///< Milliseconds since epoch
  double value{0.0};       ///< Metric value
};

/// Record of a completed connection session
struct ConnectionRecord {
  QDateTime startTime;
  QDateTime endTime;
  QString serverAddress;
  uint16_t serverPort{0};
  uint64_t totalTxBytes{0};
  uint64_t totalRxBytes{0};
};

/// Custom widget for painting a simple line graph
class MiniGraphWidget : public QWidget {
  Q_OBJECT

 public:
  explicit MiniGraphWidget(QWidget* parent = nullptr);

  /// Set graph title and unit label
  void setLabels(const QString& title, const QString& unit);

  /// Set the line color for the graph
  void setLineColor(const QColor& color);

  /// Set a second series (e.g., upload vs download)
  void setDualSeries(bool dual);
  void setSecondLineColor(const QColor& color);

  /// Add a data point to primary series
  void addDataPoint(double value);

  /// Add a data point to secondary series (only when dual)
  void addSecondDataPoint(double value);

  /// Maximum number of data points to retain (default 300 = 5 min at 1/sec)
  void setMaxPoints(int max);

  /// Clear all data
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString title_;
  QString unit_;
  QColor lineColor_{88, 166, 255};       // kAccentPrimary
  QColor secondLineColor_{63, 185, 80};  // kAccentSuccess
  bool dualSeries_{false};
  int maxPoints_{300};
  std::deque<double> data_;
  std::deque<double> secondData_;
};

/// Widget for displaying connection statistics history and graphs
class StatisticsWidget : public QWidget {
  Q_OBJECT

 public:
  explicit StatisticsWidget(QWidget* parent = nullptr);

 signals:
  void backRequested();

 public slots:
  /// Record a new bandwidth data point (called every second while connected)
  void recordBandwidth(uint64_t txBytesPerSec, uint64_t rxBytesPerSec);

  /// Record a new latency data point (called every second while connected)
  void recordLatency(int latencyMs);

  /// Called when a new connection session starts
  void onSessionStarted(const QString& server, uint16_t port);

  /// Called when a connection session ends
  void onSessionEnded(uint64_t totalTx, uint64_t totalRx);

 private slots:
  void onExportClicked();
  void onClearHistoryClicked();

 private:
  void setupUi();
  void createBandwidthGraphSection(QWidget* parent);
  void createLatencyGraphSection(QWidget* parent);
  void createConnectionHistorySection(QWidget* parent);
  void updateHistoryDisplay();

  QString formatBytes(uint64_t bytes) const;
  QString formatDuration(qint64 seconds) const;

  // Graphs
  MiniGraphWidget* bandwidthGraph_;
  MiniGraphWidget* latencyGraph_;

  // Connection history
  QWidget* historyContainer_;
  QLabel* noHistoryLabel_;
  std::deque<ConnectionRecord> connectionHistory_;  // max 10

  // Current session tracking
  bool sessionActive_{false};
  QDateTime currentSessionStart_;
  QString currentServer_;
  uint16_t currentPort_{0};

  // Export / controls
  QPushButton* exportButton_;
  QPushButton* clearButton_;

  static constexpr int kMaxHistoryEntries = 10;
};

}  // namespace veil::gui
