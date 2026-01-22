#include "connection_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QRandomGenerator>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QSettings>

#include "common/gui/theme.h"

namespace veil::gui {

// Custom widget for the circular status indicator with glow effect
class StatusRing : public QWidget {
 public:
  explicit StatusRing(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(160, 160);
    setAttribute(Qt::WA_TranslucentBackground);
  }

  void setState(ConnectionState state) {
    state_ = state;
    update();
  }

  void setPulsePhase(qreal phase) {
    pulsePhase_ = phase;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int size = qMin(width(), height());
    const int centerX = width() / 2;
    const int centerY = height() / 2;
    const int ringWidth = 6;
    const int radius = (size - ringWidth) / 2 - 16;

    // Determine colors based on state
    QColor baseColor, glowColor;
    switch (state_) {
      case ConnectionState::kConnected:
        baseColor = QColor("#3fb950");
        glowColor = QColor(63, 185, 80, static_cast<int>(100 + 60 * pulsePhase_));
        break;
      case ConnectionState::kConnecting:
      case ConnectionState::kReconnecting:
        baseColor = QColor("#d29922");
        glowColor = QColor(210, 153, 34, static_cast<int>(80 + 80 * pulsePhase_));
        break;
      case ConnectionState::kError:
        baseColor = QColor("#f85149");
        glowColor = QColor(248, 81, 73, static_cast<int>(80 + 60 * pulsePhase_));
        break;
      default:
        baseColor = QColor("#484f58");
        glowColor = QColor(72, 79, 88, 40);
        break;
    }

    // Draw outer glow for connected/connecting states
    if (state_ != ConnectionState::kDisconnected) {
      QRadialGradient glowGradient(centerX, centerY, radius + 30);
      glowGradient.setColorAt(0.5, glowColor);
      glowGradient.setColorAt(1.0, Qt::transparent);
      painter.setBrush(glowGradient);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPoint(centerX, centerY), radius + 30, radius + 30);
    }

    // Draw background circle (subtle)
    painter.setBrush(QColor(22, 27, 34, 180));
    painter.setPen(QPen(QColor(255, 255, 255, 15), 1));
    painter.drawEllipse(QPoint(centerX, centerY), radius, radius);

    // Draw main ring
    QPen ringPen(baseColor, ringWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);

    if (state_ == ConnectionState::kConnecting || state_ == ConnectionState::kReconnecting) {
      // Animated arc for connecting state
      int startAngle = static_cast<int>(pulsePhase_ * 360 * 16);
      int spanAngle = 270 * 16;
      painter.drawArc(centerX - radius, centerY - radius,
                      radius * 2, radius * 2, startAngle, spanAngle);
    } else {
      // Full ring for other states
      painter.drawEllipse(QPoint(centerX, centerY), radius, radius);
    }

    // Draw inner icon
    painter.setPen(Qt::NoPen);
    if (state_ == ConnectionState::kConnected) {
      // Shield check icon
      painter.setBrush(baseColor);
      QPainterPath shield;
      int iconSize = 36;
      int ix = centerX - iconSize/2;
      int iy = centerY - iconSize/2;
      shield.moveTo(ix + iconSize/2, iy);
      shield.lineTo(ix + iconSize, iy + iconSize * 0.3);
      shield.lineTo(ix + iconSize, iy + iconSize * 0.6);
      shield.quadTo(ix + iconSize/2, iy + iconSize * 1.1, ix + iconSize/2, iy + iconSize);
      shield.quadTo(ix + iconSize/2, iy + iconSize * 1.1, ix, iy + iconSize * 0.6);
      shield.lineTo(ix, iy + iconSize * 0.3);
      shield.closeSubpath();
      painter.drawPath(shield);

      // Checkmark
      painter.setPen(QPen(QColor("#0d1117"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(ix + 12, iy + 20, ix + 16, iy + 26);
      painter.drawLine(ix + 16, iy + 26, ix + 26, iy + 14);
    } else if (state_ == ConnectionState::kDisconnected) {
      // Shield outline
      painter.setPen(QPen(baseColor, 2));
      painter.setBrush(Qt::NoBrush);
      QPainterPath shield;
      int iconSize = 36;
      int ix = centerX - iconSize/2;
      int iy = centerY - iconSize/2;
      shield.moveTo(ix + iconSize/2, iy);
      shield.lineTo(ix + iconSize, iy + iconSize * 0.3);
      shield.lineTo(ix + iconSize, iy + iconSize * 0.6);
      shield.quadTo(ix + iconSize/2, iy + iconSize * 1.1, ix + iconSize/2, iy + iconSize);
      shield.quadTo(ix + iconSize/2, iy + iconSize * 1.1, ix, iy + iconSize * 0.6);
      shield.lineTo(ix, iy + iconSize * 0.3);
      shield.closeSubpath();
      painter.drawPath(shield);
    } else if (state_ == ConnectionState::kError) {
      // Warning triangle
      painter.setBrush(baseColor);
      QPolygonF triangle;
      triangle << QPointF(centerX, centerY - 18)
               << QPointF(centerX + 20, centerY + 14)
               << QPointF(centerX - 20, centerY + 14);
      painter.drawPolygon(triangle);

      // Exclamation mark
      painter.setPen(QPen(QColor("#0d1117"), 3, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(centerX, centerY - 8, centerX, centerY + 2);
      painter.drawPoint(centerX, centerY + 8);
    }
  }

 private:
  ConnectionState state_{ConnectionState::kDisconnected};
  qreal pulsePhase_{0.0};
};

ConnectionWidget::ConnectionWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
  setupAnimations();
  loadServerSettings();
}

void ConnectionWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge, spacing::kPaddingLarge,
                                  spacing::kPaddingXLarge, spacing::kPaddingLarge);

  // === Header with branding ===
  auto* headerWidget = new QWidget(this);
  auto* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, spacing::kPaddingLarge);

  auto* logoContainer = new QWidget(headerWidget);
  auto* logoLayout = new QHBoxLayout(logoContainer);
  logoLayout->setContentsMargins(0, 0, 0, 0);
  logoLayout->setSpacing(12);

  // Logo icon placeholder
  auto* logoIcon = new QLabel(this);
  logoIcon->setFixedSize(32, 32);
  logoIcon->setStyleSheet(R"(
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #238636, stop:1 #3fb950);
    border-radius: 8px;
  )");
  logoLayout->addWidget(logoIcon);

  auto* logoText = new QLabel("VEIL", this);
  logoText->setStyleSheet(QString(R"(
    font-size: 24px;
    font-weight: 700;
    color: #f0f6fc;
    letter-spacing: 2px;
  )"));
  logoLayout->addWidget(logoText);

  headerLayout->addWidget(logoContainer);
  headerLayout->addStretch();

  // Settings button with icon-style
  settingsButton_ = new QPushButton(this);
  settingsButton_->setFixedSize(40, 40);
  settingsButton_->setCursor(Qt::PointingHandCursor);
  settingsButton_->setToolTip("Settings");
  settingsButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      font-size: 18px;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.15);
    }
  )");
  settingsButton_->setText("\u2699");  // Gear icon
  connect(settingsButton_, &QPushButton::clicked, this, &ConnectionWidget::settingsRequested);
  headerLayout->addWidget(settingsButton_);

  mainLayout->addWidget(headerWidget);

  // === Central Status Area ===
  auto* statusContainer = new QWidget(this);
  statusContainer->setStyleSheet(R"(
    QWidget {
      background: transparent;
    }
  )");
  auto* statusContainerLayout = new QVBoxLayout(statusContainer);
  statusContainerLayout->setAlignment(Qt::AlignCenter);
  statusContainerLayout->setSpacing(20);

  // Status ring (custom painted widget)
  statusRing_ = new StatusRing(this);
  statusContainerLayout->addWidget(statusRing_, 0, Qt::AlignCenter);

  // Status text
  statusLabel_ = new QLabel("Not Connected", this);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setStyleSheet(QString(R"(
    font-size: 22px;
    font-weight: 600;
    color: %1;
  )").arg(colors::dark::kTextSecondary));
  statusContainerLayout->addWidget(statusLabel_);

  // Subtitle / IP info
  subtitleLabel_ = new QLabel("Tap Connect to secure your connection", this);
  subtitleLabel_->setAlignment(Qt::AlignCenter);
  subtitleLabel_->setStyleSheet(QString(R"(
    font-size: 14px;
    color: %1;
    padding: 0 40px;
  )").arg(colors::dark::kTextTertiary));
  subtitleLabel_->setWordWrap(true);
  statusContainerLayout->addWidget(subtitleLabel_);

  // Error label (hidden by default)
  errorLabel_ = new QLabel(this);
  errorLabel_->setWordWrap(true);
  errorLabel_->setAlignment(Qt::AlignCenter);
  errorLabel_->setStyleSheet(QString(R"(
    color: %1;
    font-size: 13px;
    padding: 12px 20px;
    background: rgba(248, 81, 73, 0.1);
    border: 1px solid rgba(248, 81, 73, 0.3);
    border-radius: 10px;
    margin: 8px 20px;
  )").arg(colors::dark::kAccentError));
  errorLabel_->hide();
  statusContainerLayout->addWidget(errorLabel_);

  mainLayout->addWidget(statusContainer, 1);

  // === Connect Button (Large, prominent) ===
  connectButton_ = new QPushButton("Connect", this);
  connectButton_->setMinimumHeight(64);
  connectButton_->setCursor(Qt::PointingHandCursor);
  connectButton_->setStyleSheet(R"(
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 16px;
      color: white;
      font-size: 18px;
      font-weight: 600;
      letter-spacing: 0.5px;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #1a7f37, stop:1 #238636);
    }
  )");
  connect(connectButton_, &QPushButton::clicked, this, &ConnectionWidget::onConnectClicked);
  mainLayout->addWidget(connectButton_);

  mainLayout->addSpacing(spacing::kPaddingLarge);

  // === Session Info Card ===
  statusCard_ = new QWidget(this);
  statusCard_->setObjectName("sessionCard");
  statusCard_->setStyleSheet(R"(
    #sessionCard {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
    }
  )");

  auto* cardLayout = new QVBoxLayout(statusCard_);
  cardLayout->setSpacing(0);
  cardLayout->setContentsMargins(20, 16, 20, 16);

  // Helper function to create info rows
  auto createInfoRow = [this, cardLayout](const QString& icon, const QString& label,
                                           QLabel*& valueLabel, bool addSeparator = true) {
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 12, 0, 12);
    rowLayout->setSpacing(12);

    // Icon
    auto* iconLabel = new QLabel(icon, this);
    iconLabel->setFixedWidth(24);
    iconLabel->setStyleSheet("font-size: 16px; color: #6e7681;");
    rowLayout->addWidget(iconLabel);

    // Label
    auto* textLabel = new QLabel(label, this);
    textLabel->setStyleSheet("color: #8b949e; font-size: 14px;");
    rowLayout->addWidget(textLabel);

    rowLayout->addStretch();

    // Value
    valueLabel = new QLabel("\u2014", this);
    valueLabel->setStyleSheet("color: #f0f6fc; font-size: 14px; font-weight: 500;");
    rowLayout->addWidget(valueLabel);

    cardLayout->addWidget(row);

    if (addSeparator) {
      auto* sep = new QFrame(this);
      sep->setFrameShape(QFrame::HLine);
      sep->setStyleSheet("background-color: rgba(255, 255, 255, 0.04); max-height: 1px;");
      cardLayout->addWidget(sep);
    }
  };

  createInfoRow("\U0001F310", "Server", serverLabel_);  // Globe
  createInfoRow("\u23F1", "Latency", latencyLabel_);  // Stopwatch
  createInfoRow("\u2191\u2193", "TX / RX", throughputLabel_);  // Up/Down arrows
  createInfoRow("\u23F0", "Uptime", uptimeLabel_, false);  // Clock

  mainLayout->addWidget(statusCard_);

  // Session ID row (separate, monospace)
  sessionInfoGroup_ = new QWidget(this);
  auto* sessionLayout = new QHBoxLayout(sessionInfoGroup_);
  sessionLayout->setContentsMargins(20, 12, 20, 0);
  sessionLayout->setSpacing(8);

  auto* sessionIcon = new QLabel("\U0001F511", this);  // Key
  sessionIcon->setStyleSheet("font-size: 14px; color: #6e7681;");
  sessionLayout->addWidget(sessionIcon);

  auto* sessionTitle = new QLabel("Session", this);
  sessionTitle->setStyleSheet("color: #6e7681; font-size: 13px;");
  sessionLayout->addWidget(sessionTitle);

  sessionLayout->addStretch();

  sessionIdLabel_ = new QLabel("\u2014", this);
  sessionIdLabel_->setStyleSheet(R"(
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 12px;
    color: #79c0ff;
  )");
  sessionLayout->addWidget(sessionIdLabel_);

  mainLayout->addWidget(sessionInfoGroup_);

  mainLayout->addStretch();
}

void ConnectionWidget::setupAnimations() {
  // Setup pulse animation timer for connecting state
  pulseTimer_ = new QTimer(this);
  pulseTimer_->setInterval(50);  // Smooth animation at ~20fps
  connect(pulseTimer_, &QTimer::timeout, this, &ConnectionWidget::onPulseAnimation);

  // Setup uptime timer
  uptimeTimer_ = new QTimer(this);
  uptimeTimer_->setInterval(1000);  // Update every second
  connect(uptimeTimer_, &QTimer::timeout, this, &ConnectionWidget::onUptimeUpdate);

  // Setup opacity effect for status indicator
  statusOpacity_ = new QGraphicsOpacityEffect(this);
  statusOpacity_->setOpacity(1.0);
}

void ConnectionWidget::onConnectClicked() {
  if (state_ == ConnectionState::kConnected ||
      state_ == ConnectionState::kConnecting ||
      state_ == ConnectionState::kReconnecting) {
    // Disconnect - emit signal for IPC manager to handle
    emit disconnectRequested();
    // State will be updated when we receive response from daemon
  } else {
    // Connect - emit signal for IPC manager to handle
    emit connectRequested();
    // Set connecting state immediately for user feedback
    // Actual state will be updated when we receive response from daemon
    setConnectionState(ConnectionState::kConnecting);
  }
}

void ConnectionWidget::setConnectionState(ConnectionState state) {
  state_ = state;

  // Update the status ring
  if (statusRing_) {
    static_cast<StatusRing*>(statusRing_)->setState(state);
  }

  // Handle state transitions
  if (state == ConnectionState::kConnecting || state == ConnectionState::kReconnecting) {
    startPulseAnimation();
  } else {
    stopPulseAnimation();
  }

  if (state == ConnectionState::kConnected) {
    uptimeCounter_.start();
    uptimeTimer_->start();
  } else {
    uptimeTimer_->stop();
  }

  if (state == ConnectionState::kDisconnected || state == ConnectionState::kError) {
    // Reset metrics
    latencyMs_ = 0;
    txBytes_ = 0;
    rxBytes_ = 0;
    sessionId_.clear();
  }

  updateStatusDisplay();
}

void ConnectionWidget::updateStatusDisplay() {
  QString statusColor = getStatusColor();
  QString statusText = getStatusText();

  // Update status label
  statusLabel_->setText(statusText);
  statusLabel_->setStyleSheet(QString("font-size: 22px; font-weight: 600; color: %1;").arg(statusColor));

  // Update subtitle based on state
  switch (state_) {
    case ConnectionState::kDisconnected:
      subtitleLabel_->setText("Tap Connect to secure your connection");
      subtitleLabel_->setStyleSheet(QString("font-size: 14px; color: %1; padding: 0 40px;")
                                        .arg(colors::dark::kTextTertiary));
      break;
    case ConnectionState::kConnecting:
      subtitleLabel_->setText("Establishing secure tunnel...");
      subtitleLabel_->setStyleSheet(QString("font-size: 14px; color: %1; padding: 0 40px;")
                                        .arg(colors::dark::kAccentWarning));
      break;
    case ConnectionState::kConnected:
      subtitleLabel_->setText(QString("Connected to %1").arg(serverAddress_));
      subtitleLabel_->setStyleSheet(QString("font-size: 14px; color: %1; padding: 0 40px;")
                                        .arg(colors::dark::kAccentSuccess));
      break;
    case ConnectionState::kReconnecting:
      subtitleLabel_->setText(QString("Reconnecting... Attempt %1").arg(reconnectAttempt_));
      subtitleLabel_->setStyleSheet(QString("font-size: 14px; color: %1; padding: 0 40px;")
                                        .arg(colors::dark::kAccentWarning));
      break;
    case ConnectionState::kError:
      subtitleLabel_->setText("Connection failed");
      subtitleLabel_->setStyleSheet(QString("font-size: 14px; color: %1; padding: 0 40px;")
                                        .arg(colors::dark::kAccentError));
      break;
  }

  // Update connect button
  switch (state_) {
    case ConnectionState::kDisconnected:
    case ConnectionState::kError:
      connectButton_->setText(state_ == ConnectionState::kError ? "Retry Connection" : "Connect");
      connectButton_->setStyleSheet(R"(
        QPushButton {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #238636, stop:1 #2ea043);
          border: none;
          border-radius: 16px;
          color: white;
          font-size: 18px;
          font-weight: 600;
          letter-spacing: 0.5px;
        }
        QPushButton:hover {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #2ea043, stop:1 #3fb950);
        }
        QPushButton:pressed {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #1a7f37, stop:1 #238636);
        }
      )");
      break;
    case ConnectionState::kConnecting:
    case ConnectionState::kReconnecting:
      connectButton_->setText("Cancel");
      connectButton_->setStyleSheet(QString(R"(
        QPushButton {
          background: transparent;
          border: 2px solid rgba(255, 255, 255, 0.2);
          border-radius: 16px;
          color: #8b949e;
          font-size: 18px;
          font-weight: 600;
        }
        QPushButton:hover {
          background: rgba(255, 255, 255, 0.04);
          border-color: rgba(255, 255, 255, 0.3);
          color: #f0f6fc;
        }
      )"));
      break;
    case ConnectionState::kConnected:
      connectButton_->setText("Disconnect");
      connectButton_->setStyleSheet(QString(R"(
        QPushButton {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #da3633, stop:1 #f85149);
          border: none;
          border-radius: 16px;
          color: white;
          font-size: 18px;
          font-weight: 600;
        }
        QPushButton:hover {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #f85149, stop:1 #ff7b7b);
        }
        QPushButton:pressed {
          background: #b62324;
        }
      )"));
      break;
  }

  // Show/hide error label
  if (state_ == ConnectionState::kError && !errorMessage_.isEmpty()) {
    errorLabel_->setText(errorMessage_);
    errorLabel_->show();
  } else {
    errorLabel_->hide();
  }

  // Update session info display based on state
  bool showMetrics = (state_ == ConnectionState::kConnected);

  if (showMetrics) {
    if (!sessionId_.isEmpty()) {
      // Truncate long session IDs
      QString displayId = sessionId_;
      if (displayId.length() > 18) {
        displayId = displayId.left(8) + "..." + displayId.right(6);
      }
      sessionIdLabel_->setText(displayId);
    }

    serverLabel_->setText(QString("%1:%2").arg(serverAddress_).arg(serverPort_));

    // Latency color coding
    QString latencyColor = colors::dark::kTextPrimary;
    if (latencyMs_ > 0) {
      if (latencyMs_ <= 50) {
        latencyColor = colors::dark::kAccentSuccess;
      } else if (latencyMs_ <= 100) {
        latencyColor = colors::dark::kAccentWarning;
      } else {
        latencyColor = colors::dark::kAccentError;
      }
      latencyLabel_->setText(QString("%1 ms").arg(latencyMs_));
      latencyLabel_->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: 500;").arg(latencyColor));
    }
    throughputLabel_->setText(QString("%1 / %2").arg(formatBytes(txBytes_), formatBytes(rxBytes_)));
  } else {
    sessionIdLabel_->setText("\u2014");
    serverLabel_->setText(QString("%1:%2").arg(serverAddress_).arg(serverPort_));
    latencyLabel_->setText("\u2014");
    latencyLabel_->setStyleSheet("color: #f0f6fc; font-size: 14px; font-weight: 500;");
    throughputLabel_->setText("\u2014");
    uptimeLabel_->setText("\u2014");
  }
}

void ConnectionWidget::updateMetrics(int latencyMs, uint64_t txBytesPerSec, uint64_t rxBytesPerSec) {
  latencyMs_ = latencyMs;
  txBytes_ = txBytesPerSec;
  rxBytes_ = rxBytesPerSec;

  if (state_ == ConnectionState::kConnected) {
    updateStatusDisplay();
  }
}

void ConnectionWidget::setSessionId(const QString& sessionId) {
  sessionId_ = sessionId;
  if (state_ == ConnectionState::kConnected) {
    updateStatusDisplay();
  }
}

void ConnectionWidget::setServerAddress(const QString& server, uint16_t port) {
  serverAddress_ = server;
  serverPort_ = port;
  serverLabel_->setText(QString("%1:%2").arg(server).arg(port));
}

void ConnectionWidget::setErrorMessage(const QString& message) {
  errorMessage_ = message;
  if (state_ == ConnectionState::kError) {
    errorLabel_->setText(message);
    errorLabel_->show();
  }
}

void ConnectionWidget::onPulseAnimation() {
  animationPhase_ += 0.03;
  if (animationPhase_ > 1.0) {
    animationPhase_ -= 1.0;
  }

  if (statusRing_) {
    static_cast<StatusRing*>(statusRing_)->setPulsePhase(animationPhase_);
  }
}

void ConnectionWidget::onUptimeUpdate() {
  if (state_ == ConnectionState::kConnected && uptimeCounter_.isValid()) {
    int seconds = static_cast<int>(uptimeCounter_.elapsed() / 1000);
    uptimeLabel_->setText(formatUptime(seconds));
    // Metrics are updated via IPC from the daemon, not simulated here
  }
}

void ConnectionWidget::startPulseAnimation() {
  animationPhase_ = 0.0;
  pulseTimer_->start();
}

void ConnectionWidget::stopPulseAnimation() {
  pulseTimer_->stop();
  animationPhase_ = 0.0;
  if (statusRing_) {
    static_cast<StatusRing*>(statusRing_)->setPulsePhase(0.0);
  }
}

QString ConnectionWidget::formatBytes(uint64_t bytesPerSec) const {
  if (bytesPerSec >= 1073741824) {  // >= 1 GB/s
    return QString("%1 GB/s").arg(static_cast<double>(bytesPerSec) / 1073741824.0, 0, 'f', 1);
  } else if (bytesPerSec >= 1048576) {  // >= 1 MB/s
    return QString("%1 MB/s").arg(static_cast<double>(bytesPerSec) / 1048576.0, 0, 'f', 1);
  } else if (bytesPerSec >= 1024) {  // >= 1 KB/s
    return QString("%1 KB/s").arg(static_cast<double>(bytesPerSec) / 1024.0, 0, 'f', 1);
  } else {
    return QString("%1 B/s").arg(bytesPerSec);
  }
}

QString ConnectionWidget::formatUptime(int seconds) const {
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  return QString("%1:%2:%3")
      .arg(hours, 2, 10, QChar('0'))
      .arg(minutes, 2, 10, QChar('0'))
      .arg(secs, 2, 10, QChar('0'));
}

QString ConnectionWidget::getStatusColor() const {
  switch (state_) {
    case ConnectionState::kDisconnected:
      return colors::dark::kTextSecondary;
    case ConnectionState::kConnecting:
    case ConnectionState::kReconnecting:
      return colors::dark::kAccentWarning;
    case ConnectionState::kConnected:
      return colors::dark::kAccentSuccess;
    case ConnectionState::kError:
      return colors::dark::kAccentError;
  }
  return colors::dark::kTextSecondary;
}

QString ConnectionWidget::getStatusText() const {
  switch (state_) {
    case ConnectionState::kDisconnected:
      return "Not Connected";
    case ConnectionState::kConnecting:
      return "Connecting";
    case ConnectionState::kConnected:
      return "Protected";
    case ConnectionState::kReconnecting:
      return "Reconnecting";
    case ConnectionState::kError:
      return "Connection Failed";
  }
  return "Unknown";
}

void ConnectionWidget::loadServerSettings() {
  QSettings settings("VEIL", "VPN Client");
  QString serverAddress = settings.value("server/address", "vpn.example.com").toString();
  uint16_t serverPort = static_cast<uint16_t>(settings.value("server/port", 4433).toInt());
  setServerAddress(serverAddress, serverPort);
}

}  // namespace veil::gui
