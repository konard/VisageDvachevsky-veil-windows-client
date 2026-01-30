#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <memory>

#include "server_config.h"

namespace veil::gui {

/// Compact server selector widget with quick-switch dropdown and latency display
class ServerSelectorWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ServerSelectorWidget(QWidget* parent = nullptr);

  /// Get currently selected server ID
  QString getCurrentServerId() const;

  /// Set currently selected server by ID
  void setCurrentServerId(const QString& id);

  /// Refresh server list from manager
  void refreshServers();

  /// Get current server configuration
  std::optional<ServerConfig> getCurrentServer() const;

 signals:
  void serverChanged(const QString& serverId);
  void manageServersRequested();

 private slots:
  void onServerSelectionChanged(int index);
  void onManageServersClicked();
  void onRefreshLatency();

 private:
  void setupUi();
  void updateLatencyDisplay();
  QString formatLatencyBadge(int latencyMs) const;

  std::unique_ptr<ServerListManager> serverManager_;

  QComboBox* serverCombo_;
  QLabel* latencyLabel_;
  QPushButton* manageButton_;
  QPushButton* refreshButton_;
  QTimer* autoRefreshTimer_;
};

}  // namespace veil::gui
