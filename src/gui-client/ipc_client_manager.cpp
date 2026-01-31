#include "ipc_client_manager.h"

#include <system_error>
#include <QDebug>

namespace veil::gui {

IpcClientManager::IpcClientManager(QObject* parent)
    : QObject(parent),
      client_(std::make_unique<ipc::IpcClient>()),
      pollTimer_(new QTimer(this)),
      reconnectTimer_(new QTimer(this)),
      heartbeatTimer_(new QTimer(this)) {
  qDebug() << "[IpcClientManager] Initializing IPC Client Manager";

  // Set up IPC message handler
  client_->on_message([this](const ipc::Message& msg) {
    handleMessage(msg);
  });

  // Set up connection change handler
  client_->on_connection_change([this](bool connected) {
    handleConnectionChange(connected);
  });

  // Set up polling timer to check for incoming messages
  // This is necessary because the IPC client uses non-blocking I/O
  connect(pollTimer_, &QTimer::timeout, this, &IpcClientManager::pollMessages);
  pollTimer_->setInterval(50);  // Poll every 50ms for responsive UI
  qDebug() << "[IpcClientManager] Poll timer interval:" << pollTimer_->interval() << "ms";

  // Set up reconnection timer
  connect(reconnectTimer_, &QTimer::timeout, this, &IpcClientManager::attemptReconnect);
  reconnectTimer_->setInterval(kReconnectIntervalMs);
  qDebug() << "[IpcClientManager] Reconnect timer interval:" << reconnectTimer_->interval() << "ms";

  // Set up heartbeat monitoring timer
  connect(heartbeatTimer_, &QTimer::timeout, this, &IpcClientManager::checkHeartbeatTimeout);
  heartbeatTimer_->setInterval(kHeartbeatCheckIntervalMs);
  qDebug() << "[IpcClientManager] Heartbeat check interval:" << heartbeatTimer_->interval() << "ms";
  qDebug() << "[IpcClientManager] Heartbeat timeout threshold:" << kHeartbeatTimeoutSec << "seconds";

  qDebug() << "[IpcClientManager] Initialization complete";
}

IpcClientManager::~IpcClientManager() {
  qDebug() << "[IpcClientManager] Shutting down IPC Client Manager";
  stopReconnectTimer();
  disconnect();
  qDebug() << "[IpcClientManager] Shutdown complete";
}

bool IpcClientManager::connectToDaemon() {
  qDebug() << "[IpcClientManager] Attempting to connect to daemon via IPC...";

  std::error_code ec;
  if (!client_->connect(ec)) {
    // Failed to connect - daemon may not be running
    qWarning() << "[IpcClientManager] Failed to connect to daemon. Error code:" << ec.value()
               << "Message:" << QString::fromStdString(ec.message());
    qWarning() << "[IpcClientManager] Daemon is likely not running or IPC socket is not available";

    // Start reconnection timer in background
    qDebug() << "[IpcClientManager] Starting reconnection timer for automatic retry";
    startReconnectTimer();

    emit errorOccurred(
        tr("Failed to connect to daemon"),
        tr("The VEIL client daemon may not be running. Please start veil-client first."));
    return false;
  }

  // Successfully connected - stop reconnection timer
  qDebug() << "[IpcClientManager] Successfully connected to daemon via IPC";
  stopReconnectTimer();
  daemonConnected_ = true;
  pollTimer_->start();
  qDebug() << "[IpcClientManager] Started message polling timer";

  // Start heartbeat monitoring
  lastHeartbeat_ = std::chrono::steady_clock::now();
  heartbeatTimer_->start();
  qDebug() << "[IpcClientManager] Started heartbeat monitoring";

  emit daemonConnectionChanged(true);
  return true;
}

void IpcClientManager::disconnect() {
  qDebug() << "[IpcClientManager] Disconnecting from daemon";
  pollTimer_->stop();
  qDebug() << "[IpcClientManager] Stopped polling timer";
  heartbeatTimer_->stop();
  qDebug() << "[IpcClientManager] Stopped heartbeat monitoring";
  client_->disconnect();
  qDebug() << "[IpcClientManager] IPC client disconnected";
  daemonConnected_ = false;
  emit daemonConnectionChanged(false);
  qDebug() << "[IpcClientManager] Daemon connection state updated to disconnected";
}

bool IpcClientManager::isConnected() const {
  return client_->is_connected();
}

bool IpcClientManager::sendConnect(const ipc::ConnectionConfig& config) {
  qDebug() << "[IpcClientManager] ========================================";
  qDebug() << "[IpcClientManager] SENDING CONNECT COMMAND";
  qDebug() << "[IpcClientManager] ========================================";

  if (!isConnected()) {
    qWarning() << "[IpcClientManager] Cannot send connect command - not connected to daemon";
    emit errorOccurred(
        tr("Not connected to daemon"),
        tr("Cannot send connect command - not connected to daemon."));
    return false;
  }

  // Log detailed configuration
  qDebug() << "[IpcClientManager] Connection Configuration:";
  qDebug() << "[IpcClientManager]   Server Address:" << QString::fromStdString(config.server_address);
  qDebug() << "[IpcClientManager]   Server Port:" << config.server_port;
  qDebug() << "[IpcClientManager]   Key File:" << QString::fromStdString(config.key_file);
  qDebug() << "[IpcClientManager]   Obfuscation Seed File:" << QString::fromStdString(config.obfuscation_seed_file);
  qDebug() << "[IpcClientManager]   TUN Device Name:" << QString::fromStdString(config.tun_device_name);
  qDebug() << "[IpcClientManager]   TUN IP Address:" << QString::fromStdString(config.tun_ip_address);
  qDebug() << "[IpcClientManager]   TUN Netmask:" << QString::fromStdString(config.tun_netmask);
  qDebug() << "[IpcClientManager]   TUN MTU:" << config.tun_mtu;
  qDebug() << "[IpcClientManager]   Route All Traffic:" << (config.route_all_traffic ? "yes" : "no");
  qDebug() << "[IpcClientManager]   Auto Reconnect:" << (config.auto_reconnect ? "yes" : "no");
  qDebug() << "[IpcClientManager]   Reconnect Interval:" << config.reconnect_interval_sec << "seconds";
  qDebug() << "[IpcClientManager]   Max Reconnect Attempts:" << config.max_reconnect_attempts;
  qDebug() << "[IpcClientManager]   Enable Obfuscation:" << (config.enable_obfuscation ? "yes" : "no");
  qDebug() << "[IpcClientManager]   DPI Bypass Mode:" << static_cast<int>(config.dpi_bypass_mode);

  if (!config.custom_routes.empty()) {
    qDebug() << "[IpcClientManager]   Custom Routes (" << config.custom_routes.size() << "):";
    for (const auto& route : config.custom_routes) {
      qDebug() << "[IpcClientManager]     -" << QString::fromStdString(route);
    }
  }

  ipc::ConnectCommand cmd;
  cmd.config = config;

  qDebug() << "[IpcClientManager] Sending ConnectCommand via IPC...";
  std::error_code ec;
  if (!client_->send_command(ipc::Command{cmd}, ec)) {
    qWarning() << "[IpcClientManager] Failed to send connect command. Error code:" << ec.value()
               << "Message:" << QString::fromStdString(ec.message());
    emit errorOccurred(
        tr("Failed to send connect command"),
        QString::fromStdString(ec.message()));
    return false;
  }

  qDebug() << "[IpcClientManager] ConnectCommand sent successfully";
  qDebug() << "[IpcClientManager] Waiting for response from daemon...";
  return true;
}

bool IpcClientManager::sendConnect(const QString& serverAddress, uint16_t serverPort) {
  ipc::ConnectionConfig config;
  config.server_address = serverAddress.toStdString();
  config.server_port = serverPort;
  config.enable_obfuscation = true;
  config.auto_reconnect = true;
  config.route_all_traffic = true;

  return sendConnect(config);
}

bool IpcClientManager::sendDisconnect() {
  qDebug() << "[IpcClientManager] ========================================";
  qDebug() << "[IpcClientManager] SENDING DISCONNECT COMMAND";
  qDebug() << "[IpcClientManager] ========================================";

  if (!isConnected()) {
    qWarning() << "[IpcClientManager] Cannot send disconnect command - not connected to daemon";
    return false;
  }

  ipc::DisconnectCommand cmd;
  qDebug() << "[IpcClientManager] Sending DisconnectCommand via IPC...";

  std::error_code ec;
  if (!client_->send_command(ipc::Command{cmd}, ec)) {
    qWarning() << "[IpcClientManager] Failed to send disconnect command. Error code:" << ec.value()
               << "Message:" << QString::fromStdString(ec.message());
    emit errorOccurred(
        tr("Failed to send disconnect command"),
        QString::fromStdString(ec.message()));
    return false;
  }

  qDebug() << "[IpcClientManager] DisconnectCommand sent successfully";
  return true;
}

bool IpcClientManager::requestStatus() {
  if (!isConnected()) {
    return false;
  }

  ipc::GetStatusCommand cmd;
  std::error_code ec;
  return client_->send_command(ipc::Command{cmd}, ec);
}

bool IpcClientManager::requestDiagnostics() {
  if (!isConnected()) {
    return false;
  }

  ipc::GetDiagnosticsCommand cmd;
  std::error_code ec;
  return client_->send_command(ipc::Command{cmd}, ec);
}

void IpcClientManager::pollMessages() {
  if (!client_->is_connected()) {
    // Lost connection to daemon
    if (daemonConnected_) {
      daemonConnected_ = false;
      emit daemonConnectionChanged(false);
      emit errorOccurred(
          tr("Lost connection to daemon"),
          tr("The connection to the VEIL client daemon was lost."));
    }
    return;
  }

  std::error_code ec;
  client_->poll(ec);
  if (ec) {
    // Error during poll - connection may have dropped
    emit errorOccurred(
        tr("IPC communication error"),
        QString::fromStdString(ec.message()));
  }
}

void IpcClientManager::handleMessage(const ipc::Message& msg) {
  qDebug() << "[IpcClientManager] Received message from daemon, type:" << static_cast<int>(msg.type);

  // Handle events from daemon
  if (msg.type == ipc::MessageType::kEvent) {
    qDebug() << "[IpcClientManager] Message is an Event";
    const auto* event = std::get_if<ipc::Event>(&msg.payload);
    if (event == nullptr) {
      qWarning() << "[IpcClientManager] Failed to extract Event from message payload";
      return;
    }

    // Handle different event types
    if (const auto* statusEvent = std::get_if<ipc::StatusUpdateEvent>(event)) {
      qDebug() << "[IpcClientManager] Received StatusUpdateEvent";
      qDebug() << "[IpcClientManager]   Connection State:" << static_cast<int>(statusEvent->status.state);
      qDebug() << "[IpcClientManager]   Server:" << QString::fromStdString(statusEvent->status.server_address)
               << ":" << statusEvent->status.server_port;
      if (!statusEvent->status.session_id.empty()) {
        qDebug() << "[IpcClientManager]   Session ID:" << QString::fromStdString(statusEvent->status.session_id);
      }
      if (!statusEvent->status.error_message.empty()) {
        qDebug() << "[IpcClientManager]   Error Message:" << QString::fromStdString(statusEvent->status.error_message);
      }
      emit statusUpdated(statusEvent->status);
      emit connectionStateChanged(statusEvent->status.state);
    }
    else if (const auto* metricsEvent = std::get_if<ipc::MetricsUpdateEvent>(event)) {
      qDebug() << "[IpcClientManager] Received MetricsUpdateEvent";
      qDebug() << "[IpcClientManager]   Latency:" << metricsEvent->metrics.latency_ms << "ms";
      qDebug() << "[IpcClientManager]   TX:" << metricsEvent->metrics.tx_bytes_per_sec << "B/s";
      qDebug() << "[IpcClientManager]   RX:" << metricsEvent->metrics.rx_bytes_per_sec << "B/s";
      emit metricsUpdated(metricsEvent->metrics);
    }
    else if (const auto* stateEvent = std::get_if<ipc::ConnectionStateChangeEvent>(event)) {
      qDebug() << "[IpcClientManager] Received ConnectionStateChangeEvent";
      qDebug() << "[IpcClientManager]   New State:" << static_cast<int>(stateEvent->new_state);
      emit connectionStateChanged(stateEvent->new_state);
    }
    else if (const auto* errorEvent = std::get_if<ipc::ErrorEvent>(event)) {
      qWarning() << "[IpcClientManager] Received ErrorEvent";
      qWarning() << "[IpcClientManager]   Error:" << QString::fromStdString(errorEvent->error_message);
      qWarning() << "[IpcClientManager]   Details:" << QString::fromStdString(errorEvent->details);
      emit errorOccurred(
          QString::fromStdString(errorEvent->error_message),
          QString::fromStdString(errorEvent->details));
    }
    else if (const auto* logEvent = std::get_if<ipc::LogEventData>(event)) {
      qDebug() << "[IpcClientManager] Received LogEvent:"
               << QString::fromStdString(logEvent->event.level)
               << QString::fromStdString(logEvent->event.message);
      emit logEventReceived(logEvent->event);
    }
    else if (const auto* heartbeatEvent = std::get_if<ipc::HeartbeatEvent>(event)) {
      qDebug() << "[IpcClientManager] Received HeartbeatEvent (timestamp: " << heartbeatEvent->timestamp_ms << ")";
      // Update last heartbeat timestamp
      lastHeartbeat_ = std::chrono::steady_clock::now();
    }
    else {
      qWarning() << "[IpcClientManager] Received unknown event type";
    }
  }
  // Handle responses from daemon
  else if (msg.type == ipc::MessageType::kResponse) {
    qDebug() << "[IpcClientManager] Message is a Response";
    const auto* response = std::get_if<ipc::Response>(&msg.payload);
    if (response == nullptr) {
      qWarning() << "[IpcClientManager] Failed to extract Response from message payload";
      return;
    }

    if (const auto* statusResp = std::get_if<ipc::StatusResponse>(response)) {
      qDebug() << "[IpcClientManager] Received StatusResponse";
      qDebug() << "[IpcClientManager]   Connection State:" << static_cast<int>(statusResp->status.state);
      emit statusUpdated(statusResp->status);
      emit connectionStateChanged(statusResp->status.state);
    }
    else if (const auto* metricsResp = std::get_if<ipc::MetricsResponse>(response)) {
      qDebug() << "[IpcClientManager] Received MetricsResponse";
      emit metricsUpdated(metricsResp->metrics);
    }
    else if (const auto* diagResp = std::get_if<ipc::DiagnosticsResponse>(response)) {
      qDebug() << "[IpcClientManager] Received DiagnosticsResponse";
      emit diagnosticsReceived(diagResp->diagnostics);
    }
    else if (const auto* successResp = std::get_if<ipc::SuccessResponse>(response)) {
      qDebug() << "[IpcClientManager] Received SuccessResponse";
      qDebug() << "[IpcClientManager]   Message:" << QString::fromStdString(successResp->message);
      // Success responses don't need special handling - the status updates will come via events
    }
    else if (const auto* errorResp = std::get_if<ipc::ErrorResponse>(response)) {
      qWarning() << "[IpcClientManager] Received ErrorResponse";
      qWarning() << "[IpcClientManager]   Error:" << QString::fromStdString(errorResp->error_message);
      qWarning() << "[IpcClientManager]   Details:" << QString::fromStdString(errorResp->details);
      emit errorOccurred(
          QString::fromStdString(errorResp->error_message),
          QString::fromStdString(errorResp->details));
    }
    else {
      qWarning() << "[IpcClientManager] Received unknown response type";
    }
  }
  // Handle error messages
  else if (msg.type == ipc::MessageType::kError) {
    qWarning() << "[IpcClientManager] Received error message from daemon";
    emit errorOccurred(tr("IPC Error"), tr("Received error message from daemon"));
  }
  else {
    qWarning() << "[IpcClientManager] Received unknown message type:" << static_cast<int>(msg.type);
  }
}

void IpcClientManager::handleConnectionChange(bool connected) {
  qDebug() << "[IpcClientManager] Connection state changed:" << (connected ? "CONNECTED" : "DISCONNECTED");

  daemonConnected_ = connected;
  emit daemonConnectionChanged(connected);

  if (!connected) {
    qDebug() << "[IpcClientManager] Stopping poll timer and heartbeat monitoring, starting reconnection attempts";
    pollTimer_->stop();
    heartbeatTimer_->stop();
    // Start trying to reconnect in background
    startReconnectTimer();
  } else {
    qDebug() << "[IpcClientManager] Connection established, stopping reconnection timer";
    // Stop reconnection timer on successful connection
    stopReconnectTimer();
    // Start heartbeat monitoring
    lastHeartbeat_ = std::chrono::steady_clock::now();
    heartbeatTimer_->start();
  }
}

void IpcClientManager::attemptReconnect() {
  if (daemonConnected_ || client_->is_connected()) {
    // Already connected, stop reconnecting
    qDebug() << "[IpcClientManager] Already connected, stopping reconnection attempts";
    stopReconnectTimer();
    return;
  }

  reconnectAttempts_++;
  qDebug() << "[IpcClientManager] Reconnection attempt" << reconnectAttempts_ << "of" << kMaxReconnectAttempts;

  // Try to connect
  std::error_code ec;
  if (client_->connect(ec)) {
    // Successfully reconnected
    qDebug() << "[IpcClientManager] Reconnection successful!";
    stopReconnectTimer();
    daemonConnected_ = true;
    pollTimer_->start();

    // Restart heartbeat monitoring
    lastHeartbeat_ = std::chrono::steady_clock::now();
    heartbeatTimer_->start();
    qDebug() << "[IpcClientManager] Restarted heartbeat monitoring after reconnection";

    emit daemonConnectionChanged(true);
  } else {
    qDebug() << "[IpcClientManager] Reconnection failed. Error:" << QString::fromStdString(ec.message());

    if (reconnectAttempts_ >= kMaxReconnectAttempts) {
      // Give up after max attempts
      qWarning() << "[IpcClientManager] Maximum reconnection attempts reached, giving up";
      stopReconnectTimer();
    } else {
      qDebug() << "[IpcClientManager] Will retry in" << (kReconnectIntervalMs / 1000) << "seconds";
    }
  }
  // Otherwise, timer will fire again and we'll retry
}

void IpcClientManager::startReconnectTimer() {
  if (!reconnectTimer_->isActive()) {
    reconnectAttempts_ = 0;
    reconnectTimer_->start();
  }
}

void IpcClientManager::stopReconnectTimer() {
  reconnectTimer_->stop();
  reconnectAttempts_ = 0;
}

void IpcClientManager::checkHeartbeatTimeout() {
  if (!daemonConnected_ || !client_->is_connected()) {
    // Not connected, no need to check heartbeat
    return;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastHeartbeat_).count();

  if (elapsed >= kHeartbeatTimeoutSec) {
    // Heartbeat timeout - service is likely dead
    qWarning() << "[IpcClientManager] Heartbeat timeout detected!";
    qWarning() << "[IpcClientManager] No heartbeat received for" << elapsed << "seconds (threshold:" << kHeartbeatTimeoutSec << "seconds)";
    qWarning() << "[IpcClientManager] Service is likely unreachable or crashed";

    // Stop heartbeat monitoring
    heartbeatTimer_->stop();

    // Mark as disconnected
    daemonConnected_ = false;
    pollTimer_->stop();

    // Emit error and connection change
    emit errorOccurred(
        tr("Service unreachable"),
        tr("The VEIL client service has not responded for %1 seconds. The service may have crashed.").arg(elapsed));
    emit daemonConnectionChanged(false);

    // Start reconnection attempts
    qDebug() << "[IpcClientManager] Starting reconnection attempts after heartbeat timeout";
    startReconnectTimer();
  }
}

}  // namespace veil::gui
