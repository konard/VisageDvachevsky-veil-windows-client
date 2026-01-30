#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QDialog>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <memory>

#include "update_checker.h"
#include "common/gui/error_message.h"
#include "common/gui/theme.h"

namespace veil::gui {

class ConnectionWidget;
class SettingsWidget;
class DiagnosticsWidget;
class IpcClientManager;
class ServerListWidget;

/// Connection state for system tray icon updates
enum class TrayConnectionState {
  kDisconnected,
  kConnecting,
  kConnected,
  kError
};

/// Animated stacked widget with smooth fade/slide transitions
class AnimatedStackedWidget : public QStackedWidget {
  Q_OBJECT

 public:
  explicit AnimatedStackedWidget(QWidget* parent = nullptr);

  /// Set the current widget with animation
  void setCurrentWidgetAnimated(int index);

  /// Animation duration in milliseconds
  void setAnimationDuration(int duration) { animationDuration_ = duration; }

 private:
  int animationDuration_{250};
  bool isAnimating_{false};
};

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 public slots:
  /// Update the system tray icon based on connection state
  void updateTrayIcon(TrayConnectionState state);

  /// Show error with system tray notification for critical errors
  void showError(const ErrorMessage& error, bool showTrayNotification = false);

  /// Apply the specified theme to the application
  void applyTheme(Theme theme);

 protected:
  /// Handle window close event - minimize to tray if enabled
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void showConnectionView();
  void showSettingsView();
  void showDiagnosticsView();
  void showServerListView();
  void showAboutDialog();
  void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
  void onQuickConnect();
  void onQuickDisconnect();
  void checkForUpdates();
  void onUpdateAvailable(const UpdateInfo& info);
  void onNoUpdateAvailable();
  void onUpdateCheckFailed(const QString& error);

 private:
  void setupUi();
  void setupIpcConnections();
  void setupMenuBar();
  void setupStatusBar();
  void setupSystemTray();
  void setupUpdateChecker();
  void applyDarkTheme();
  void loadThemePreference();

#ifdef _WIN32
  /// Ensure the Windows service is running, starting it if necessary
  bool ensureServiceRunning();

  /// Wait for the service IPC server to be ready for connections.
  /// Uses a two-phase approach: first checks a Windows Event signal from the
  /// service, then falls back to polling for the Named Pipe existence.
  /// Returns true if the service is ready within the timeout.
  bool waitForServiceReady(int timeout_ms = 5000);
#endif

  AnimatedStackedWidget* stackedWidget_;
  ConnectionWidget* connectionWidget_;
  SettingsWidget* settingsWidget_;
  DiagnosticsWidget* diagnosticsWidget_;
  ServerListWidget* serverListWidget_;
  std::unique_ptr<IpcClientManager> ipcManager_;

  // System tray
  QSystemTrayIcon* trayIcon_;
  QMenu* trayMenu_;
  QAction* trayConnectAction_;
  QAction* trayDisconnectAction_;
  bool minimizeToTray_{true};
  TrayConnectionState currentTrayState_{TrayConnectionState::kDisconnected};

  // Update checker
  std::unique_ptr<UpdateChecker> updateChecker_;

  // Current theme
  Theme currentTheme_{Theme::kDark};
};

}  // namespace veil::gui
