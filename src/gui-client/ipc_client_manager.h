#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

#include "common/ipc/ipc_protocol.h"
#include "common/ipc/ipc_socket.h"

namespace veil::gui {

/// Manager for IPC communication with the veil-client daemon.
/// Provides Qt signals for UI updates based on daemon events.
class IpcClientManager : public QObject {
  Q_OBJECT

 public:
  explicit IpcClientManager(QObject* parent = nullptr);
  ~IpcClientManager() override;

  /// Attempt to connect to the daemon
  bool connectToDaemon();

  /// Disconnect from the daemon
  void disconnect();

  /// Check if connected to daemon
  [[nodiscard]] bool isConnected() const;

  /// Send a connect command to the daemon with full configuration
  bool sendConnect(const ipc::ConnectionConfig& config);

  /// Send a connect command to the daemon (legacy - uses minimal config)
  bool sendConnect(const QString& serverAddress, uint16_t serverPort);

  /// Send a disconnect command to the daemon
  bool sendDisconnect();

  /// Request current status from daemon
  bool requestStatus();

  /// Request diagnostics data from daemon
  bool requestDiagnostics();

 signals:
  /// Emitted when connection state changes
  void connectionStateChanged(ipc::ConnectionState state);

  /// Emitted when connection status is updated
  void statusUpdated(const ipc::ConnectionStatus& status);

  /// Emitted when metrics are updated
  void metricsUpdated(const ipc::ConnectionMetrics& metrics);

  /// Emitted when diagnostics data is received
  void diagnosticsReceived(const ipc::DiagnosticsData& diagnostics);

  /// Emitted when a log event is received
  void logEventReceived(const ipc::LogEvent& event);

  /// Emitted when an error occurs
  void errorOccurred(const QString& errorMessage, const QString& details);

  /// Emitted when daemon connection state changes
  void daemonConnectionChanged(bool connected);

 private slots:
  /// Poll for IPC messages
  void pollMessages();

  /// Attempt to reconnect to daemon
  void attemptReconnect();

 private:
  void handleMessage(const ipc::Message& msg);
  void handleConnectionChange(bool connected);
  void startReconnectTimer();
  void stopReconnectTimer();

  std::unique_ptr<ipc::IpcClient> client_;
  QTimer* pollTimer_;
  QTimer* reconnectTimer_;
  bool daemonConnected_{false};
  int reconnectAttempts_{0};

  // Reconnection constants
  static constexpr int kReconnectIntervalMs = 5000;  // 5 seconds
  static constexpr int kMaxReconnectAttempts = 12;   // Try for 1 minute
};

}  // namespace veil::gui
