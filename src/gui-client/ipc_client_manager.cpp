#include "ipc_client_manager.h"

#include <system_error>

namespace veil::gui {

IpcClientManager::IpcClientManager(QObject* parent)
    : QObject(parent),
      client_(std::make_unique<ipc::IpcClient>()),
      pollTimer_(new QTimer(this)) {
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
}

IpcClientManager::~IpcClientManager() {
  disconnect();
}

bool IpcClientManager::connectToDaemon() {
  std::error_code ec;
  if (!client_->connect(ec)) {
    // Failed to connect - daemon may not be running
    emit errorOccurred(
        tr("Failed to connect to daemon"),
        tr("The VEIL client daemon may not be running. Please start veil-client first."));
    return false;
  }

  daemonConnected_ = true;
  pollTimer_->start();
  emit daemonConnectionChanged(true);
  return true;
}

void IpcClientManager::disconnect() {
  pollTimer_->stop();
  client_->disconnect();
  daemonConnected_ = false;
  emit daemonConnectionChanged(false);
}

bool IpcClientManager::isConnected() const {
  return client_->is_connected();
}

bool IpcClientManager::sendConnect(const QString& serverAddress, uint16_t serverPort) {
  if (!isConnected()) {
    emit errorOccurred(
        tr("Not connected to daemon"),
        tr("Cannot send connect command - not connected to daemon."));
    return false;
  }

  ipc::ConnectCommand cmd;
  cmd.config.server_address = serverAddress.toStdString();
  cmd.config.server_port = serverPort;
  cmd.config.enable_obfuscation = true;
  cmd.config.auto_reconnect = true;
  cmd.config.route_all_traffic = true;

  std::error_code ec;
  if (!client_->send_command(ipc::Command{cmd}, ec)) {
    emit errorOccurred(
        tr("Failed to send connect command"),
        QString::fromStdString(ec.message()));
    return false;
  }

  return true;
}

bool IpcClientManager::sendDisconnect() {
  if (!isConnected()) {
    return false;
  }

  ipc::DisconnectCommand cmd;
  std::error_code ec;
  if (!client_->send_command(ipc::Command{cmd}, ec)) {
    emit errorOccurred(
        tr("Failed to send disconnect command"),
        QString::fromStdString(ec.message()));
    return false;
  }

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
  // Handle events from daemon
  if (msg.type == ipc::MessageType::kEvent) {
    const auto* event = std::get_if<ipc::Event>(&msg.payload);
    if (!event) return;

    // Handle different event types
    if (const auto* statusEvent = std::get_if<ipc::StatusUpdateEvent>(event)) {
      emit statusUpdated(statusEvent->status);
      emit connectionStateChanged(statusEvent->status.state);
    }
    else if (const auto* metricsEvent = std::get_if<ipc::MetricsUpdateEvent>(event)) {
      emit metricsUpdated(metricsEvent->metrics);
    }
    else if (const auto* stateEvent = std::get_if<ipc::ConnectionStateChangeEvent>(event)) {
      emit connectionStateChanged(stateEvent->new_state);
    }
    else if (const auto* errorEvent = std::get_if<ipc::ErrorEvent>(event)) {
      emit errorOccurred(
          QString::fromStdString(errorEvent->error_message),
          QString::fromStdString(errorEvent->details));
    }
    else if (const auto* logEvent = std::get_if<ipc::LogEventData>(event)) {
      emit logEventReceived(logEvent->event);
    }
  }
  // Handle responses from daemon
  else if (msg.type == ipc::MessageType::kResponse) {
    const auto* response = std::get_if<ipc::Response>(&msg.payload);
    if (!response) return;

    if (const auto* statusResp = std::get_if<ipc::StatusResponse>(response)) {
      emit statusUpdated(statusResp->status);
      emit connectionStateChanged(statusResp->status.state);
    }
    else if (const auto* metricsResp = std::get_if<ipc::MetricsResponse>(response)) {
      emit metricsUpdated(metricsResp->metrics);
    }
    else if (const auto* diagResp = std::get_if<ipc::DiagnosticsResponse>(response)) {
      emit diagnosticsReceived(diagResp->diagnostics);
    }
    else if (const auto* errorResp = std::get_if<ipc::ErrorResponse>(response)) {
      emit errorOccurred(
          QString::fromStdString(errorResp->error_message),
          QString::fromStdString(errorResp->details));
    }
  }
  // Handle error messages
  else if (msg.type == ipc::MessageType::kError) {
    emit errorOccurred(tr("IPC Error"), tr("Received error message from daemon"));
  }
}

void IpcClientManager::handleConnectionChange(bool connected) {
  daemonConnected_ = connected;
  emit daemonConnectionChanged(connected);

  if (!connected) {
    pollTimer_->stop();
  }
}

}  // namespace veil::gui
