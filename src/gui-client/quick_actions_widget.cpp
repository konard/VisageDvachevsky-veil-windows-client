#include "quick_actions_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QSettings>
#include <QToolTip>

#include "common/gui/theme.h"
#include "connection_widget.h"

namespace veil::gui {

QuickActionsWidget::QuickActionsWidget(QWidget* parent)
    : QWidget(parent) {
  setupUi();

  // Load persisted panel state
  QSettings settings("VEIL", "VPN Client");
  bool wasExpanded = settings.value("quickActions/expanded", false).toBool();
  killSwitchEnabled_ = settings.value("quickActions/killSwitch", false).toBool();
  obfuscationEnabled_ = settings.value("advanced/obfuscation", true).toBool();

  // Set initial state without animation
  if (wasExpanded) {
    collapsed_ = false;
    contentContainer_->setMaximumHeight(16777215);  // QWIDGETSIZE_MAX
  } else {
    contentContainer_->setMaximumHeight(0);
  }

  updateToggleIcon();
  updateActionStates();
}

void QuickActionsWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Toggle button - styled as a subtle expand/collapse bar
  toggleButton_ = new QPushButton(this);
  toggleButton_->setCursor(Qt::PointingHandCursor);
  toggleButton_->setToolTip("Quick Actions (Ctrl+Q)");
  toggleButton_->setFixedHeight(36);
  toggleButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 10px;
      color: #8b949e;
      font-size: 13px;
      font-weight: 500;
      padding: 0 16px;
      text-align: center;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.06);
      border-color: rgba(255, 255, 255, 0.1);
      color: #f0f6fc;
    }
  )");
  connect(toggleButton_, &QPushButton::clicked, this, &QuickActionsWidget::onToggleClicked);
  mainLayout->addWidget(toggleButton_);

  // Content container (collapsible)
  contentContainer_ = new QFrame(this);
  contentContainer_->setObjectName("quickActionsContent");
  contentContainer_->setStyleSheet(R"(
    #quickActionsContent {
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      margin-top: 6px;
    }
  )");

  contentLayout_ = new QVBoxLayout(contentContainer_);
  contentLayout_->setSpacing(4);
  contentLayout_->setContentsMargins(12, 12, 12, 12);

  // Helper to create an action row with icon and label
  auto createActionButton = [this](const QString& icon, const QString& label,
                                    const QString& tooltip) -> QPushButton* {
    auto* btn = new QPushButton(QString("%1  %2").arg(icon, label), this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedHeight(40);
    btn->setStyleSheet(R"(
      QPushButton {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 8px;
        color: #f0f6fc;
        font-size: 13px;
        font-weight: 500;
        padding: 0 12px;
        text-align: left;
      }
      QPushButton:hover {
        background: rgba(255, 255, 255, 0.06);
        border-color: rgba(255, 255, 255, 0.08);
      }
      QPushButton:pressed {
        background: rgba(255, 255, 255, 0.1);
      }
    )");
    return btn;
  };

  // === Primary Actions Row ===
  auto* primaryLabel = new QLabel("QUICK TOGGLES", this);
  primaryLabel->setStyleSheet(R"(
    color: #6e7681;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.5px;
    padding: 4px 12px 4px 12px;
    background: transparent;
    border: none;
  )");
  contentLayout_->addWidget(primaryLabel);

  // Kill switch toggle
  killSwitchButton_ = createActionButton(
      "\u26A1", "Kill Switch", "Block all traffic if VPN disconnects");
  connect(killSwitchButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onKillSwitchClicked);
  contentLayout_->addWidget(killSwitchButton_);

  // Obfuscation toggle
  obfuscationButton_ = createActionButton(
      "\U0001F512", "Obfuscation", "Toggle traffic obfuscation");
  connect(obfuscationButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onObfuscationClicked);
  contentLayout_->addWidget(obfuscationButton_);

  // Separator
  auto* sep1 = new QFrame(this);
  sep1->setFrameShape(QFrame::HLine);
  sep1->setStyleSheet("background-color: rgba(255, 255, 255, 0.04); max-height: 1px; border: none;");
  contentLayout_->addWidget(sep1);

  // === Utility Actions ===
  auto* utilityLabel = new QLabel("UTILITIES", this);
  utilityLabel->setStyleSheet(R"(
    color: #6e7681;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.5px;
    padding: 4px 12px 4px 12px;
    background: transparent;
    border: none;
  )");
  contentLayout_->addWidget(utilityLabel);

  // Copy IP address
  copyIpButton_ = createActionButton(
      "\U0001F4CB", "Copy IP Address", "Copy current server IP to clipboard");
  connect(copyIpButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onCopyIpClicked);
  contentLayout_->addWidget(copyIpButton_);

  // Share connection status
  shareStatusButton_ = createActionButton(
      "\U0001F4E4", "Share Status", "Copy connection status to clipboard");
  connect(shareStatusButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onShareStatusClicked);
  contentLayout_->addWidget(shareStatusButton_);

  // Separator
  auto* sep2 = new QFrame(this);
  sep2->setFrameShape(QFrame::HLine);
  sep2->setStyleSheet("background-color: rgba(255, 255, 255, 0.04); max-height: 1px; border: none;");
  contentLayout_->addWidget(sep2);

  // === Debug Actions ===
  auto* debugLabel = new QLabel("DEBUG", this);
  debugLabel->setStyleSheet(R"(
    color: #6e7681;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.5px;
    padding: 4px 12px 4px 12px;
    background: transparent;
    border: none;
  )");
  contentLayout_->addWidget(debugLabel);

  // Open diagnostics
  openDiagnosticsButton_ = createActionButton(
      "\U0001F50D", "Open Diagnostics", "Open the diagnostics view");
  connect(openDiagnosticsButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onOpenDiagnosticsClicked);
  contentLayout_->addWidget(openDiagnosticsButton_);

  // Copy debug info
  copyDebugInfoButton_ = createActionButton(
      "\U0001F41B", "Copy Debug Info", "Copy diagnostic info to clipboard");
  connect(copyDebugInfoButton_, &QPushButton::clicked,
          this, &QuickActionsWidget::onCopyDebugInfoClicked);
  contentLayout_->addWidget(copyDebugInfoButton_);

  mainLayout->addWidget(contentContainer_);

  // Setup animation
  animation_ = new QPropertyAnimation(this, "contentHeight", this);
  animation_->setDuration(animations::kDurationNormal);
  animation_->setEasingCurve(QEasingCurve::OutCubic);

  updateToggleIcon();
}

void QuickActionsWidget::onToggleClicked() {
  collapsed_ = !collapsed_;

  // Persist state
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("quickActions/expanded", !collapsed_);

  // Calculate expanded height if needed
  if (expandedHeight_ == 0) {
    expandedHeight_ = contentContainer_->sizeHint().height();
  }

  // Animate
  animation_->stop();
  if (collapsed_) {
    animation_->setStartValue(contentContainer_->height());
    animation_->setEndValue(0);
  } else {
    animation_->setStartValue(0);
    animation_->setEndValue(expandedHeight_);
  }
  animation_->start();

  updateToggleIcon();
}

void QuickActionsWidget::updateToggleIcon() {
  if (collapsed_) {
    toggleButton_->setText("\u25BC Quick Actions \u25BC");
  } else {
    toggleButton_->setText("\u25B2 Quick Actions \u25B2");
  }
}

int QuickActionsWidget::contentHeight() const {
  return contentContainer_->maximumHeight();
}

void QuickActionsWidget::setContentHeight(int height) {
  contentContainer_->setMaximumHeight(height);
}

void QuickActionsWidget::setIpAddress(const QString& ip, uint16_t port) {
  ipAddress_ = ip;
  port_ = port;
}

void QuickActionsWidget::setConnectionState(ConnectionState state) {
  connectionState_ = state;
  updateActionStates();
}

void QuickActionsWidget::setKillSwitchEnabled(bool enabled) {
  killSwitchEnabled_ = enabled;

  QSettings settings("VEIL", "VPN Client");
  settings.setValue("quickActions/killSwitch", enabled);

  updateActionStates();
}

void QuickActionsWidget::setObfuscationEnabled(bool enabled) {
  obfuscationEnabled_ = enabled;

  QSettings settings("VEIL", "VPN Client");
  settings.setValue("advanced/obfuscation", enabled);

  updateActionStates();
}

void QuickActionsWidget::updateActionStates() {
  bool isConnected = (connectionState_ == ConnectionState::kConnected);

  // Update kill switch button text with state indicator
  QString ksState = killSwitchEnabled_ ? "ON" : "OFF";
  QString ksColor = killSwitchEnabled_ ? "#3fb950" : "#8b949e";
  killSwitchButton_->setText(
      QString("\u26A1  Kill Switch                              [%1]").arg(ksState));
  killSwitchButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 8px;
      color: %1;
      font-size: 13px;
      font-weight: 500;
      padding: 0 12px;
      text-align: left;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.06);
      border-color: rgba(255, 255, 255, 0.08);
    }
    QPushButton:pressed {
      background: rgba(255, 255, 255, 0.1);
    }
  )").arg(ksColor));

  // Update obfuscation button text with state indicator
  QString obState = obfuscationEnabled_ ? "ON" : "OFF";
  QString obColor = obfuscationEnabled_ ? "#3fb950" : "#8b949e";
  obfuscationButton_->setText(
      QString("\U0001F512  Obfuscation                            [%1]").arg(obState));
  obfuscationButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 8px;
      color: %1;
      font-size: 13px;
      font-weight: 500;
      padding: 0 12px;
      text-align: left;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.06);
      border-color: rgba(255, 255, 255, 0.08);
    }
    QPushButton:pressed {
      background: rgba(255, 255, 255, 0.1);
    }
  )").arg(obColor));

  // Copy IP is only meaningful when connected
  copyIpButton_->setEnabled(isConnected);
  if (!isConnected) {
    copyIpButton_->setStyleSheet(R"(
      QPushButton {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 8px;
        color: #484f58;
        font-size: 13px;
        font-weight: 500;
        padding: 0 12px;
        text-align: left;
      }
    )");
  } else {
    copyIpButton_->setStyleSheet(R"(
      QPushButton {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 8px;
        color: #f0f6fc;
        font-size: 13px;
        font-weight: 500;
        padding: 0 12px;
        text-align: left;
      }
      QPushButton:hover {
        background: rgba(255, 255, 255, 0.06);
        border-color: rgba(255, 255, 255, 0.08);
      }
      QPushButton:pressed {
        background: rgba(255, 255, 255, 0.1);
      }
    )");
  }
}

void QuickActionsWidget::onKillSwitchClicked() {
  killSwitchEnabled_ = !killSwitchEnabled_;
  setKillSwitchEnabled(killSwitchEnabled_);
  emit killSwitchToggled(killSwitchEnabled_);
}

void QuickActionsWidget::onObfuscationClicked() {
  obfuscationEnabled_ = !obfuscationEnabled_;
  setObfuscationEnabled(obfuscationEnabled_);
  emit obfuscationToggled(obfuscationEnabled_);
}

void QuickActionsWidget::onCopyIpClicked() {
  if (ipAddress_.isEmpty()) {
    QToolTip::showText(copyIpButton_->mapToGlobal(QPoint(0, 0)),
                       "No IP address available", copyIpButton_, QRect(), 2000);
    return;
  }

  QString text = port_ > 0
      ? QString("%1:%2").arg(ipAddress_).arg(port_)
      : ipAddress_;
  QApplication::clipboard()->setText(text);
  QToolTip::showText(copyIpButton_->mapToGlobal(QPoint(0, 0)),
                     "IP copied to clipboard", copyIpButton_, QRect(), 2000);
}

void QuickActionsWidget::onShareStatusClicked() {
  QString status;
  switch (connectionState_) {
    case ConnectionState::kDisconnected:
      status = "VEIL VPN: Not Connected";
      break;
    case ConnectionState::kConnecting:
      status = "VEIL VPN: Connecting...";
      break;
    case ConnectionState::kConnected:
      status = QString("VEIL VPN: Connected to %1:%2")
          .arg(ipAddress_).arg(port_);
      if (killSwitchEnabled_) {
        status += " | Kill Switch: ON";
      }
      if (obfuscationEnabled_) {
        status += " | Obfuscation: ON";
      }
      break;
    case ConnectionState::kReconnecting:
      status = "VEIL VPN: Reconnecting...";
      break;
    case ConnectionState::kError:
      status = "VEIL VPN: Connection Error";
      break;
  }

  QApplication::clipboard()->setText(status);
  QToolTip::showText(shareStatusButton_->mapToGlobal(QPoint(0, 0)),
                     "Status copied to clipboard", shareStatusButton_, QRect(), 2000);
}

void QuickActionsWidget::onOpenDiagnosticsClicked() {
  emit diagnosticsRequested();
}

void QuickActionsWidget::onCopyDebugInfoClicked() {
  QSettings settings("VEIL", "VPN Client");

  QString debugInfo;
  debugInfo += "=== VEIL VPN Debug Info ===\n";
  debugInfo += QString("Server: %1:%2\n").arg(
      settings.value("server/address", "N/A").toString()).arg(
      settings.value("server/port", 4433).toInt());
  debugInfo += QString("Kill Switch: %1\n").arg(killSwitchEnabled_ ? "ON" : "OFF");
  debugInfo += QString("Obfuscation: %1\n").arg(obfuscationEnabled_ ? "ON" : "OFF");
  debugInfo += QString("DPI Bypass Mode: %1\n").arg(
      settings.value("dpi/mode", 0).toInt());
  debugInfo += QString("Auto Reconnect: %1\n").arg(
      settings.value("connection/autoReconnect", true).toBool() ? "ON" : "OFF");
  debugInfo += QString("Route All Traffic: %1\n").arg(
      settings.value("routing/routeAllTraffic", true).toBool() ? "YES" : "NO");

  switch (connectionState_) {
    case ConnectionState::kDisconnected:
      debugInfo += "Connection State: Disconnected\n";
      break;
    case ConnectionState::kConnecting:
      debugInfo += "Connection State: Connecting\n";
      break;
    case ConnectionState::kConnected:
      debugInfo += "Connection State: Connected\n";
      break;
    case ConnectionState::kReconnecting:
      debugInfo += "Connection State: Reconnecting\n";
      break;
    case ConnectionState::kError:
      debugInfo += "Connection State: Error\n";
      break;
  }

  debugInfo += "===========================\n";

  QApplication::clipboard()->setText(debugInfo);
  QToolTip::showText(copyDebugInfoButton_->mapToGlobal(QPoint(0, 0)),
                     "Debug info copied to clipboard", copyDebugInfoButton_, QRect(), 2000);
}

}  // namespace veil::gui
