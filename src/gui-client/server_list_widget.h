#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QComboBox>
#include <memory>

#include "server_config.h"

namespace veil::gui {

/// Custom list item widget for server display
class ServerListItem : public QWidget {
  Q_OBJECT

 public:
  explicit ServerListItem(const ServerConfig& server, QWidget* parent = nullptr);

  /// Get server ID
  QString serverId() const { return serverId_; }

  /// Update server data
  void updateServer(const ServerConfig& server);

 signals:
  void editRequested(const QString& serverId);
  void deleteRequested(const QString& serverId);
  void favoriteToggled(const QString& serverId);
  void pingRequested(const QString& serverId);

 private:
  void setupUi(const ServerConfig& server);
  QString getLatencyBadgeHtml(int latencyMs) const;
  QString getLatencyColor(int latencyMs) const;

  QString serverId_;
  QLabel* nameLabel_;
  QLabel* addressLabel_;
  QLabel* latencyLabel_;
  QPushButton* favoriteButton_;
  QPushButton* editButton_;
  QPushButton* deleteButton_;
  QPushButton* pingButton_;
};

/// Widget for managing server list
class ServerListWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ServerListWidget(QWidget* parent = nullptr);

  /// Reload server list from manager
  void refreshServerList();

  /// Get selected server ID (if any)
  QString getSelectedServerId() const;

 signals:
  void backRequested();
  void serverSelected(const QString& serverId);

 private slots:
  void onAddServer();
  void onEditServer(const QString& serverId);
  void onDeleteServer(const QString& serverId);
  void onToggleFavorite(const QString& serverId);
  void onPingServer(const QString& serverId);
  void onPingAllServers();
  void onImportFromUri();
  void onImportFromFile();
  void onExportServer(const QString& serverId);
  void onServerItemClicked(QListWidgetItem* item);
  void onSearchTextChanged(const QString& text);
  void onSortModeChanged(int index);

 private:
  void setupUi();
  void showServerDialog(const ServerConfig& server, bool isNew);
  void applySearchFilter();
  void applySortMode();
  void pingServerAsync(const QString& serverId);

  std::unique_ptr<ServerListManager> serverManager_;

  // UI Elements
  QLineEdit* searchEdit_;
  QComboBox* sortCombo_;
  QListWidget* serverList_;
  QPushButton* addButton_;
  QPushButton* importUriButton_;
  QPushButton* importFileButton_;
  QPushButton* pingAllButton_;
  QLabel* emptyStateLabel_;

  // State
  QString currentSearch_;
  int currentSortMode_{0};  // 0=Name, 1=Latency, 2=Favorites, 3=Recent
};

/// Dialog for adding/editing server
class ServerEditDialog : public QWidget {
  Q_OBJECT

 public:
  explicit ServerEditDialog(const ServerConfig& server, bool isNew, QWidget* parent = nullptr);

  /// Get edited server configuration
  ServerConfig getServerConfig() const;

 signals:
  void saveRequested();
  void cancelRequested();

 private:
  void setupUi();
  void loadServerData();
  void validateForm();
  void onBrowseKeyFile();
  void onBrowseObfuscationSeed();

  ServerConfig server_;
  bool isNew_;

  // Form fields
  QLineEdit* nameEdit_;
  QLineEdit* addressEdit_;
  QLineEdit* portEdit_;
  QLineEdit* keyFileEdit_;
  QPushButton* browseKeyFileButton_;
  QLineEdit* obfuscationSeedEdit_;
  QPushButton* browseObfuscationSeedButton_;
  QLineEdit* notesEdit_;
  QPushButton* saveButton_;
  QPushButton* cancelButton_;
};

}  // namespace veil::gui
