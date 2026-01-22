#include "mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QShortcut>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QSettings>
#include <QDateTime>

#include "common/gui/theme.h"
#include "common/ipc/ipc_protocol.h"
#include "common/version.h"
#include "connection_widget.h"
#include "diagnostics_widget.h"
#include "ipc_client_manager.h"
#include "settings_widget.h"
#include "update_checker.h"

namespace veil::gui {

// ===================== AnimatedStackedWidget Implementation =====================

AnimatedStackedWidget::AnimatedStackedWidget(QWidget* parent)
    : QStackedWidget(parent) {
}

void AnimatedStackedWidget::setCurrentWidgetAnimated(int index) {
  if (index == currentIndex() || isAnimating_) {
    return;
  }

  isAnimating_ = true;

  QWidget* currentW = currentWidget();
  QWidget* nextW = widget(index);

  if (!currentW || !nextW) {
    setCurrentIndex(index);
    isAnimating_ = false;
    return;
  }

  // Prepare the next widget
  nextW->setGeometry(0, 0, width(), height());
  nextW->show();
  nextW->raise();

  // Create opacity effects
  auto* currentEffect = new QGraphicsOpacityEffect(currentW);
  auto* nextEffect = new QGraphicsOpacityEffect(nextW);
  currentW->setGraphicsEffect(currentEffect);
  nextW->setGraphicsEffect(nextEffect);

  // Animation group for parallel execution
  auto* group = new QParallelAnimationGroup(this);

  // Fade out current widget
  auto* fadeOut = new QPropertyAnimation(currentEffect, "opacity", this);
  fadeOut->setDuration(animationDuration_);
  fadeOut->setStartValue(1.0);
  fadeOut->setEndValue(0.0);
  fadeOut->setEasingCurve(QEasingCurve::OutCubic);
  group->addAnimation(fadeOut);

  // Fade in next widget
  auto* fadeIn = new QPropertyAnimation(nextEffect, "opacity", this);
  fadeIn->setDuration(animationDuration_);
  fadeIn->setStartValue(0.0);
  fadeIn->setEndValue(1.0);
  fadeIn->setEasingCurve(QEasingCurve::InCubic);
  group->addAnimation(fadeIn);

  // Connect completion handler
  connect(group, &QParallelAnimationGroup::finished, this, [this, currentW, nextW, index]() {
    // Clean up effects
    currentW->setGraphicsEffect(nullptr);
    nextW->setGraphicsEffect(nullptr);

    // Actually switch to the new widget
    setCurrentIndex(index);
    isAnimating_ = false;
  });

  group->start(QAbstractAnimation::DeleteWhenStopped);
}

// ===================== MainWindow Implementation =====================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      stackedWidget_(new AnimatedStackedWidget(this)),
      connectionWidget_(new ConnectionWidget(this)),
      settingsWidget_(new SettingsWidget(this)),
      diagnosticsWidget_(new DiagnosticsWidget(this)),
      ipcManager_(std::make_unique<IpcClientManager>(this)),
      trayIcon_(nullptr),
      trayMenu_(nullptr),
      trayConnectAction_(nullptr),
      trayDisconnectAction_(nullptr),
      updateChecker_(std::make_unique<UpdateChecker>(this)) {
  setupUi();
  setupIpcConnections();
  setupMenuBar();
  setupStatusBar();
  setupSystemTray();
  setupUpdateChecker();
  applyDarkTheme();

  // Attempt to connect to daemon
  if (!ipcManager_->connectToDaemon()) {
    statusBar()->showMessage(tr("Daemon not running - start veil-client first"), 5000);
  }

  // Check for updates on startup (delayed)
  QTimer::singleShot(3000, this, &MainWindow::checkForUpdates);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
  setWindowTitle("VEIL VPN Client");
  setMinimumSize(480, 720);
  resize(480, 720);

  // Set window icon from embedded resources
  setWindowIcon(QIcon(":/icons/icon_disconnected.svg"));

  // Set window flags for modern appearance
  setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);

  // Add widgets to stacked widget
  stackedWidget_->addWidget(connectionWidget_);
  stackedWidget_->addWidget(settingsWidget_);
  stackedWidget_->addWidget(diagnosticsWidget_);

  // Set central widget
  setCentralWidget(stackedWidget_);

  // Connect signals
  connect(connectionWidget_, &ConnectionWidget::settingsRequested,
          this, &MainWindow::showSettingsView);
  connect(settingsWidget_, &SettingsWidget::backRequested,
          this, &MainWindow::showConnectionView);
  connect(diagnosticsWidget_, &DiagnosticsWidget::backRequested,
          this, &MainWindow::showConnectionView);

  // Update connection widget when settings are saved
  connect(settingsWidget_, &SettingsWidget::settingsSaved,
          connectionWidget_, &ConnectionWidget::loadServerSettings);
}

void MainWindow::setupIpcConnections() {
  // Connect connection widget signals to IPC manager
  connect(connectionWidget_, &ConnectionWidget::connectRequested, this, [this]() {
    // Get server address and port from settings
    QSettings settings("VEIL", "VPN Client");
    QString serverAddress = settings.value("server/address", "vpn.example.com").toString();
    uint16_t serverPort = static_cast<uint16_t>(settings.value("server/port", 4433).toInt());

    ipcManager_->sendConnect(serverAddress, serverPort);
  });

  connect(connectionWidget_, &ConnectionWidget::disconnectRequested, this, [this]() {
    ipcManager_->sendDisconnect();
  });

  // Connect IPC manager signals to UI widgets
  connect(ipcManager_.get(), &IpcClientManager::connectionStateChanged,
          this, [this](ipc::ConnectionState state) {
    // Convert IPC state to GUI state
    ConnectionState guiState;
    switch (state) {
      case ipc::ConnectionState::kDisconnected:
        guiState = ConnectionState::kDisconnected;
        updateTrayIcon(TrayConnectionState::kDisconnected);
        break;
      case ipc::ConnectionState::kConnecting:
        guiState = ConnectionState::kConnecting;
        updateTrayIcon(TrayConnectionState::kConnecting);
        break;
      case ipc::ConnectionState::kConnected:
        guiState = ConnectionState::kConnected;
        updateTrayIcon(TrayConnectionState::kConnected);
        break;
      case ipc::ConnectionState::kReconnecting:
        guiState = ConnectionState::kReconnecting;
        updateTrayIcon(TrayConnectionState::kConnecting);
        break;
      case ipc::ConnectionState::kError:
        guiState = ConnectionState::kError;
        updateTrayIcon(TrayConnectionState::kError);
        break;
    }
    connectionWidget_->setConnectionState(guiState);
  });

  connect(ipcManager_.get(), &IpcClientManager::statusUpdated,
          this, [this](const ipc::ConnectionStatus& status) {
    if (!status.session_id.empty()) {
      connectionWidget_->setSessionId(QString::fromStdString(status.session_id));
    }
    if (!status.server_address.empty()) {
      connectionWidget_->setServerAddress(
          QString::fromStdString(status.server_address), status.server_port);
    }
    if (!status.error_message.empty()) {
      connectionWidget_->setErrorMessage(QString::fromStdString(status.error_message));
    }
  });

  connect(ipcManager_.get(), &IpcClientManager::metricsUpdated,
          this, [this](const ipc::ConnectionMetrics& metrics) {
    connectionWidget_->updateMetrics(
        static_cast<int>(metrics.latency_ms),
        metrics.tx_bytes_per_sec,
        metrics.rx_bytes_per_sec);
  });

  connect(ipcManager_.get(), &IpcClientManager::diagnosticsReceived,
          this, [this](const ipc::DiagnosticsData& diag) {
    diagnosticsWidget_->updateProtocolMetrics(
        diag.protocol.send_sequence,  // Using send_sequence as counter
        diag.protocol.send_sequence,
        diag.protocol.recv_sequence,
        diag.protocol.packets_sent,
        diag.protocol.packets_received,
        diag.protocol.packets_lost,
        diag.protocol.packets_retransmitted);

    diagnosticsWidget_->updateReassemblyStats(
        static_cast<uint32_t>(diag.reassembly.fragments_received),
        static_cast<uint32_t>(diag.reassembly.messages_reassembled),
        static_cast<uint32_t>(diag.reassembly.fragments_pending),
        static_cast<uint32_t>(diag.reassembly.reassembly_timeouts));

    diagnosticsWidget_->updateObfuscationProfile(
        diag.obfuscation.padding_enabled,
        diag.obfuscation.current_padding_size,
        QString::fromStdString(diag.obfuscation.timing_jitter_model),
        QString::fromStdString(diag.obfuscation.heartbeat_mode),
        diag.obfuscation.last_heartbeat_sec);
  });

  connect(ipcManager_.get(), &IpcClientManager::logEventReceived,
          this, [this](const ipc::LogEvent& event) {
    QString timestamp = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(event.timestamp_ms)).toString("hh:mm:ss");
    diagnosticsWidget_->addLogEntry(
        timestamp,
        QString::fromStdString(event.message),
        QString::fromStdString(event.level));
  });

  connect(ipcManager_.get(), &IpcClientManager::errorOccurred,
          this, [this](const QString& error, const QString& details) {
    connectionWidget_->setErrorMessage(error);
    statusBar()->showMessage(error + ": " + details, 5000);
  });

  connect(ipcManager_.get(), &IpcClientManager::daemonConnectionChanged,
          this, [this](bool connected) {
    diagnosticsWidget_->setDaemonConnected(connected);
    if (connected) {
      statusBar()->showMessage(tr("Connected to daemon"), 3000);
    } else {
      statusBar()->showMessage(tr("Disconnected from daemon"), 3000);
      // Reset UI to disconnected state
      connectionWidget_->setConnectionState(ConnectionState::kDisconnected);
      updateTrayIcon(TrayConnectionState::kDisconnected);
    }
  });

  // Connect diagnostics widget request to IPC manager
  connect(diagnosticsWidget_, &DiagnosticsWidget::diagnosticsRequested,
          this, [this]() {
    ipcManager_->requestDiagnostics();
  });
}

void MainWindow::setupMenuBar() {
  // Set menu bar style with new color scheme
  menuBar()->setStyleSheet(R"(
    QMenuBar {
      background-color: #0d1117;
      color: #f0f6fc;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      padding: 6px 12px;
    }
    QMenuBar::item {
      padding: 8px 16px;
      border-radius: 6px;
      margin: 2px;
    }
    QMenuBar::item:selected {
      background-color: rgba(255, 255, 255, 0.08);
    }
    QMenu {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 12px;
      padding: 8px;
    }
    QMenu::item {
      padding: 10px 24px;
      border-radius: 8px;
      margin: 2px 0;
    }
    QMenu::item:selected {
      background-color: #238636;
      color: white;
    }
    QMenu::separator {
      height: 1px;
      background-color: rgba(255, 255, 255, 0.06);
      margin: 8px 12px;
    }
  )");

  auto* viewMenu = menuBar()->addMenu(tr("&View"));

  auto* connectionAction = viewMenu->addAction(tr("&Connection"));
  connectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  connect(connectionAction, &QAction::triggered, this, &MainWindow::showConnectionView);

  auto* settingsViewAction = viewMenu->addAction(tr("&Settings"));
  settingsViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
  connect(settingsViewAction, &QAction::triggered, this, &MainWindow::showSettingsView);

  auto* diagnosticsAction = viewMenu->addAction(tr("&Diagnostics"));
  diagnosticsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
  connect(diagnosticsAction, &QAction::triggered, this, &MainWindow::showDiagnosticsView);

  viewMenu->addSeparator();

  auto* minimizeAction = viewMenu->addAction(tr("&Minimize to Tray"));
  minimizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(minimizeAction, &QAction::triggered, this, [this]() {
    if (trayIcon_ && trayIcon_->isVisible()) {
      hide();
    }
  });

  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  auto* aboutAction = helpMenu->addAction(tr("&About VEIL"));
  aboutAction->setShortcut(QKeySequence(Qt::Key_F1));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

  helpMenu->addAction(tr("Check for &Updates"), this, &MainWindow::checkForUpdates);

  // Global shortcuts for quick connect/disconnect
  auto* quickConnectShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
  connect(quickConnectShortcut, &QShortcut::activated, this, [this]() {
    if (currentTrayState_ == TrayConnectionState::kDisconnected ||
        currentTrayState_ == TrayConnectionState::kError) {
      onQuickConnect();
    }
  });

  auto* quickDisconnectShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
  connect(quickDisconnectShortcut, &QShortcut::activated, this, [this]() {
    if (currentTrayState_ == TrayConnectionState::kConnected ||
        currentTrayState_ == TrayConnectionState::kConnecting) {
      onQuickDisconnect();
    }
  });
}

void MainWindow::setupStatusBar() {
  statusBar()->setStyleSheet(R"(
    QStatusBar {
      background-color: #0d1117;
      color: #8b949e;
      border-top: 1px solid rgba(255, 255, 255, 0.06);
      padding: 6px 12px;
      font-size: 12px;
    }
    QStatusBar::item {
      border: none;
    }
  )");

  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::applyDarkTheme() {
  // Apply comprehensive dark theme stylesheet from theme.h
  setStyleSheet(getDarkThemeStylesheet());

  // Additional window-specific styles
  QString windowStyle = R"(
    QMainWindow {
      background-color: #0d1117;
    }
    QStackedWidget {
      background-color: #0d1117;
    }
  )";

  // Append to existing stylesheet
  setStyleSheet(styleSheet() + windowStyle);
}

void MainWindow::showConnectionView() {
  stackedWidget_->setCurrentWidgetAnimated(stackedWidget_->indexOf(connectionWidget_));
  statusBar()->showMessage(tr("Connection"));
}

void MainWindow::showSettingsView() {
  stackedWidget_->setCurrentWidgetAnimated(stackedWidget_->indexOf(settingsWidget_));
  statusBar()->showMessage(tr("Settings"));
}

void MainWindow::showDiagnosticsView() {
  stackedWidget_->setCurrentWidgetAnimated(stackedWidget_->indexOf(diagnosticsWidget_));
  statusBar()->showMessage(tr("Diagnostics"));
}

void MainWindow::showAboutDialog() {
  // Create a modern about dialog
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(tr("About VEIL"));
  dialog->setModal(true);
  dialog->setFixedSize(420, 380);

  dialog->setStyleSheet(R"(
    QDialog {
      background-color: #0d1117;
      color: #f0f6fc;
    }
    QLabel {
      color: #f0f6fc;
    }
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 10px;
      padding: 12px 32px;
      color: white;
      font-weight: 600;
      font-size: 14px;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
  )");

  auto* layout = new QVBoxLayout(dialog);
  layout->setSpacing(20);
  layout->setContentsMargins(40, 40, 40, 40);

  // Logo placeholder
  auto* logoWidget = new QWidget(dialog);
  logoWidget->setFixedSize(64, 64);
  logoWidget->setStyleSheet(R"(
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #238636, stop:1 #3fb950);
    border-radius: 16px;
  )");
  layout->addWidget(logoWidget, 0, Qt::AlignCenter);

  auto* titleLabel = new QLabel("VEIL VPN", dialog);
  titleLabel->setStyleSheet("font-size: 28px; font-weight: 700; color: #f0f6fc; letter-spacing: 2px;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* versionLabel = new QLabel(QString("Version %1").arg(veil::kVersionString), dialog);
  versionLabel->setStyleSheet(R"(
    color: #8b949e;
    font-size: 14px;
    padding: 4px 16px;
    background: rgba(255, 255, 255, 0.04);
    border-radius: 12px;
  )");
  versionLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(versionLabel, 0, Qt::AlignCenter);

  layout->addSpacing(8);

  auto* descLabel = new QLabel(
      "A secure UDP-based VPN client with\n"
      "DPI evasion capabilities.\n\n"
      "Modern cryptography (X25519, ChaCha20-Poly1305)\n"
      "Advanced traffic morphing techniques",
      dialog);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("color: #8b949e; font-size: 14px; line-height: 1.6;");
  descLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(descLabel);

  layout->addStretch();

  auto* closeButton = new QPushButton(tr("Close"), dialog);
  connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeButton, 0, Qt::AlignCenter);

  dialog->exec();
  dialog->deleteLater();
}

void MainWindow::setupSystemTray() {
  // Check if system tray is available
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    minimizeToTray_ = false;
    return;
  }

  // Create tray icon
  trayIcon_ = new QSystemTrayIcon(this);
  trayIcon_->setIcon(QIcon(":/icons/icon_disconnected.svg"));
  trayIcon_->setToolTip("VEIL VPN - Disconnected");

  // Create tray menu
  trayMenu_ = new QMenu(this);
  trayMenu_->setStyleSheet(R"(
    QMenu {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      padding: 8px;
    }
    QMenu::item {
      padding: 10px 24px;
      border-radius: 6px;
      color: #f0f6fc;
    }
    QMenu::item:selected {
      background-color: #238636;
    }
    QMenu::separator {
      height: 1px;
      background-color: rgba(255, 255, 255, 0.08);
      margin: 8px 12px;
    }
  )");

  // Status label (non-clickable)
  auto* statusAction = trayMenu_->addAction("Not Connected");
  statusAction->setEnabled(false);

  trayMenu_->addSeparator();

  // Connect/Disconnect actions
  trayConnectAction_ = trayMenu_->addAction(tr("Connect"));
  connect(trayConnectAction_, &QAction::triggered, this, &MainWindow::onQuickConnect);

  trayDisconnectAction_ = trayMenu_->addAction(tr("Disconnect"));
  trayDisconnectAction_->setEnabled(false);
  connect(trayDisconnectAction_, &QAction::triggered, this, &MainWindow::onQuickDisconnect);

  trayMenu_->addSeparator();

  // Show window action
  auto* showAction = trayMenu_->addAction(tr("Show Window"));
  connect(showAction, &QAction::triggered, this, [this]() {
    show();
    raise();
    activateWindow();
  });

  // Settings action
  auto* settingsAction = trayMenu_->addAction(tr("Settings"));
  connect(settingsAction, &QAction::triggered, this, [this]() {
    show();
    raise();
    activateWindow();
    showSettingsView();
  });

  trayMenu_->addSeparator();

  // Quit action
  auto* quitAction = trayMenu_->addAction(tr("Quit"));
  connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

  trayIcon_->setContextMenu(trayMenu_);

  // Connect activation signal
  connect(trayIcon_, &QSystemTrayIcon::activated,
          this, &MainWindow::onTrayIconActivated);

  // Show tray icon
  trayIcon_->show();
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
  switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
      // Show/hide window on click
      if (isVisible()) {
        hide();
      } else {
        show();
        raise();
        activateWindow();
      }
      break;
    default:
      break;
  }
}

void MainWindow::onQuickConnect() {
  // Delegate to connection widget
  connectionWidget_->onConnectClicked();
  updateTrayIcon(TrayConnectionState::kConnecting);
}

void MainWindow::onQuickDisconnect() {
  // Delegate to connection widget
  connectionWidget_->onConnectClicked();  // Toggle disconnect
  updateTrayIcon(TrayConnectionState::kDisconnected);
}

void MainWindow::updateTrayIcon(TrayConnectionState state) {
  if (!trayIcon_) return;

  currentTrayState_ = state;
  QString iconPath;
  QString tooltip;
  bool connectEnabled = true;
  bool disconnectEnabled = false;

  switch (state) {
    case TrayConnectionState::kDisconnected:
      iconPath = ":/icons/icon_disconnected.svg";
      tooltip = "VEIL VPN - Disconnected";
      connectEnabled = true;
      disconnectEnabled = false;
      break;
    case TrayConnectionState::kConnecting:
      iconPath = ":/icons/icon_connecting.svg";
      tooltip = "VEIL VPN - Connecting...";
      connectEnabled = false;
      disconnectEnabled = true;
      break;
    case TrayConnectionState::kConnected:
      iconPath = ":/icons/icon_connected.svg";
      tooltip = "VEIL VPN - Connected";
      connectEnabled = false;
      disconnectEnabled = true;
      break;
    case TrayConnectionState::kError:
      iconPath = ":/icons/icon_error.svg";
      tooltip = "VEIL VPN - Connection Error";
      connectEnabled = true;
      disconnectEnabled = false;
      break;
  }

  trayIcon_->setIcon(QIcon(iconPath));
  trayIcon_->setToolTip(tooltip);

  if (trayConnectAction_) {
    trayConnectAction_->setEnabled(connectEnabled);
  }
  if (trayDisconnectAction_) {
    trayDisconnectAction_->setEnabled(disconnectEnabled);
  }

  // Update the status label in the menu
  if (trayMenu_ && !trayMenu_->actions().isEmpty()) {
    auto* statusAction = trayMenu_->actions().first();
    switch (state) {
      case TrayConnectionState::kDisconnected:
        statusAction->setText("Not Connected");
        break;
      case TrayConnectionState::kConnecting:
        statusAction->setText("Connecting...");
        break;
      case TrayConnectionState::kConnected:
        statusAction->setText("Connected");
        break;
      case TrayConnectionState::kError:
        statusAction->setText("Connection Error");
        break;
    }
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (minimizeToTray_ && trayIcon_ && trayIcon_->isVisible()) {
    // Minimize to tray instead of closing
    hide();
    trayIcon_->showMessage(
        "VEIL VPN",
        "Application minimized to system tray. Click the icon to restore.",
        QSystemTrayIcon::Information,
        2000);
    event->ignore();
  } else {
    event->accept();
  }
}

void MainWindow::setupUpdateChecker() {
  connect(updateChecker_.get(), &UpdateChecker::updateAvailable,
          this, &MainWindow::onUpdateAvailable);
  connect(updateChecker_.get(), &UpdateChecker::noUpdateAvailable,
          this, &MainWindow::onNoUpdateAvailable);
  connect(updateChecker_.get(), &UpdateChecker::checkFailed,
          this, &MainWindow::onUpdateCheckFailed);
}

void MainWindow::checkForUpdates() {
  statusBar()->showMessage(tr("Checking for updates..."));
  updateChecker_->checkForUpdates();
}

void MainWindow::onUpdateAvailable(const UpdateInfo& info) {
  statusBar()->showMessage(tr("Update available: v%1").arg(info.version), 5000);

  // Show update notification dialog
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Update Available"));
  dialog->setModal(true);
  dialog->setFixedSize(450, 300);

  dialog->setStyleSheet(R"(
    QDialog {
      background-color: #0d1117;
      color: #f0f6fc;
    }
    QLabel {
      color: #f0f6fc;
    }
    QPushButton {
      border: none;
      border-radius: 10px;
      padding: 12px 24px;
      color: white;
      font-weight: 600;
      font-size: 13px;
    }
    QPushButton#downloadBtn {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
    }
    QPushButton#downloadBtn:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
    QPushButton#laterBtn {
      background: rgba(255, 255, 255, 0.08);
      color: #8b949e;
    }
    QPushButton#laterBtn:hover {
      background: rgba(255, 255, 255, 0.12);
    }
  )");

  auto* layout = new QVBoxLayout(dialog);
  layout->setSpacing(16);
  layout->setContentsMargins(32, 32, 32, 32);

  // Title
  auto* titleLabel = new QLabel(tr("A new version is available!"), dialog);
  titleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #f0f6fc;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  // Version info
  auto* versionLabel = new QLabel(
      QString("Current version: %1\nNew version: %2")
          .arg(UpdateChecker::currentVersion(), info.version),
      dialog);
  versionLabel->setStyleSheet("font-size: 14px; color: #8b949e; line-height: 1.6;");
  versionLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(versionLabel);

  // Release notes (if available)
  if (!info.releaseNotes.isEmpty()) {
    auto* notesLabel = new QLabel(info.releaseNotes.left(200) + "...", dialog);
    notesLabel->setStyleSheet(R"(
      font-size: 12px;
      color: #8b949e;
      padding: 12px;
      background: rgba(255, 255, 255, 0.04);
      border-radius: 8px;
    )");
    notesLabel->setWordWrap(true);
    layout->addWidget(notesLabel);
  }

  layout->addStretch();

  // Buttons
  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(12);

  auto* laterBtn = new QPushButton(tr("Later"), dialog);
  laterBtn->setObjectName("laterBtn");
  connect(laterBtn, &QPushButton::clicked, dialog, &QDialog::reject);
  buttonLayout->addWidget(laterBtn);

  auto* downloadBtn = new QPushButton(tr("Download Update"), dialog);
  downloadBtn->setObjectName("downloadBtn");
  connect(downloadBtn, &QPushButton::clicked, dialog, [info, dialog]() {
    QDesktopServices::openUrl(QUrl(info.downloadUrl));
    dialog->accept();
  });
  buttonLayout->addWidget(downloadBtn);

  layout->addLayout(buttonLayout);

  dialog->exec();
  dialog->deleteLater();
}

void MainWindow::onNoUpdateAvailable() {
  statusBar()->showMessage(tr("You have the latest version"), 3000);
}

void MainWindow::onUpdateCheckFailed(const QString& error) {
  statusBar()->showMessage(tr("Update check failed: %1").arg(error), 5000);
}

}  // namespace veil::gui
