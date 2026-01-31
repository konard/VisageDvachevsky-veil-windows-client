#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QPropertyAnimation>
#include <QFrame>
#include <QVBoxLayout>

#include "connection_state.h"

namespace veil::gui {

/// Expandable quick actions panel for frequently used operations.
///
/// Provides one-click access to common actions without navigating
/// to settings or diagnostics views:
/// - Kill switch toggle
/// - Obfuscation mode quick switch
/// - Copy IP address
/// - Share connection status
/// - Open diagnostics
/// - Copy debug info
class QuickActionsWidget : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)

 public:
  explicit QuickActionsWidget(QWidget* parent = nullptr);

  /// Update the displayed IP address (called when connected)
  void setIpAddress(const QString& ip, uint16_t port);

  /// Update the connection state to enable/disable relevant actions
  void setConnectionState(ConnectionState state);

  /// Update the kill switch state display
  void setKillSwitchEnabled(bool enabled);

  /// Update the obfuscation state display
  void setObfuscationEnabled(bool enabled);

  /// Get current kill switch state
  bool isKillSwitchEnabled() const { return killSwitchEnabled_; }

  /// Get current obfuscation state
  bool isObfuscationEnabled() const { return obfuscationEnabled_; }

 signals:
  /// Emitted when kill switch is toggled
  void killSwitchToggled(bool enabled);

  /// Emitted when obfuscation is toggled
  void obfuscationToggled(bool enabled);

  /// Emitted when user requests to open diagnostics
  void diagnosticsRequested();

  /// Emitted when user requests to open settings
  void settingsRequested();

 private slots:
  void onToggleClicked();
  void onKillSwitchClicked();
  void onObfuscationClicked();
  void onCopyIpClicked();
  void onShareStatusClicked();
  void onOpenDiagnosticsClicked();
  void onCopyDebugInfoClicked();

 private:
  void setupUi();
  void updateToggleIcon();
  void updateActionStates();

  // Property accessors for animation
  int contentHeight() const;
  void setContentHeight(int height);

  // Toggle button
  QPushButton* toggleButton_;

  // Content container (animated)
  QFrame* contentContainer_;
  QVBoxLayout* contentLayout_;
  QPropertyAnimation* animation_;
  bool collapsed_{true};
  int expandedHeight_{0};

  // Action buttons
  QPushButton* killSwitchButton_;
  QPushButton* obfuscationButton_;
  QPushButton* copyIpButton_;
  QPushButton* shareStatusButton_;
  QPushButton* openDiagnosticsButton_;
  QPushButton* copyDebugInfoButton_;

  // State
  bool killSwitchEnabled_{false};
  bool obfuscationEnabled_{true};
  QString ipAddress_;
  uint16_t port_{0};
  ConnectionState connectionState_{ConnectionState::kDisconnected};
};

}  // namespace veil::gui
