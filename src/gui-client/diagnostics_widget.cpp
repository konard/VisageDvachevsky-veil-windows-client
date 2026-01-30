#include "diagnostics_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFile>
#include <QMessageBox>
#include <QRandomGenerator>

#include "common/gui/theme.h"

namespace veil::gui {

DiagnosticsWidget::DiagnosticsWidget(QWidget* parent) : QWidget(parent) {
  setupUi();

  // Timer for periodic diagnostics refresh (requests data from daemon)
  updateTimer_ = new QTimer(this);
  updateTimer_->setInterval(1000);
  connect(updateTimer_, &QTimer::timeout, this, &DiagnosticsWidget::onRequestDiagnostics);
  // Timer will be started when we're connected to the daemon

  // Initial log entry
  addLogEntry(QDateTime::currentDateTime().toString("hh:mm:ss"),
              "Diagnostics view opened", "info");
}

void DiagnosticsWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge(), spacing::kPaddingMedium(),
                                  spacing::kPaddingXLarge(), spacing::kPaddingMedium());

  // === Header ===
  auto* headerLayout = new QHBoxLayout();

  auto* backButton = new QPushButton("\u2190 Back", this);
  backButton->setCursor(Qt::PointingHandCursor);
  backButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      color: #58a6ff;
      font-size: 14px;
      font-weight: 500;
      padding: 8px 0;
    }
    QPushButton:hover {
      color: #79c0ff;
    }
  )");
  connect(backButton, &QPushButton::clicked, this, &DiagnosticsWidget::backRequested);
  headerLayout->addWidget(backButton);

  headerLayout->addStretch();
  mainLayout->addLayout(headerLayout);

  // Title
  auto* titleLabel = new QLabel("Diagnostics", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 700; color: #f0f6fc; margin-bottom: 8px;")
                                .arg(fonts::kFontSizeHeadline()));
  mainLayout->addWidget(titleLabel);

  // === Scrollable content ===
  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

  auto* scrollWidget = new QWidget();
  scrollWidget->setStyleSheet("background: transparent;");
  auto* scrollLayout = new QVBoxLayout(scrollWidget);
  scrollLayout->setSpacing(12);
  scrollLayout->setContentsMargins(0, 0, 12, 0);

  // Create sections
  createProtocolMetricsSection(scrollWidget);
  createReassemblySection(scrollWidget);
  createObfuscationSection(scrollWidget);
  createLogSection(scrollWidget);

  scrollArea->setWidget(scrollWidget);
  mainLayout->addWidget(scrollArea, 1);

  // === Footer ===
  auto* footerLayout = new QHBoxLayout();
  footerLayout->setSpacing(12);

  exportButton_ = new QPushButton("Export Diagnostics", this);
  exportButton_->setCursor(Qt::PointingHandCursor);
  exportButton_->setStyleSheet(R"(
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #1f6feb, stop:1 #58a6ff);
      border: none;
      border-radius: 12px;
      padding: 14px 28px;
      color: white;
      font-size: 15px;
      font-weight: 600;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #58a6ff, stop:1 #79c0ff);
    }
  )");
  connect(exportButton_, &QPushButton::clicked, this, &DiagnosticsWidget::onExportClicked);
  footerLayout->addWidget(exportButton_);

  mainLayout->addLayout(footerLayout);
}

void DiagnosticsWidget::createProtocolMetricsSection(QWidget* parent) {
  auto* group = new QGroupBox("Protocol Metrics", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  auto createMetricRow = [&](const QString& label, QLabel*& valueLabel, bool monospace = true) {
    auto* row = new QHBoxLayout();
    auto* labelWidget = new QLabel(label, group);
    labelWidget->setProperty("textStyle", "secondary");
    row->addWidget(labelWidget);
    row->addStretch();
    valueLabel = new QLabel("\u2014", group);
    if (monospace) {
      valueLabel->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 13px;");
    }
    row->addWidget(valueLabel);
    layout->addLayout(row);
  };

  createMetricRow("Sequence Counter", seqCounterLabel_);
  createMetricRow("Send Sequence", sendSeqLabel_);
  createMetricRow("Recv Sequence", recvSeqLabel_);

  // Separator
  auto* sep = new QFrame(group);
  sep->setFrameShape(QFrame::HLine);
  sep->setStyleSheet("background-color: rgba(255, 255, 255, 0.05);");
  sep->setFixedHeight(1);
  layout->addWidget(sep);

  createMetricRow("Packets Sent", packetsSentLabel_, false);
  createMetricRow("Packets Received", packetsReceivedLabel_, false);
  createMetricRow("Packets Lost", packetsLostLabel_, false);
  createMetricRow("Packets Retransmitted", packetsRetransmittedLabel_, false);

  parent->layout()->addWidget(group);
}

void DiagnosticsWidget::createReassemblySection(QWidget* parent) {
  auto* group = new QGroupBox("Reassembly Stats", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  auto createMetricRow = [&](const QString& label, QLabel*& valueLabel) {
    auto* row = new QHBoxLayout();
    auto* labelWidget = new QLabel(label, group);
    labelWidget->setProperty("textStyle", "secondary");
    row->addWidget(labelWidget);
    row->addStretch();
    valueLabel = new QLabel("\u2014", group);
    row->addWidget(valueLabel);
    layout->addLayout(row);
  };

  createMetricRow("Fragments Received", fragmentsReceivedLabel_);
  createMetricRow("Messages Reassembled", messagesReassembledLabel_);
  createMetricRow("Fragments Pending", fragmentsPendingLabel_);
  createMetricRow("Reassembly Timeouts", reassemblyTimeoutsLabel_);

  parent->layout()->addWidget(group);
}

void DiagnosticsWidget::createObfuscationSection(QWidget* parent) {
  auto* group = new QGroupBox("Obfuscation Profile", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  auto createMetricRow = [&](const QString& label, QLabel*& valueLabel) {
    auto* row = new QHBoxLayout();
    auto* labelWidget = new QLabel(label, group);
    labelWidget->setProperty("textStyle", "secondary");
    row->addWidget(labelWidget);
    row->addStretch();
    valueLabel = new QLabel("\u2014", group);
    row->addWidget(valueLabel);
    layout->addLayout(row);
  };

  createMetricRow("Padding Enabled", paddingEnabledLabel_);
  createMetricRow("Current Padding Size", currentPaddingSizeLabel_);
  createMetricRow("Timing Jitter", timingJitterLabel_);
  createMetricRow("Heartbeat Mode", heartbeatModeLabel_);
  createMetricRow("Last Heartbeat", lastHeartbeatLabel_);

  parent->layout()->addWidget(group);
}

void DiagnosticsWidget::createLogSection(QWidget* parent) {
  auto* group = new QGroupBox("Live Event Log", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  logTextEdit_ = new QTextEdit(group);
  logTextEdit_->setReadOnly(true);
  logTextEdit_->setMinimumHeight(200);
  logTextEdit_->setStyleSheet(R"(
    QTextEdit {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      padding: 16px;
      color: #f0f6fc;
      font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', 'Consolas', monospace;
      font-size: 12px;
      line-height: 1.5;
    }
  )");
  layout->addWidget(logTextEdit_);

  clearLogButton_ = new QPushButton("Clear Log", group);
  clearLogButton_->setCursor(Qt::PointingHandCursor);
  clearLogButton_->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 8px;
      color: #8b949e;
      padding: 10px 20px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.04);
      border-color: rgba(255, 255, 255, 0.2);
      color: #f0f6fc;
    }
  )");
  connect(clearLogButton_, &QPushButton::clicked, this, &DiagnosticsWidget::onClearLogClicked);

  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(clearLogButton_);
  layout->addLayout(buttonLayout);

  parent->layout()->addWidget(group);
}

void DiagnosticsWidget::updateProtocolMetrics(uint64_t seqCounter, uint64_t sendSeq, uint64_t recvSeq,
                                               uint64_t packetsSent, uint64_t packetsReceived,
                                               uint64_t packetsLost, uint64_t packetsRetransmitted) {
  seqCounterLabel_->setText(QString("0x%1").arg(seqCounter, 16, 16, QChar('0')));
  sendSeqLabel_->setText(QString("0x%1").arg(sendSeq, 16, 16, QChar('0')));
  recvSeqLabel_->setText(QString("0x%1").arg(recvSeq, 16, 16, QChar('0')));

  packetsSentLabel_->setText(formatNumber(packetsSent));
  packetsReceivedLabel_->setText(formatNumber(packetsReceived));

  // Color-code loss rate
  QString lossColor = colors::dark::kTextPrimary;
  if (packetsReceived > 0) {
    double lossRate = static_cast<double>(packetsLost) / static_cast<double>(packetsReceived) * 100.0;
    if (lossRate < 1.0) {
      lossColor = colors::dark::kAccentSuccess;
    } else if (lossRate < 5.0) {
      lossColor = colors::dark::kAccentWarning;
    } else {
      lossColor = colors::dark::kAccentError;
    }
  }
  packetsLostLabel_->setText(QString("%1 %2").arg(formatNumber(packetsLost),
                                                    formatPercentage(packetsLost, packetsReceived)));
  packetsLostLabel_->setStyleSheet(QString("color: %1;").arg(lossColor));

  packetsRetransmittedLabel_->setText(QString("%1 %2").arg(formatNumber(packetsRetransmitted),
                                                            formatPercentage(packetsRetransmitted, packetsSent)));
}

void DiagnosticsWidget::updateReassemblyStats(uint32_t fragmentsReceived, uint32_t messagesReassembled,
                                               uint32_t fragmentsPending, uint32_t reassemblyTimeouts) {
  fragmentsReceivedLabel_->setText(formatNumber(fragmentsReceived));
  messagesReassembledLabel_->setText(formatNumber(messagesReassembled));
  fragmentsPendingLabel_->setText(formatNumber(fragmentsPending));

  // Color-code timeouts
  QString timeoutColor = colors::dark::kTextPrimary;
  if (reassemblyTimeouts == 0) {
    timeoutColor = colors::dark::kAccentSuccess;
  } else if (reassemblyTimeouts < 5) {
    timeoutColor = colors::dark::kAccentWarning;
  } else {
    timeoutColor = colors::dark::kAccentError;
  }
  reassemblyTimeoutsLabel_->setText(QString::number(reassemblyTimeouts));
  reassemblyTimeoutsLabel_->setStyleSheet(QString("color: %1;").arg(timeoutColor));
}

void DiagnosticsWidget::updateObfuscationProfile(bool paddingEnabled, uint32_t currentPaddingSize,
                                                  const QString& timingJitter, const QString& heartbeatMode,
                                                  double lastHeartbeatSec) {
  paddingEnabledLabel_->setText(paddingEnabled ? "Yes" : "No");
  paddingEnabledLabel_->setStyleSheet(QString("color: %1;")
                                          .arg(paddingEnabled ? colors::dark::kAccentSuccess
                                                              : colors::dark::kTextSecondary));

  currentPaddingSizeLabel_->setText(QString("%1 bytes").arg(currentPaddingSize));
  timingJitterLabel_->setText(timingJitter);
  heartbeatModeLabel_->setText(heartbeatMode);
  lastHeartbeatLabel_->setText(QString("%1s ago").arg(lastHeartbeatSec, 0, 'f', 1));
}

void DiagnosticsWidget::addLogEntry(const QString& timestamp, const QString& message,
                                     const QString& level) {
  QString color = colors::dark::kTextPrimary;
  if (level == "success") {
    color = colors::dark::kAccentSuccess;
  } else if (level == "warning") {
    color = colors::dark::kAccentWarning;
  } else if (level == "error") {
    color = colors::dark::kAccentError;
  } else if (level == "debug") {
    color = colors::dark::kTextSecondary;
  }

  QString html = QString("<span style='color: %1;'>[%2]</span> <span style='color: %3;'>%4</span><br>")
                     .arg(colors::dark::kTextSecondary, timestamp, color, message.toHtmlEscaped());

  logTextEdit_->moveCursor(QTextCursor::End);
  logTextEdit_->insertHtml(html);
  logTextEdit_->moveCursor(QTextCursor::End);

  // Limit log entries to prevent memory issues
  if (logTextEdit_->document()->blockCount() > 500) {
    QTextCursor cursor = logTextEdit_->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 100);
    cursor.removeSelectedText();
  }
}

void DiagnosticsWidget::clearLog() {
  logTextEdit_->clear();
}

void DiagnosticsWidget::onExportClicked() {
  QString fileName = QFileDialog::getSaveFileName(this, tr("Export Diagnostics"),
                                                   "veil-diagnostics.json",
                                                   tr("JSON Files (*.json)"));
  if (fileName.isEmpty()) {
    return;
  }

  QJsonObject diagnostics;

  // Protocol metrics
  QJsonObject protocol;
  protocol["sequence_counter"] = seqCounterLabel_->text();
  protocol["send_sequence"] = sendSeqLabel_->text();
  protocol["recv_sequence"] = recvSeqLabel_->text();
  protocol["packets_sent"] = packetsSentLabel_->text();
  protocol["packets_received"] = packetsReceivedLabel_->text();
  protocol["packets_lost"] = packetsLostLabel_->text();
  protocol["packets_retransmitted"] = packetsRetransmittedLabel_->text();
  diagnostics["protocol_metrics"] = protocol;

  // Reassembly stats
  QJsonObject reassembly;
  reassembly["fragments_received"] = fragmentsReceivedLabel_->text();
  reassembly["messages_reassembled"] = messagesReassembledLabel_->text();
  reassembly["fragments_pending"] = fragmentsPendingLabel_->text();
  reassembly["reassembly_timeouts"] = reassemblyTimeoutsLabel_->text();
  diagnostics["reassembly_stats"] = reassembly;

  // Obfuscation profile
  QJsonObject obfuscation;
  obfuscation["padding_enabled"] = paddingEnabledLabel_->text();
  obfuscation["current_padding_size"] = currentPaddingSizeLabel_->text();
  obfuscation["timing_jitter"] = timingJitterLabel_->text();
  obfuscation["heartbeat_mode"] = heartbeatModeLabel_->text();
  obfuscation["last_heartbeat"] = lastHeartbeatLabel_->text();
  diagnostics["obfuscation_profile"] = obfuscation;

  // Event log
  diagnostics["event_log"] = logTextEdit_->toPlainText();

  // System info
  QJsonObject system;
  system["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  system["version"] = "0.1.0";
  diagnostics["system_info"] = system;

  QJsonDocument doc(diagnostics);
  QFile file(fileName);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    QMessageBox::information(this, tr("Export Successful"),
                             tr("Diagnostics exported to:\n%1").arg(fileName));
  } else {
    QMessageBox::warning(this, tr("Export Failed"),
                         tr("Could not write to file:\n%1").arg(fileName));
  }
}

void DiagnosticsWidget::onClearLogClicked() {
  clearLog();
  addLogEntry(QDateTime::currentDateTime().toString("hh:mm:ss"),
              "Log cleared", "info");
}

void DiagnosticsWidget::onRequestDiagnostics() {
  // This slot is called periodically to request updated diagnostics from daemon
  // The actual request is sent via IPC by the main window / IPC manager
  emit diagnosticsRequested();
}

void DiagnosticsWidget::setDaemonConnected(bool connected) {
  if (connected) {
    updateTimer_->start();
    addLogEntry(QDateTime::currentDateTime().toString("hh:mm:ss"),
                "Connected to daemon", "success");
  } else {
    updateTimer_->stop();
    addLogEntry(QDateTime::currentDateTime().toString("hh:mm:ss"),
                "Disconnected from daemon", "warning");
  }
}

QString DiagnosticsWidget::formatNumber(uint64_t value) const {
  return QLocale().toString(static_cast<qulonglong>(value));
}

QString DiagnosticsWidget::formatPercentage(uint64_t count, uint64_t total) const {
  if (total == 0) return "(0.00%)";
  double pct = static_cast<double>(count) / static_cast<double>(total) * 100.0;
  return QString("(%1%)").arg(pct, 0, 'f', 2);
}

}  // namespace veil::gui
