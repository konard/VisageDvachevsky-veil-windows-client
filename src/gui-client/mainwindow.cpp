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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStringList>
#include <QDebug>
#include <QProgressDialog>

#include "common/gui/theme.h"
#include "common/ipc/ipc_protocol.h"
#include "common/version.h"
#include "connection_widget.h"
#include "diagnostics_widget.h"
#include "ipc_client_manager.h"
#include "settings_widget.h"
#include "update_checker.h"

#ifdef _WIN32
#include <chrono>
#include <thread>
#include "windows/service_manager.h"
#endif

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
  qDebug() << "MainWindow: Initializing GUI components...";
  setupUi();
  setupIpcConnections();
  setupMenuBar();
  setupStatusBar();
  setupSystemTray();
  setupUpdateChecker();
  applyDarkTheme();
  qDebug() << "MainWindow: GUI components initialized";

  // Attempt to connect to daemon
  // With SERVICE_AUTO_START, the service should already be running on Windows.
  // If not yet ready (e.g., delayed auto-start), retry after a brief delay.
  qDebug() << "MainWindow: Attempting to connect to VEIL daemon...";

  // Create progress dialog for initial connection
  auto* connectionProgress = new QProgressDialog(
      tr("Connecting to VEIL daemon..."),
      nullptr,  // No cancel button
      0, 0,     // Indeterminate progress (0-0 range)
      this);
  connectionProgress->setWindowModality(Qt::WindowModal);
  connectionProgress->setMinimumDuration(500);  // Show after 500ms if still connecting
  connectionProgress->setValue(0);

  if (!ipcManager_->connectToDaemon()) {
    qWarning() << "MainWindow: Failed to connect to daemon on first attempt";
#ifdef _WIN32
    // Service uses delayed auto-start, so it may still be starting.
    // Retry connection after a short delay before falling back to manual start.
    qDebug() << "MainWindow: Service may still be starting (delayed auto-start), retrying soon...";
    connectionProgress->setLabelText(tr("Waiting for VEIL service to start..."));
    statusBar()->showMessage(tr("Waiting for VEIL service to start..."));
    QTimer::singleShot(3000, this, [this, connectionProgress]() {
      qDebug() << "MainWindow: Retrying daemon connection...";
      if (!ipcManager_->connectToDaemon()) {
        qWarning() << "MainWindow: Retry failed, attempting to ensure service is running...";
        connectionProgress->setLabelText(tr("Starting VEIL service..."));
        if (ensureServiceRunning()) {
          qDebug() << "MainWindow: Service startup succeeded, waiting for IPC server to be ready...";
          // Wait for the service's IPC Named Pipe to be available before connecting
          connectionProgress->setLabelText(tr("Waiting for service to be ready..."));
          if (waitForServiceReady(5000)) {
            qDebug() << "MainWindow: Service IPC is ready, connecting...";
            connectionProgress->setLabelText(tr("Connecting to daemon..."));
            if (!ipcManager_->connectToDaemon()) {
              qWarning() << "MainWindow: Failed to connect to daemon after service ready";
              statusBar()->showMessage(tr("Failed to connect to daemon after service start"), 5000);
            } else {
              qDebug() << "MainWindow: Successfully connected to daemon after service start";
            }
          } else {
            qWarning() << "MainWindow: Timed out waiting for service IPC, attempting connection anyway...";
            if (!ipcManager_->connectToDaemon()) {
              qWarning() << "MainWindow: Failed to connect to daemon after timeout";
              statusBar()->showMessage(tr("Failed to connect to daemon after service start"), 5000);
            } else {
              qDebug() << "MainWindow: Successfully connected to daemon despite timeout";
            }
          }
        } else {
          qWarning() << "MainWindow: Failed to ensure service is running";
          statusBar()->showMessage(tr("Failed to start VEIL service - run as administrator"), 5000);
        }
      } else {
        qDebug() << "MainWindow: Successfully connected to daemon on retry";
        statusBar()->showMessage(tr("Connected to daemon"), 3000);
      }
      // Close the progress dialog
      connectionProgress->close();
      connectionProgress->deleteLater();
    });
#else
    statusBar()->showMessage(tr("Daemon not running - start veil-client first"), 5000);
    connectionProgress->close();
    connectionProgress->deleteLater();
#endif
  } else {
    qDebug() << "MainWindow: Successfully connected to daemon";
    connectionProgress->close();
    connectionProgress->deleteLater();
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

// Helper function to build ConnectionConfig from QSettings
static ipc::ConnectionConfig buildConnectionConfig() {
  qDebug() << "[MainWindow] ========================================";
  qDebug() << "[MainWindow] BUILDING CONNECTION CONFIGURATION";
  qDebug() << "[MainWindow] ========================================";

  QSettings settings("VEIL", "VPN Client");
  qDebug() << "[MainWindow] Loading settings from:" << settings.fileName();

  ipc::ConnectionConfig config;

  // Server configuration
  QString serverAddress = settings.value("server/address", "vpn.example.com").toString();
  int serverPort = settings.value("server/port", 4433).toInt();
  qDebug() << "[MainWindow] Server Configuration:";
  qDebug() << "[MainWindow]   Address:" << serverAddress << "(raw value from settings)";
  qDebug() << "[MainWindow]   Port:" << serverPort << "(raw value from settings)";

  config.server_address = serverAddress.toStdString();
  config.server_port = static_cast<uint16_t>(serverPort);

  // Cryptographic settings
  QString keyFile = settings.value("crypto/keyFile", "").toString();
  QString obfuscationSeedFile = settings.value("crypto/obfuscationSeedFile", "").toString();
  qDebug() << "[MainWindow] Cryptographic Settings:";
  qDebug() << "[MainWindow]   Key File:" << (keyFile.isEmpty() ? "<not set>" : keyFile);
  qDebug() << "[MainWindow]   Obfuscation Seed File:" << (obfuscationSeedFile.isEmpty() ? "<not set>" : obfuscationSeedFile);

  // Verify key file exists
  if (!keyFile.isEmpty()) {
    QFileInfo keyFileInfo(keyFile);
    if (keyFileInfo.exists() && keyFileInfo.isFile()) {
      qDebug() << "[MainWindow]   Key file exists: YES";
      qDebug() << "[MainWindow]   Key file size:" << keyFileInfo.size() << "bytes";
    } else {
      qWarning() << "[MainWindow]   Key file exists: NO - file not found or not accessible";
    }
  }

  // Verify obfuscation seed file exists
  if (!obfuscationSeedFile.isEmpty()) {
    QFileInfo seedFileInfo(obfuscationSeedFile);
    if (seedFileInfo.exists() && seedFileInfo.isFile()) {
      qDebug() << "[MainWindow]   Obfuscation seed file exists: YES";
      qDebug() << "[MainWindow]   Obfuscation seed file size:" << seedFileInfo.size() << "bytes";
    } else {
      qWarning() << "[MainWindow]   Obfuscation seed file exists: NO - file not found or not accessible";
    }
  }

  config.key_file = keyFile.toStdString();
  config.obfuscation_seed_file = obfuscationSeedFile.toStdString();

  // TUN interface settings
  QString tunDeviceName = settings.value("tun/deviceName", "veil0").toString();
  QString tunIpAddress = settings.value("tun/ipAddress", "10.8.0.2").toString();
  QString tunNetmask = settings.value("tun/netmask", "255.255.255.0").toString();
  int tunMtu = settings.value("tun/mtu", 1400).toInt();

  qDebug() << "[MainWindow] TUN Interface Settings:";
  qDebug() << "[MainWindow]   Device Name:" << tunDeviceName;
  qDebug() << "[MainWindow]   IP Address:" << tunIpAddress;
  qDebug() << "[MainWindow]   Netmask:" << tunNetmask;
  qDebug() << "[MainWindow]   MTU:" << tunMtu;

  config.tun_device_name = tunDeviceName.toStdString();
  config.tun_ip_address = tunIpAddress.toStdString();
  config.tun_netmask = tunNetmask.toStdString();
  config.tun_mtu = static_cast<uint16_t>(tunMtu);

  // Routing settings
  bool routeAllTraffic = settings.value("routing/routeAllTraffic", true).toBool();
  QString customRoutes = settings.value("routing/customRoutes", "").toString();

  qDebug() << "[MainWindow] Routing Settings:";
  qDebug() << "[MainWindow]   Route All Traffic:" << (routeAllTraffic ? "YES" : "NO");

  config.route_all_traffic = routeAllTraffic;

  if (!customRoutes.isEmpty()) {
    QStringList routeList = customRoutes.split(",", Qt::SkipEmptyParts);
    qDebug() << "[MainWindow]   Custom Routes (" << routeList.size() << "):";
    for (const QString& route : routeList) {
      QString trimmedRoute = route.trimmed();
      qDebug() << "[MainWindow]     -" << trimmedRoute;
      config.custom_routes.push_back(trimmedRoute.toStdString());
    }
  } else {
    qDebug() << "[MainWindow]   Custom Routes: <none>";
  }

  // Connection settings
  bool autoReconnect = settings.value("connection/autoReconnect", true).toBool();
  int reconnectInterval = settings.value("connection/reconnectInterval", 5).toInt();
  int maxReconnectAttempts = settings.value("connection/maxReconnectAttempts", 5).toInt();

  qDebug() << "[MainWindow] Connection Settings:";
  qDebug() << "[MainWindow]   Auto Reconnect:" << (autoReconnect ? "YES" : "NO");
  qDebug() << "[MainWindow]   Reconnect Interval:" << reconnectInterval << "seconds";
  qDebug() << "[MainWindow]   Max Reconnect Attempts:" << maxReconnectAttempts;

  config.auto_reconnect = autoReconnect;
  config.reconnect_interval_sec = static_cast<uint32_t>(reconnectInterval);
  config.max_reconnect_attempts = static_cast<uint32_t>(maxReconnectAttempts);

  // Advanced settings
  bool enableObfuscation = settings.value("advanced/obfuscation", true).toBool();
  int dpiBypassMode = settings.value("dpi/mode", 0).toInt();

  qDebug() << "[MainWindow] Advanced Settings:";
  qDebug() << "[MainWindow]   Enable Obfuscation:" << (enableObfuscation ? "YES" : "NO");
  qDebug() << "[MainWindow]   DPI Bypass Mode:" << dpiBypassMode;

  config.enable_obfuscation = enableObfuscation;
  config.dpi_bypass_mode = static_cast<uint8_t>(dpiBypassMode);

  qDebug() << "[MainWindow] Configuration building complete";
  qDebug() << "[MainWindow] ========================================";

  return config;
}

void MainWindow::setupIpcConnections() {
  // Connect connection widget signals to IPC manager
  connect(connectionWidget_, &ConnectionWidget::connectRequested, this, [this]() {
    qDebug() << "[MainWindow] ========================================";
    qDebug() << "[MainWindow] CONNECT BUTTON CLICKED";
    qDebug() << "[MainWindow] ========================================";
    qDebug() << "[MainWindow] User requested VPN connection";

    // First check if daemon is connected, try to reconnect if not
    qDebug() << "[MainWindow] Checking daemon connection status...";

    if (!ipcManager_->isConnected()) {
      qWarning() << "[MainWindow] Daemon is NOT connected, attempting to connect...";

      if (!ipcManager_->connectToDaemon()) {
        qWarning() << "[MainWindow] Failed to connect to daemon";
#ifdef _WIN32
        // On Windows, try to auto-start the service
        qDebug() << "[MainWindow] Platform: Windows - attempting to ensure service is running";

        if (!ensureServiceRunning()) {
          qWarning() << "[MainWindow] Failed to ensure service is running";
          connectionWidget_->setConnectionState(ConnectionState::kError);
          updateTrayIcon(TrayConnectionState::kError);
          return;
        }

        qDebug() << "[MainWindow] Service should be running now, waiting for IPC server to be ready...";

        // Wait for service IPC to be ready, then retry connection
        connectionWidget_->setConnectionState(ConnectionState::kConnecting);
        updateTrayIcon(TrayConnectionState::kConnecting);

        bool service_ready = waitForServiceReady(5000);
        if (!service_ready) {
          qWarning() << "[MainWindow] Timed out waiting for service IPC, attempting connection anyway...";
        }

        qDebug() << "[MainWindow] Retrying daemon connection after service startup...";
        if (!ipcManager_->connectToDaemon()) {
          qWarning() << "[MainWindow] Failed to connect to daemon even after service start";
          connectionWidget_->setErrorMessage(
              tr("Failed to connect to daemon after service start."));
          connectionWidget_->setConnectionState(ConnectionState::kError);
          updateTrayIcon(TrayConnectionState::kError);
        } else {
          qDebug() << "[MainWindow] Successfully connected to daemon after service start";
          qDebug() << "[MainWindow] Now building and sending connection configuration...";

          // Now that we're connected, send the connect command with full config
          ipc::ConnectionConfig config = buildConnectionConfig();
          ipcManager_->sendConnect(config);
        }
        return;
#else
        // Failed to connect to daemon - show error
        qWarning() << "[MainWindow] Platform: Non-Windows - cannot auto-start daemon";
        connectionWidget_->setErrorMessage(
            tr("Cannot connect: VEIL daemon is not running.\n"
               "Please start the daemon first."));
        connectionWidget_->setConnectionState(ConnectionState::kError);
        updateTrayIcon(TrayConnectionState::kError);
        return;
#endif
      }
    } else {
      qDebug() << "[MainWindow] Daemon is already connected";
    }

    qDebug() << "[MainWindow] Building connection configuration from settings...";

    // Build full configuration from settings
    ipc::ConnectionConfig config = buildConnectionConfig();

    qDebug() << "[MainWindow] Validating configuration...";

    // Validate key file exists if specified
    if (!config.key_file.empty()) {
      qDebug() << "[MainWindow] Checking key file:" << QString::fromStdString(config.key_file);

      QFileInfo keyFileInfo(QString::fromStdString(config.key_file));
      if (!keyFileInfo.exists() || !keyFileInfo.isFile()) {
        qWarning() << "[MainWindow] Key file validation FAILED - file does not exist or is not a file";
        qWarning() << "[MainWindow]   Exists:" << keyFileInfo.exists();
        qWarning() << "[MainWindow]   Is File:" << keyFileInfo.isFile();
        qWarning() << "[MainWindow]   Path:" << keyFileInfo.absoluteFilePath();

        connectionWidget_->setErrorMessage(
            tr("Pre-shared key file not found: %1\n"
               "Please configure a valid key file in Settings.")
                .arg(QString::fromStdString(config.key_file)));
        connectionWidget_->setConnectionState(ConnectionState::kError);
        updateTrayIcon(TrayConnectionState::kError);
        return;
      } else {
        qDebug() << "[MainWindow] Key file validation PASSED";
      }
    } else {
      qWarning() << "[MainWindow] No key file configured (this may cause connection issues)";
    }

    qDebug() << "[MainWindow] Configuration validated successfully";
    qDebug() << "[MainWindow] Sending connection request to daemon via IPC...";

    if (!ipcManager_->sendConnect(config)) {
      // Failed to send connect command
      qWarning() << "[MainWindow] Failed to send connect command to daemon";
      connectionWidget_->setErrorMessage(
          tr("Failed to send connect command to daemon."));
      connectionWidget_->setConnectionState(ConnectionState::kError);
      updateTrayIcon(TrayConnectionState::kError);
    } else {
      qDebug() << "[MainWindow] Connect command sent successfully, waiting for response...";
    }
  });

  connect(connectionWidget_, &ConnectionWidget::disconnectRequested, this, [this]() {
    qDebug() << "[MainWindow] ========================================";
    qDebug() << "[MainWindow] DISCONNECT BUTTON CLICKED";
    qDebug() << "[MainWindow] ========================================";
    qDebug() << "[MainWindow] User requested VPN disconnection";
    qDebug() << "[MainWindow] Sending disconnect request to daemon...";

    ipcManager_->sendDisconnect();
  });

  // Connect IPC manager signals to UI widgets
  connect(ipcManager_.get(), &IpcClientManager::connectionStateChanged,
          this, [this](ipc::ConnectionState state) {
    qDebug() << "[MainWindow] Connection state changed:" << static_cast<int>(state);

    // Convert IPC state to GUI state
    ConnectionState guiState = ConnectionState::kDisconnected;  // Default to disconnected
    switch (state) {
      case ipc::ConnectionState::kDisconnected:
        qDebug() << "[MainWindow] New state: DISCONNECTED";
        guiState = ConnectionState::kDisconnected;
        updateTrayIcon(TrayConnectionState::kDisconnected);
        break;
      case ipc::ConnectionState::kConnecting:
        qDebug() << "[MainWindow] New state: CONNECTING";
        guiState = ConnectionState::kConnecting;
        updateTrayIcon(TrayConnectionState::kConnecting);
        break;
      case ipc::ConnectionState::kConnected:
        qDebug() << "[MainWindow] New state: CONNECTED";
        guiState = ConnectionState::kConnected;
        updateTrayIcon(TrayConnectionState::kConnected);
        break;
      case ipc::ConnectionState::kReconnecting:
        qDebug() << "[MainWindow] New state: RECONNECTING";
        guiState = ConnectionState::kReconnecting;
        updateTrayIcon(TrayConnectionState::kConnecting);
        break;
      case ipc::ConnectionState::kError:
        qDebug() << "[MainWindow] New state: ERROR";
        guiState = ConnectionState::kError;
        updateTrayIcon(TrayConnectionState::kError);
        break;
    }

    qDebug() << "[MainWindow] Updating UI to reflect new state";
    connectionWidget_->setConnectionState(guiState);
  });

  connect(ipcManager_.get(), &IpcClientManager::statusUpdated,
          this, [this](const ipc::ConnectionStatus& status) {
    qDebug() << "[MainWindow] Received status update from daemon";

    if (!status.session_id.empty()) {
      qDebug() << "[MainWindow]   Session ID:" << QString::fromStdString(status.session_id);
      connectionWidget_->setSessionId(QString::fromStdString(status.session_id));
    }

    if (!status.server_address.empty()) {
      qDebug() << "[MainWindow]   Server Address:" << QString::fromStdString(status.server_address)
               << ":" << status.server_port;
      connectionWidget_->setServerAddress(
          QString::fromStdString(status.server_address), status.server_port);
    }

    if (!status.error_message.empty()) {
      qWarning() << "[MainWindow]   Error Message:" << QString::fromStdString(status.error_message);
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
    qWarning() << "[MainWindow] ========================================";
    qWarning() << "[MainWindow] ERROR OCCURRED";
    qWarning() << "[MainWindow] ========================================";
    qWarning() << "[MainWindow] Error:" << error;
    qWarning() << "[MainWindow] Details:" << details;
    qWarning() << "[MainWindow] ========================================";

    connectionWidget_->setErrorMessage(error);
    statusBar()->showMessage(error + ": " + details, 5000);
  });

  connect(ipcManager_.get(), &IpcClientManager::daemonConnectionChanged,
          this, [this](bool connected) {
    qDebug() << "[MainWindow] Daemon connection status changed:" << (connected ? "CONNECTED" : "DISCONNECTED");

    diagnosticsWidget_->setDaemonConnected(connected);

    if (connected) {
      qDebug() << "[MainWindow] Daemon is now connected";
      statusBar()->showMessage(tr("Connected to daemon"), 3000);
    } else {
      qWarning() << "[MainWindow] Daemon is now disconnected";
      statusBar()->showMessage(tr("Disconnected from daemon"), 3000);

      // Reset UI to disconnected state
      qDebug() << "[MainWindow] Resetting UI to disconnected state";
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

  // Create progress dialog for update check
  auto* progress = new QProgressDialog(
      tr("Checking for updates..."),
      tr("Cancel"),
      0, 0,  // Indeterminate progress
      this);
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(500);  // Show after 500ms if still checking
  progress->setValue(0);

  // Connect to cancel button
  connect(progress, &QProgressDialog::canceled, [progress]() {
    progress->close();
    progress->deleteLater();
  });

  // Connect to update checker signals to close progress dialog
  connect(updateChecker_.get(), &UpdateChecker::updateAvailable, progress, [progress](const UpdateInfo&) {
    progress->close();
    progress->deleteLater();
  }, Qt::UniqueConnection);

  connect(updateChecker_.get(), &UpdateChecker::noUpdateAvailable, progress, [progress]() {
    progress->close();
    progress->deleteLater();
  }, Qt::UniqueConnection);

  connect(updateChecker_.get(), &UpdateChecker::checkFailed, progress, [progress](const QString&) {
    progress->close();
    progress->deleteLater();
  }, Qt::UniqueConnection);

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

#ifdef _WIN32
bool MainWindow::ensureServiceRunning() {
  using namespace veil::windows;

  qDebug() << "ensureServiceRunning: Checking VEIL service status...";

  // Check if service is already running
  if (ServiceManager::is_running()) {
    qDebug() << "ensureServiceRunning: Service is already running";
    return true;
  }

  qDebug() << "ensureServiceRunning: Service is not running";

  // Check if service is installed
  if (!ServiceManager::is_installed()) {
    qDebug() << "ensureServiceRunning: Service is not installed, attempting automatic installation...";
    // Service not installed - attempt to install it automatically
    statusBar()->showMessage(tr("VEIL service not found, attempting to install..."));

    // Check if we have admin privileges
    if (!elevation::is_elevated()) {
      qDebug() << "ensureServiceRunning: Application is not elevated, requesting elevation for installation...";

      // Try to install with elevation
      QMessageBox msgBox(this);
      msgBox.setIcon(QMessageBox::Information);
      msgBox.setWindowTitle(tr("VEIL Service Installation Required"));
      msgBox.setText(tr("The VEIL VPN service is not installed and needs to be set up.\n\n"
                       "Administrator privileges are required to install the service."));
      msgBox.setInformativeText(tr("Would you like to install the service now?"));
      msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      msgBox.setDefaultButton(QMessageBox::Yes);

      if (msgBox.exec() == QMessageBox::Yes) {
        // Get path to veil-service.exe
        QString appDir = QCoreApplication::applicationDirPath();
        QString servicePath = QDir(appDir).filePath("veil-service.exe");

        qDebug() << "ensureServiceRunning: Application directory:" << appDir;
        qDebug() << "ensureServiceRunning: Service executable path:" << servicePath;

        if (QFile::exists(servicePath)) {
          qDebug() << "ensureServiceRunning: Service executable found, requesting elevation...";

          // Create progress dialog for service installation
          auto* progress = new QProgressDialog(
              tr("Installing VEIL service..."),
              nullptr,  // No cancel button
              0, 0,     // Indeterminate progress
              this);
          progress->setWindowModality(Qt::WindowModal);
          progress->setMinimumDuration(0);  // Show immediately
          progress->setValue(0);

          // Request elevation and install
          statusBar()->showMessage(tr("Installing VEIL service..."));
          if (elevation::run_elevated(servicePath.toStdString(), "--install", true)) {
            qDebug() << "ensureServiceRunning: Elevation succeeded, service installation requested";

            progress->setLabelText(tr("Verifying service installation..."));
            QApplication::processEvents();  // Update UI

            // Verify installation succeeded
            if (ServiceManager::is_installed()) {
              qDebug() << "ensureServiceRunning: Service installation verified, attempting to start...";

              progress->setLabelText(tr("Starting VEIL service..."));
              QApplication::processEvents();  // Update UI

              // Try to start the service and wait for it to be ready
              std::string error;
              if (ServiceManager::start_and_wait(error)) {
                qDebug() << "ensureServiceRunning: Service started and is now running";
                statusBar()->showMessage(tr("VEIL service started successfully"), 3000);
                progress->close();
                progress->deleteLater();
                return true;
              } else {
                qWarning() << "ensureServiceRunning: Failed to start service after installation:" << QString::fromStdString(error);
              }
            } else {
              qWarning() << "ensureServiceRunning: Service installation verification failed";
            }

            progress->close();
            progress->deleteLater();
          } else {
            qWarning() << "ensureServiceRunning: Elevation failed or was denied";
            progress->close();
            progress->deleteLater();

            QMessageBox::warning(
                this,
                tr("Service Installation Failed"),
                tr("Failed to install the VEIL service.\n\n"
                   "Please ensure you have administrator privileges and try again."));
            return false;
          }
        } else {
          qWarning() << "ensureServiceRunning: Service executable not found at:" << servicePath;
          QMessageBox::warning(
              this,
              tr("Service Executable Not Found"),
              tr("Could not find veil-service.exe in the application directory.\n\n"
                 "Please reinstall VEIL VPN to ensure all components are present."));
          return false;
        }
      } else {
        qDebug() << "ensureServiceRunning: User declined service installation";
        return false;
      }
    } else {
      qDebug() << "ensureServiceRunning: Application is already elevated, installing directly...";
      // We have admin privileges, try to install directly
      QString appDir = QCoreApplication::applicationDirPath();
      QString servicePath = QDir(appDir).filePath("veil-service.exe");

      qDebug() << "ensureServiceRunning: Service executable path:" << servicePath;

      if (QFile::exists(servicePath)) {
        qDebug() << "ensureServiceRunning: Service executable found, installing...";

        // Create progress dialog for service installation
        auto* progress = new QProgressDialog(
            tr("Installing VEIL service..."),
            nullptr,  // No cancel button
            0, 0,     // Indeterminate progress
            this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);  // Show immediately
        progress->setValue(0);

        std::string error;
        if (ServiceManager::install(servicePath.toStdString(), error)) {
          qDebug() << "ensureServiceRunning: Service installed successfully, attempting to start...";
          statusBar()->showMessage(tr("VEIL service installed successfully"), 3000);

          progress->setLabelText(tr("Starting VEIL service..."));
          QApplication::processEvents();  // Update UI

          // Try to start the service and wait for it to be ready
          if (ServiceManager::start_and_wait(error)) {
            qDebug() << "ensureServiceRunning: Service started and is now running";
            statusBar()->showMessage(tr("VEIL service started successfully"), 3000);
            progress->close();
            progress->deleteLater();
            return true;
          } else {
            qWarning() << "ensureServiceRunning: Failed to start service after installation:" << QString::fromStdString(error);
          }

          progress->close();
          progress->deleteLater();
        } else {
          qWarning() << "ensureServiceRunning: Service installation failed:" << QString::fromStdString(error);
          progress->close();
          progress->deleteLater();

          QMessageBox::warning(
              this,
              tr("Service Installation Failed"),
              tr("Failed to install the VEIL service: %1").arg(QString::fromStdString(error)));
          return false;
        }
      } else {
        qWarning() << "ensureServiceRunning: Service executable not found at:" << servicePath;
        QMessageBox::warning(
            this,
            tr("Service Executable Not Found"),
            tr("Could not find veil-service.exe in the application directory.\n\n"
               "Please reinstall VEIL VPN to ensure all components are present."));
        return false;
      }
    }
  }

  // Service is installed but not running - try to start it and wait for it to be ready
  qDebug() << "ensureServiceRunning: Service is installed but not running, attempting to start...";

  // Create progress dialog for service start
  auto* progress = new QProgressDialog(
      tr("Starting VEIL service..."),
      nullptr,  // No cancel button
      0, 0,     // Indeterminate progress
      this);
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(0);  // Show immediately
  progress->setValue(0);

  statusBar()->showMessage(tr("Starting VEIL service..."));

  std::string error;
  // Use start_and_wait() to ensure the service is fully running before returning
  // This prevents race conditions where the GUI tries to connect before the
  // service has created the IPC Named Pipe
  if (ServiceManager::start_and_wait(error)) {
    qDebug() << "ensureServiceRunning: Service started and is now running";
    statusBar()->showMessage(tr("VEIL service started successfully"), 3000);
    progress->close();
    progress->deleteLater();
    return true;
  }

  progress->close();
  progress->deleteLater();

  qWarning() << "ensureServiceRunning: Failed to start service:" << QString::fromStdString(error);

  // Failed to start - might need elevation
  if (error.find("Access is denied") != std::string::npos ||
      error.find("5") != std::string::npos) {  // ERROR_ACCESS_DENIED = 5
    qWarning() << "ensureServiceRunning: Access denied error, need administrator privileges";
    // Need admin rights to start service - inform user
    QMessageBox::warning(
        this,
        tr("Administrator Rights Required"),
        tr("Failed to start the VEIL service.\n\n"
           "Please run this application as Administrator,\n"
           "or start the service manually from Windows Services."));
  } else {
    qWarning() << "ensureServiceRunning: Service start failed with error:" << QString::fromStdString(error);
    QMessageBox::warning(
        this,
        tr("Service Start Failed"),
        tr("Failed to start the VEIL service:\n%1").arg(QString::fromStdString(error)));
  }

  return false;
}

bool MainWindow::waitForServiceReady(int timeout_ms) {
  static constexpr const char* kReadyEventName = "Global\\VEIL_SERVICE_READY";
  static constexpr const char* kPipeName = "\\\\.\\pipe\\veil-client";
  static constexpr int kPollIntervalMs = 100;

  qDebug() << "waitForServiceReady: Waiting up to" << timeout_ms << "ms for service IPC to be ready...";

  // Phase 1: Try the Windows Event signal (set by the service after IPC server starts)
  HANDLE event = OpenEventA(SYNCHRONIZE, FALSE, kReadyEventName);
  if (event) {
    qDebug() << "waitForServiceReady: Found service ready event, waiting for signal...";
    DWORD result = WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
    CloseHandle(event);
    if (result == WAIT_OBJECT_0) {
      qDebug() << "waitForServiceReady: Service ready event signaled - IPC server is ready";
      return true;
    }
    qWarning() << "waitForServiceReady: Wait on event timed out or failed (result:" << result << ")";
  } else {
    qDebug() << "waitForServiceReady: Service ready event not found, falling back to Named Pipe polling";
  }

  // Phase 2: Fall back to polling for Named Pipe existence
  // This handles the case where the service was already running (event already signaled
  // and possibly cleaned up) or the event could not be created.
  auto start = std::chrono::steady_clock::now();
  while (true) {
    if (WaitNamedPipeA(kPipeName, 0)) {
      qDebug() << "waitForServiceReady: Named Pipe is available - IPC server is ready";
      return true;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (elapsed_ms >= timeout_ms) {
      qWarning() << "waitForServiceReady: Timed out after" << elapsed_ms
                 << "ms waiting for Named Pipe";
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  }
}
#endif

}  // namespace veil::gui
