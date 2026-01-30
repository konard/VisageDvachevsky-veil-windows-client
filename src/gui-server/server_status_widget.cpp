#include "server_status_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <cmath>

#include "common/gui/theme.h"

namespace veil::gui {

ServerStatusWidget::ServerStatusWidget(QWidget* parent) : QWidget(parent) {
  setupUi();

  // Setup timers
  pulseTimer_ = new QTimer(this);
  connect(pulseTimer_, &QTimer::timeout, this, &ServerStatusWidget::updatePulseAnimation);

  uptimeTimer_ = new QTimer(this);
  connect(uptimeTimer_, &QTimer::timeout, this, &ServerStatusWidget::updateUptime);

  demoTimer_ = new QTimer(this);
  connect(demoTimer_, &QTimer::timeout, this, &ServerStatusWidget::simulateDemoData);

  // Start in demo mode - simulate server starting
  QTimer::singleShot(500, this, [this]() {
    setServerState(ServerState::kStarting);
    QTimer::singleShot(2000, this, [this]() {
      setServerState(ServerState::kRunning);
      demoTimer_->start(1000);
    });
  });
}

void ServerStatusWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // === Status Card ===
  auto* statusCard = new QFrame(this);
  statusCard->setStyleSheet(QString(R"(
    QFrame {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(30, 35, 45, 0.95),
        stop:1 rgba(25, 30, 40, 0.95));
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: %1px;
    }
  )").arg(spacing::kBorderRadiusLarge()));

  auto* cardLayout = new QVBoxLayout(statusCard);
  cardLayout->setSpacing(20);
  cardLayout->setContentsMargins(spacing::kPaddingLarge(), spacing::kPaddingLarge(),
                                  spacing::kPaddingLarge(), spacing::kPaddingLarge());

  // === Status Header Row ===
  auto* headerRow = new QHBoxLayout();

  // Status indicator (pulsing dot)
  statusIndicator_ = new QWidget(this);
  statusIndicator_->setFixedSize(scaleDpi(16), scaleDpi(16));
  statusIndicator_->setStyleSheet(QString(R"(
    background: %1;
    border-radius: 8px;
  )").arg(colors::dark::kTextSecondary));
  indicatorOpacity_ = new QGraphicsOpacityEffect(statusIndicator_);
  statusIndicator_->setGraphicsEffect(indicatorOpacity_);
  headerRow->addWidget(statusIndicator_);

  statusLabel_ = new QLabel("Stopped", this);
  statusLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 600;")
                                  .arg(fonts::kFontSizeTitle()));
  headerRow->addWidget(statusLabel_);

  headerRow->addStretch();

  // Start/Stop button
  startStopButton_ = new QPushButton("Start Server", this);
  startStopButton_->setCursor(Qt::PointingHandCursor);
  startStopButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 %1, stop:1 %2);
      border: none;
      border-radius: %3px;
      color: white;
      font-weight: 600;
      padding: 12px 24px;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 %2, stop:1 %1);
    }
    QPushButton:pressed {
      background: %1;
    }
  )").arg(colors::dark::kAccentSuccess, colors::dark::kAccentPrimary)
    .arg(spacing::kBorderRadiusMedium()));
  connect(startStopButton_, &QPushButton::clicked, this, &ServerStatusWidget::onStartStopClicked);
  headerRow->addWidget(startStopButton_);

  cardLayout->addLayout(headerRow);

  // === Separator ===
  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("background: rgba(255, 255, 255, 0.1); max-height: 1px;");
  cardLayout->addWidget(separator);

  // === Metrics Grid ===
  auto* metricsGrid = new QHBoxLayout();
  metricsGrid->setSpacing(spacing::kPaddingLarge());

  // Listen Address
  auto* listenBox = new QVBoxLayout();
  auto* listenTitle = new QLabel("Listen Address", this);
  listenTitle->setProperty("textStyle", "secondary");
  listenTitle->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kTextSecondary));
  listenBox->addWidget(listenTitle);

  listenAddressLabel_ = new QLabel("0.0.0.0:4433", this);
  listenAddressLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 500;")
                                         .arg(fonts::kFontSizeBody()));
  listenBox->addWidget(listenAddressLabel_);
  metricsGrid->addLayout(listenBox);

  metricsGrid->addStretch();

  // Active Clients
  auto* clientsBox = new QVBoxLayout();
  auto* clientsTitle = new QLabel("Active Clients", this);
  clientsTitle->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kTextSecondary));
  clientsBox->addWidget(clientsTitle);

  auto* clientsRow = new QHBoxLayout();
  activeClientsLabel_ = new QLabel("0", this);
  activeClientsLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 600; color: %2;")
                                         .arg(fonts::kFontSizeTitle())
                                         .arg(colors::dark::kAccentPrimary));
  clientsRow->addWidget(activeClientsLabel_);

  maxClientsLabel_ = new QLabel("/ 100", this);
  maxClientsLabel_->setStyleSheet(QString("font-size: %1px; color: %2;")
                                      .arg(fonts::kFontSizeCaption())
                                      .arg(colors::dark::kTextSecondary));
  clientsRow->addWidget(maxClientsLabel_);
  clientsRow->addStretch();
  clientsBox->addLayout(clientsRow);
  metricsGrid->addLayout(clientsBox);

  metricsGrid->addStretch();

  // Uptime
  auto* uptimeBox = new QVBoxLayout();
  auto* uptimeTitle = new QLabel("Uptime", this);
  uptimeTitle->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kTextSecondary));
  uptimeBox->addWidget(uptimeTitle);

  uptimeLabel_ = new QLabel("--:--:--", this);
  uptimeLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 500;")
                                  .arg(fonts::kFontSizeBody()));
  uptimeBox->addWidget(uptimeLabel_);
  metricsGrid->addLayout(uptimeBox);

  cardLayout->addLayout(metricsGrid);

  mainLayout->addWidget(statusCard);

  // === Traffic Statistics Card ===
  auto* trafficCard = new QFrame(this);
  trafficCard->setStyleSheet(QString(R"(
    QFrame {
      background: rgba(30, 35, 45, 0.8);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: %1px;
    }
  )").arg(spacing::kBorderRadiusMedium()));

  auto* trafficLayout = new QVBoxLayout(trafficCard);
  trafficLayout->setContentsMargins(spacing::kPaddingMedium(), spacing::kPaddingMedium(),
                                     spacing::kPaddingMedium(), spacing::kPaddingMedium());

  auto* trafficTitle = new QLabel("Traffic Statistics", this);
  trafficTitle->setStyleSheet(QString("font-size: %1px; font-weight: 600; margin-bottom: 8px;")
                                  .arg(fonts::kFontSizeBody()));
  trafficLayout->addWidget(trafficTitle);

  auto* trafficRow = new QHBoxLayout();
  trafficRow->setSpacing(spacing::kPaddingXLarge());

  // Bytes Sent
  auto* sentBox = new QVBoxLayout();
  auto* sentTitle = new QLabel("Total Sent", this);
  sentTitle->setStyleSheet(QString("color: %1; font-size: 11px;").arg(colors::dark::kTextSecondary));
  sentBox->addWidget(sentTitle);

  bytesSentLabel_ = new QLabel("0 B", this);
  bytesSentLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 500; color: %2;")
                                     .arg(fonts::kFontSizeBody())
                                     .arg(colors::dark::kAccentSuccess));
  sentBox->addWidget(bytesSentLabel_);
  trafficRow->addLayout(sentBox);

  // Bytes Received
  auto* recvBox = new QVBoxLayout();
  auto* recvTitle = new QLabel("Total Received", this);
  recvTitle->setStyleSheet(QString("color: %1; font-size: 11px;").arg(colors::dark::kTextSecondary));
  recvBox->addWidget(recvTitle);

  bytesReceivedLabel_ = new QLabel("0 B", this);
  bytesReceivedLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 500; color: %2;")
                                         .arg(fonts::kFontSizeBody())
                                         .arg(colors::dark::kAccentPrimary));
  recvBox->addWidget(bytesReceivedLabel_);
  trafficRow->addLayout(recvBox);

  trafficRow->addStretch();
  trafficLayout->addLayout(trafficRow);

  mainLayout->addWidget(trafficCard);

  mainLayout->addStretch();
}

void ServerStatusWidget::setServerState(ServerState state) {
  state_ = state;
  updateStatusIndicator();

  switch (state) {
    case ServerState::kStopped:
      statusLabel_->setText("Stopped");
      startStopButton_->setText("Start Server");
      startStopButton_->setEnabled(true);
      startStopButton_->setStyleSheet(QString(R"(
        QPushButton {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
            stop:0 %1, stop:1 %2);
          border: none;
          border-radius: %3px;
          color: white;
          font-weight: 600;
          padding: 12px 24px;
        }
        QPushButton:hover {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
            stop:0 %2, stop:1 %1);
        }
      )").arg(colors::dark::kAccentSuccess, colors::dark::kAccentPrimary)
        .arg(spacing::kBorderRadiusMedium()));
      pulseTimer_->stop();
      uptimeTimer_->stop();
      uptimeLabel_->setText("--:--:--");
      break;

    case ServerState::kStarting:
      statusLabel_->setText("Starting...");
      startStopButton_->setText("Starting...");
      startStopButton_->setEnabled(false);
      pulseTimer_->start(50);
      break;

    case ServerState::kRunning:
      statusLabel_->setText("Running");
      startStopButton_->setText("Stop Server");
      startStopButton_->setEnabled(true);
      startStopButton_->setStyleSheet(QString(R"(
        QPushButton {
          background: %1;
          border: none;
          border-radius: %2px;
          color: white;
          font-weight: 600;
          padding: 12px 24px;
        }
        QPushButton:hover {
          background: %3;
        }
      )").arg(colors::dark::kAccentError)
        .arg(spacing::kBorderRadiusMedium())
        .arg("#ff8080"));
      pulseTimer_->stop();
      indicatorOpacity_->setOpacity(1.0);
      uptimeCounter_.start();
      uptimeTimer_->start(1000);
      break;

    case ServerState::kStopping:
      statusLabel_->setText("Stopping...");
      startStopButton_->setText("Stopping...");
      startStopButton_->setEnabled(false);
      pulseTimer_->start(50);
      break;
  }
}

void ServerStatusWidget::updateMetrics(uint64_t bytesSent, uint64_t bytesReceived,
                                        int activeClients, int maxClients) {
  bytesSentLabel_->setText(formatBytes(bytesSent));
  bytesReceivedLabel_->setText(formatBytes(bytesReceived));
  activeClientsLabel_->setText(QString::number(activeClients));
  maxClientsLabel_->setText(QString("/ %1").arg(maxClients));

  // Color code client count based on capacity
  double utilization = static_cast<double>(activeClients) / maxClients;
  QString color;
  if (utilization < 0.5) {
    color = colors::dark::kAccentSuccess;
  } else if (utilization < 0.8) {
    color = colors::dark::kAccentWarning;
  } else {
    color = colors::dark::kAccentError;
  }
  activeClientsLabel_->setStyleSheet(QString("font-size: %1px; font-weight: 600; color: %2;")
                                         .arg(fonts::kFontSizeTitle())
                                         .arg(color));
}

void ServerStatusWidget::setListenAddress(const QString& address, int port) {
  listenAddressLabel_->setText(QString("%1:%2").arg(address).arg(port));
}

void ServerStatusWidget::onStartStopClicked() {
  if (state_ == ServerState::kStopped) {
    emit startRequested();
    // Demo: simulate starting
    setServerState(ServerState::kStarting);
    QTimer::singleShot(2000, this, [this]() {
      setServerState(ServerState::kRunning);
      demoTimer_->start(1000);
    });
  } else if (state_ == ServerState::kRunning) {
    emit stopRequested();
    // Demo: simulate stopping
    setServerState(ServerState::kStopping);
    demoTimer_->stop();
    QTimer::singleShot(1000, this, [this]() {
      setServerState(ServerState::kStopped);
      // Reset demo data
      demoBytesSent_ = 0;
      demoBytesReceived_ = 0;
      demoClients_ = 0;
      updateMetrics(0, 0, 0, 100);
    });
  }
}

void ServerStatusWidget::updateUptime() {
  if (state_ == ServerState::kRunning) {
    qint64 seconds = uptimeCounter_.elapsed() / 1000;
    uptimeLabel_->setText(formatUptime(seconds));
  }
}

void ServerStatusWidget::updatePulseAnimation() {
  pulsePhase_ += 0.1f;
  float opacity = 0.5f + 0.5f * std::sin(pulsePhase_);
  indicatorOpacity_->setOpacity(static_cast<qreal>(opacity));
}

void ServerStatusWidget::simulateDemoData() {
  // Simulate traffic
  demoBytesSent_ += static_cast<uint64_t>(1024 + (rand() % 10240));
  demoBytesReceived_ += static_cast<uint64_t>(512 + (rand() % 5120));

  // Occasionally change client count
  if (rand() % 5 == 0) {
    if (rand() % 2 == 0 && demoClients_ < 100) {
      demoClients_++;
    } else if (demoClients_ > 0) {
      demoClients_--;
    }
  }

  updateMetrics(demoBytesSent_, demoBytesReceived_, demoClients_, 100);
}

void ServerStatusWidget::updateStatusIndicator() {
  QString color;
  switch (state_) {
    case ServerState::kStopped:
      color = colors::dark::kTextSecondary;
      break;
    case ServerState::kStarting:
    case ServerState::kStopping:
      color = colors::dark::kAccentWarning;
      break;
    case ServerState::kRunning:
      color = colors::dark::kAccentSuccess;
      break;
  }
  statusIndicator_->setStyleSheet(QString(R"(
    background: %1;
    border-radius: 8px;
  )").arg(color));
}

QString ServerStatusWidget::formatBytes(uint64_t bytes) const {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unitIndex = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unitIndex < 4) {
    size /= 1024;
    unitIndex++;
  }

  if (unitIndex == 0) {
    return QString("%1 %2").arg(bytes).arg(units[unitIndex]);
  }
  return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unitIndex]);
}

QString ServerStatusWidget::formatUptime(qint64 seconds) const {
  int hours = static_cast<int>(seconds / 3600);
  int minutes = static_cast<int>((seconds % 3600) / 60);
  int secs = static_cast<int>(seconds % 60);
  return QString("%1:%2:%3")
      .arg(hours, 2, 10, QChar('0'))
      .arg(minutes, 2, 10, QChar('0'))
      .arg(secs, 2, 10, QChar('0'));
}

}  // namespace veil::gui
