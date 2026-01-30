#pragma once

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QElapsedTimer>

namespace veil::gui {

class ServerSelectorWidget;

/// Connection states as defined in the UI design spec
enum class ConnectionState {
  kDisconnected,
  kConnecting,
  kConnected,
  kReconnecting,
  kError
};

/// Widget for displaying connection status and controls
class ConnectionWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ConnectionWidget(QWidget* parent = nullptr);

 signals:
  void settingsRequested();
  void serversRequested();
  void connectRequested();
  void disconnectRequested();

 public slots:
  /// Update connection state from IPC manager
  void setConnectionState(ConnectionState state);

  /// Update metrics (called periodically when connected)
  void updateMetrics(int latencyMs, uint64_t txBytesPerSec, uint64_t rxBytesPerSec);

  /// Update session info
  void setSessionId(const QString& sessionId);
  void setServerAddress(const QString& server, uint16_t port);

  /// Set error message when in error state
  void setErrorMessage(const QString& message);

  /// Handle connect button click (toggle connect/disconnect)
  void onConnectClicked();

  /// Load server settings from QSettings and update display
  void loadServerSettings();

 private slots:
  void onPulseAnimation();
  void onUptimeUpdate();
  void onConnectionTimeout();

 private:
  void setupUi();
  void setupAnimations();
  void updateStatusDisplay();
  void startPulseAnimation();
  void stopPulseAnimation();

  QString formatBytes(uint64_t bytesPerSec) const;
  QString formatUptime(int seconds) const;
  QString getStatusColor() const;
  QString getStatusText() const;

  // UI Elements
  QWidget* statusCard_;
  QWidget* statusRing_;  // Custom painted status ring
  QLabel* statusLabel_;
  QLabel* subtitleLabel_;
  QLabel* errorLabel_;
  QPushButton* connectButton_;

  // Session info group
  QWidget* sessionInfoGroup_;
  QLabel* sessionIdLabel_;
  QLabel* serverLabel_;
  QLabel* latencyLabel_;
  QLabel* throughputLabel_;
  QLabel* uptimeLabel_;

  // Navigation
  QPushButton* settingsButton_;
  ServerSelectorWidget* serverSelector_;

  // State
  ConnectionState state_{ConnectionState::kDisconnected};
  QString sessionId_;
  QString serverAddress_;
  uint16_t serverPort_{4433};
  int latencyMs_{0};
  uint64_t txBytes_{0};
  uint64_t rxBytes_{0};
  int reconnectAttempt_{0};
  QString errorMessage_;

  // Animation
  QTimer* pulseTimer_;
  QTimer* uptimeTimer_;
  QTimer* connectionTimeoutTimer_;
  QPropertyAnimation* pulseAnimation_;
  QGraphicsOpacityEffect* statusOpacity_;
  QElapsedTimer uptimeCounter_;
  bool pulseState_{false};
  qreal animationPhase_{0.0};

  // Connection timeout (30 seconds default)
  static constexpr int kConnectionTimeoutMs = 30000;
};

}  // namespace veil::gui
