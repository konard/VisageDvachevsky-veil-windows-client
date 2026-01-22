#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>

namespace veil::gui {

/// Diagnostics widget showing protocol metrics, reassembly stats, and live logs
class DiagnosticsWidget : public QWidget {
  Q_OBJECT

 public:
  explicit DiagnosticsWidget(QWidget* parent = nullptr);

 signals:
  void backRequested();
  /// Emitted when diagnostics data should be requested from daemon
  void diagnosticsRequested();

 public slots:
  /// Update protocol metrics
  void updateProtocolMetrics(uint64_t seqCounter, uint64_t sendSeq, uint64_t recvSeq,
                              uint64_t packetsSent, uint64_t packetsReceived,
                              uint64_t packetsLost, uint64_t packetsRetransmitted);

  /// Update reassembly statistics
  void updateReassemblyStats(uint32_t fragmentsReceived, uint32_t messagesReassembled,
                              uint32_t fragmentsPending, uint32_t reassemblyTimeouts);

  /// Update obfuscation profile info
  void updateObfuscationProfile(bool paddingEnabled, uint32_t currentPaddingSize,
                                 const QString& timingJitter, const QString& heartbeatMode,
                                 double lastHeartbeatSec);

  /// Add a log entry
  void addLogEntry(const QString& timestamp, const QString& message,
                   const QString& level = "info");

  /// Clear log
  void clearLog();

  /// Set daemon connection state (starts/stops auto-refresh)
  void setDaemonConnected(bool connected);

 private slots:
  void onExportClicked();
  void onClearLogClicked();
  void onRequestDiagnostics();

 private:
  void setupUi();
  void createProtocolMetricsSection(QWidget* parent);
  void createReassemblySection(QWidget* parent);
  void createObfuscationSection(QWidget* parent);
  void createLogSection(QWidget* parent);

  QString formatNumber(uint64_t value) const;
  QString formatPercentage(uint64_t count, uint64_t total) const;

  // Protocol Metrics
  QLabel* seqCounterLabel_;
  QLabel* sendSeqLabel_;
  QLabel* recvSeqLabel_;
  QLabel* packetsSentLabel_;
  QLabel* packetsReceivedLabel_;
  QLabel* packetsLostLabel_;
  QLabel* packetsRetransmittedLabel_;

  // Reassembly Stats
  QLabel* fragmentsReceivedLabel_;
  QLabel* messagesReassembledLabel_;
  QLabel* fragmentsPendingLabel_;
  QLabel* reassemblyTimeoutsLabel_;

  // Obfuscation Profile
  QLabel* paddingEnabledLabel_;
  QLabel* currentPaddingSizeLabel_;
  QLabel* timingJitterLabel_;
  QLabel* heartbeatModeLabel_;
  QLabel* lastHeartbeatLabel_;

  // Log
  QTextEdit* logTextEdit_;
  QPushButton* clearLogButton_;
  QPushButton* exportButton_;

  // Refresh timer (requests data from daemon)
  QTimer* updateTimer_;
};

}  // namespace veil::gui
