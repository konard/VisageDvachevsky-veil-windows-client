#include "server_list_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QScrollArea>
#include <QFrame>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>

#include "common/gui/theme.h"

namespace veil::gui {

//==============================================================================
// ServerListItem Implementation
//==============================================================================

ServerListItem::ServerListItem(const ServerConfig& server, QWidget* parent)
    : QWidget(parent), serverId_(server.id) {
  setupUi(server);
}

void ServerListItem::setupUi(const ServerConfig& server) {
  auto* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(16, 12, 16, 12);
  mainLayout->setSpacing(12);

  // Favorite star button
  favoriteButton_ = new QPushButton(server.isFavorite ? "★" : "☆", this);
  favoriteButton_->setFixedSize(32, 32);
  favoriteButton_->setCursor(Qt::PointingHandCursor);
  favoriteButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 20px;
      color: %1;
    }
    QPushButton:hover {
      color: %2;
    }
  )").arg(server.isFavorite ? "#ffd700" : "#6e7681", "#ffd700"));
  connect(favoriteButton_, &QPushButton::clicked, this, [this]() {
    emit favoriteToggled(serverId_);
  });
  mainLayout->addWidget(favoriteButton_);

  // Server info (name + address)
  auto* infoLayout = new QVBoxLayout();
  infoLayout->setSpacing(4);

  nameLabel_ = new QLabel(server.name, this);
  nameLabel_->setStyleSheet("font-size: 15px; font-weight: 600; color: #f0f6fc;");
  infoLayout->addWidget(nameLabel_);

  addressLabel_ = new QLabel(QString("%1:%2").arg(server.address).arg(server.port), this);
  addressLabel_->setStyleSheet("font-size: 13px; color: #8b949e;");
  infoLayout->addWidget(addressLabel_);

  mainLayout->addLayout(infoLayout, 1);  // Stretch

  // Latency badge
  latencyLabel_ = new QLabel(this);
  latencyLabel_->setTextFormat(Qt::RichText);
  latencyLabel_->setText(getLatencyBadgeHtml(server.lastLatencyMs));
  mainLayout->addWidget(latencyLabel_);

  // Ping button
  pingButton_ = new QPushButton("Ping", this);
  pingButton_->setFixedHeight(28);
  pingButton_->setCursor(Qt::PointingHandCursor);
  pingButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(88, 166, 255, 0.1);
      border: 1px solid rgba(88, 166, 255, 0.3);
      border-radius: 6px;
      color: #58a6ff;
      padding: 4px 12px;
      font-size: 12px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(88, 166, 255, 0.15);
      border-color: #58a6ff;
    }
  )");
  connect(pingButton_, &QPushButton::clicked, this, [this]() {
    emit pingRequested(serverId_);
  });
  mainLayout->addWidget(pingButton_);

  // Edit button
  editButton_ = new QPushButton("Edit", this);
  editButton_->setFixedHeight(28);
  editButton_->setCursor(Qt::PointingHandCursor);
  editButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 6px;
      color: #8b949e;
      padding: 4px 12px;
      font-size: 12px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      color: #f0f6fc;
    }
  )");
  connect(editButton_, &QPushButton::clicked, this, [this]() {
    emit editRequested(serverId_);
  });
  mainLayout->addWidget(editButton_);

  // Delete button
  deleteButton_ = new QPushButton("×", this);
  deleteButton_->setFixedSize(28, 28);
  deleteButton_->setCursor(Qt::PointingHandCursor);
  deleteButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(248, 81, 73, 0.1);
      border: 1px solid rgba(248, 81, 73, 0.3);
      border-radius: 6px;
      color: #f85149;
      font-size: 18px;
      font-weight: 600;
    }
    QPushButton:hover {
      background: rgba(248, 81, 73, 0.15);
      border-color: #f85149;
    }
  )");
  connect(deleteButton_, &QPushButton::clicked, this, [this]() {
    emit deleteRequested(serverId_);
  });
  mainLayout->addWidget(deleteButton_);

  // Overall styling
  setStyleSheet(R"(
    ServerListItem {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
    }
    ServerListItem:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.15);
    }
  )");
}

void ServerListItem::updateServer(const ServerConfig& server) {
  nameLabel_->setText(server.name);
  addressLabel_->setText(QString("%1:%2").arg(server.address).arg(server.port));
  latencyLabel_->setText(getLatencyBadgeHtml(server.lastLatencyMs));
  favoriteButton_->setText(server.isFavorite ? "★" : "☆");
  favoriteButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 20px;
      color: %1;
    }
    QPushButton:hover {
      color: %2;
    }
  )").arg(server.isFavorite ? "#ffd700" : "#6e7681", "#ffd700"));
}

QString ServerListItem::getLatencyBadgeHtml(int latencyMs) const {
  if (latencyMs < 0) {
    return QString("<span style='color: #6e7681; font-size: 12px;'>—</span>");
  }

  QString color = getLatencyColor(latencyMs);
  return QString("<span style='color: %1; font-size: 13px; font-weight: 600;'>%2ms</span>")
      .arg(color)
      .arg(latencyMs);
}

QString ServerListItem::getLatencyColor(int latencyMs) const {
  if (latencyMs < 0) return "#6e7681";
  if (latencyMs < 50) return "#3fb950";   // Green - excellent
  if (latencyMs < 100) return "#58a6ff";  // Blue - good
  if (latencyMs < 200) return "#d29922";  // Yellow - fair
  return "#f85149";  // Red - poor
}

//==============================================================================
// ServerListWidget Implementation
//==============================================================================

ServerListWidget::ServerListWidget(QWidget* parent)
    : QWidget(parent), serverManager_(std::make_unique<ServerListManager>()) {
  setupUi();
  refreshServerList();
}

void ServerListWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(20);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge, spacing::kPaddingMedium,
                                  spacing::kPaddingXLarge, spacing::kPaddingMedium);

  // === Header ===
  auto* headerLayout = new QHBoxLayout();

  auto* backButton = new QPushButton("\u2190 Back", this);
  backButton->setCursor(Qt::PointingHandCursor);
  backButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      color: #58a6ff;
      font-size: 14px;
      font-weight: 500;
      padding: 8px 0;
      text-align: left;
    }
    QPushButton:hover {
      color: #79c0ff;
    }
  )");
  connect(backButton, &QPushButton::clicked, this, &ServerListWidget::backRequested);
  headerLayout->addWidget(backButton);

  headerLayout->addStretch();
  mainLayout->addLayout(headerLayout);

  // Title
  auto* titleLabel = new QLabel("Server Management", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 700; color: #f0f6fc; margin-bottom: 8px;")
                                .arg(fonts::kFontSizeHeadline));
  mainLayout->addWidget(titleLabel);

  // === Toolbar ===
  auto* toolbarLayout = new QHBoxLayout();
  toolbarLayout->setSpacing(12);

  // Search box
  searchEdit_ = new QLineEdit(this);
  searchEdit_->setPlaceholderText("Search servers...");
  searchEdit_->setFixedHeight(36);
  connect(searchEdit_, &QLineEdit::textChanged, this, &ServerListWidget::onSearchTextChanged);
  toolbarLayout->addWidget(searchEdit_, 1);

  // Sort dropdown
  sortCombo_ = new QComboBox(this);
  sortCombo_->addItems({"Sort: Name", "Sort: Latency", "Sort: Favorites", "Sort: Recent"});
  sortCombo_->setFixedHeight(36);
  connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ServerListWidget::onSortModeChanged);
  toolbarLayout->addWidget(sortCombo_);

  // Ping All button
  pingAllButton_ = new QPushButton("Ping All", this);
  pingAllButton_->setFixedHeight(36);
  pingAllButton_->setCursor(Qt::PointingHandCursor);
  pingAllButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(88, 166, 255, 0.1);
      border: 1px solid rgba(88, 166, 255, 0.3);
      border-radius: 8px;
      color: #58a6ff;
      padding: 0 16px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(88, 166, 255, 0.15);
      border-color: #58a6ff;
    }
  )");
  connect(pingAllButton_, &QPushButton::clicked, this, &ServerListWidget::onPingAllServers);
  toolbarLayout->addWidget(pingAllButton_);

  mainLayout->addLayout(toolbarLayout);

  // === Server List ===
  serverList_ = new QListWidget(this);
  serverList_->setFrameShape(QFrame::NoFrame);
  serverList_->setSpacing(8);
  serverList_->setStyleSheet("QListWidget { background: transparent; border: none; }");
  connect(serverList_, &QListWidget::itemClicked, this, &ServerListWidget::onServerItemClicked);
  mainLayout->addWidget(serverList_, 1);  // Stretch

  // Empty state label
  emptyStateLabel_ = new QLabel("No servers configured.\nAdd a server to get started.", this);
  emptyStateLabel_->setAlignment(Qt::AlignCenter);
  emptyStateLabel_->setStyleSheet("color: #6e7681; font-size: 14px;");
  emptyStateLabel_->hide();
  mainLayout->addWidget(emptyStateLabel_);

  // === Action Buttons ===
  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(12);

  addButton_ = new QPushButton("+ Add Server", this);
  addButton_->setCursor(Qt::PointingHandCursor);
  addButton_->setStyleSheet(R"(
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 12px;
      padding: 14px 24px;
      color: white;
      font-size: 15px;
      font-weight: 600;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
  )");
  connect(addButton_, &QPushButton::clicked, this, &ServerListWidget::onAddServer);
  buttonLayout->addWidget(addButton_);

  importUriButton_ = new QPushButton("Import URI", this);
  importUriButton_->setCursor(Qt::PointingHandCursor);
  importUriButton_->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 12px;
      color: #8b949e;
      padding: 14px 24px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.04);
      border-color: rgba(255, 255, 255, 0.2);
      color: #f0f6fc;
    }
  )");
  connect(importUriButton_, &QPushButton::clicked, this, &ServerListWidget::onImportFromUri);
  buttonLayout->addWidget(importUriButton_);

  importFileButton_ = new QPushButton("Import File", this);
  importFileButton_->setCursor(Qt::PointingHandCursor);
  importFileButton_->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 12px;
      color: #8b949e;
      padding: 14px 24px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.04);
      border-color: rgba(255, 255, 255, 0.2);
      color: #f0f6fc;
    }
  )");
  connect(importFileButton_, &QPushButton::clicked, this, &ServerListWidget::onImportFromFile);
  buttonLayout->addWidget(importFileButton_);

  buttonLayout->addStretch();
  mainLayout->addLayout(buttonLayout);
}

void ServerListWidget::refreshServerList() {
  serverList_->clear();
  serverManager_->loadServers();

  applySortMode();
  applySearchFilter();

  if (serverList_->count() == 0) {
    serverList_->hide();
    emptyStateLabel_->show();
  } else {
    serverList_->show();
    emptyStateLabel_->hide();
  }
}

void ServerListWidget::applySearchFilter() {
  QString search = searchEdit_->text().trimmed().toLower();

  for (int i = 0; i < serverList_->count(); ++i) {
    QListWidgetItem* item = serverList_->item(i);
    ServerListItem* widget = qobject_cast<ServerListItem*>(serverList_->itemWidget(item));

    if (search.isEmpty()) {
      item->setHidden(false);
    } else {
      auto server = serverManager_->getServer(widget->serverId());
      bool matches = server && (server->name.toLower().contains(search) ||
                                server->address.toLower().contains(search));
      item->setHidden(!matches);
    }
  }
}

void ServerListWidget::applySortMode() {
  QList<ServerConfig> servers;

  switch (currentSortMode_) {
    case 0:  // Name
      servers = serverManager_->getAllServers();
      std::sort(servers.begin(), servers.end(), [](const ServerConfig& a, const ServerConfig& b) {
        return a.name < b.name;
      });
      break;
    case 1:  // Latency
      servers = serverManager_->getServersSortedByLatency();
      break;
    case 2:  // Favorites
      servers = serverManager_->getAllServers();
      std::sort(servers.begin(), servers.end(), [](const ServerConfig& a, const ServerConfig& b) {
        if (a.isFavorite != b.isFavorite) return a.isFavorite;
        return a.name < b.name;
      });
      break;
    case 3:  // Recent
      servers = serverManager_->getAllServers();
      std::sort(servers.begin(), servers.end(), [](const ServerConfig& a, const ServerConfig& b) {
        if (!a.lastConnected.isValid()) return false;
        if (!b.lastConnected.isValid()) return true;
        return a.lastConnected > b.lastConnected;
      });
      break;
  }

  serverList_->clear();

  for (const auto& server : servers) {
    auto* item = new QListWidgetItem(serverList_);
    auto* widget = new ServerListItem(server, serverList_);

    connect(widget, &ServerListItem::editRequested, this, &ServerListWidget::onEditServer);
    connect(widget, &ServerListItem::deleteRequested, this, &ServerListWidget::onDeleteServer);
    connect(widget, &ServerListItem::favoriteToggled, this, &ServerListWidget::onToggleFavorite);
    connect(widget, &ServerListItem::pingRequested, this, &ServerListWidget::onPingServer);

    item->setSizeHint(widget->sizeHint());
    serverList_->setItemWidget(item, widget);
  }
}

void ServerListWidget::onAddServer() {
  ServerConfig newServer;
  newServer.id = ServerListManager::generateServerId();
  newServer.dateAdded = QDateTime::currentDateTime();
  newServer.port = 4433;

  showServerDialog(newServer, true);
}

void ServerListWidget::onEditServer(const QString& serverId) {
  auto server = serverManager_->getServer(serverId);
  if (server) {
    showServerDialog(*server, false);
  }
}

void ServerListWidget::onDeleteServer(const QString& serverId) {
  auto server = serverManager_->getServer(serverId);
  if (!server) return;

  auto reply = QMessageBox::question(
      this, "Delete Server",
      QString("Are you sure you want to delete '%1'?").arg(server->name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    serverManager_->removeServer(serverId);
    refreshServerList();
  }
}

void ServerListWidget::onToggleFavorite(const QString& serverId) {
  serverManager_->toggleFavorite(serverId);
  refreshServerList();
}

void ServerListWidget::onPingServer(const QString& serverId) {
  pingServerAsync(serverId);
}

void ServerListWidget::onPingAllServers() {
  auto servers = serverManager_->getAllServers();
  for (const auto& server : servers) {
    pingServerAsync(server.id);
  }
}

void ServerListWidget::onImportFromUri() {
  bool ok;
  QString uri = QInputDialog::getText(
      this, "Import from URI",
      "Enter VEIL connection URI (veil://host:port?name=...)",
      QLineEdit::Normal, "veil://", &ok);

  if (ok && !uri.trimmed().isEmpty()) {
    QString error;
    auto server = serverManager_->importFromUri(uri, &error);
    if (server) {
      serverManager_->addServer(*server);
      refreshServerList();
      QMessageBox::information(this, "Success", "Server imported successfully!");
    } else {
      QMessageBox::warning(this, "Import Failed", error);
    }
  }
}

void ServerListWidget::onImportFromFile() {
  QString filePath = QFileDialog::getOpenFileName(
      this, "Import Server Configuration", "",
      "JSON Files (*.json);;All Files (*)");

  if (!filePath.isEmpty()) {
    QString error;
    auto server = serverManager_->importFromJsonFile(filePath, &error);
    if (server) {
      serverManager_->addServer(*server);
      refreshServerList();
      QMessageBox::information(this, "Success", "Server imported successfully!");
    } else {
      QMessageBox::warning(this, "Import Failed", error);
    }
  }
}

void ServerListWidget::onExportServer(const QString& serverId) {
  QString json = serverManager_->exportServerToJson(serverId);
  if (json.isEmpty()) return;

  QString filePath = QFileDialog::getSaveFileName(
      this, "Export Server Configuration", "",
      "JSON Files (*.json);;All Files (*)");

  if (!filePath.isEmpty()) {
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(json.toUtf8());
      file.close();
      QMessageBox::information(this, "Success", "Server exported successfully!");
    } else {
      QMessageBox::warning(this, "Export Failed", "Failed to write file: " + file.errorString());
    }
  }
}

void ServerListWidget::onServerItemClicked(QListWidgetItem* item) {
  ServerListItem* widget = qobject_cast<ServerListItem*>(serverList_->itemWidget(item));
  if (widget) {
    emit serverSelected(widget->serverId());
  }
}

void ServerListWidget::onSearchTextChanged(const QString& text) {
  currentSearch_ = text;
  applySearchFilter();
}

void ServerListWidget::onSortModeChanged(int index) {
  currentSortMode_ = index;
  applySortMode();
  applySearchFilter();
}

QString ServerListWidget::getSelectedServerId() const {
  auto* item = serverList_->currentItem();
  if (!item) return "";

  ServerListItem* widget = qobject_cast<ServerListItem*>(serverList_->itemWidget(item));
  return widget ? widget->serverId() : "";
}

void ServerListWidget::showServerDialog(const ServerConfig& server, bool isNew) {
  // Create modal dialog
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(isNew ? "Add Server" : "Edit Server");
  dialog->setModal(true);
  dialog->setMinimumWidth(500);

  auto* layout = new QVBoxLayout(dialog);
  layout->setSpacing(16);
  layout->setContentsMargins(24, 24, 24, 24);

  // Name
  auto* nameLabel = new QLabel("Server Name:", dialog);
  auto* nameEdit = new QLineEdit(server.name, dialog);
  nameEdit->setPlaceholderText("My VPN Server");
  layout->addWidget(nameLabel);
  layout->addWidget(nameEdit);

  // Address
  auto* addressLabel = new QLabel("Server Address:", dialog);
  auto* addressEdit = new QLineEdit(server.address, dialog);
  addressEdit->setPlaceholderText("vpn.example.com or 192.168.1.1");
  layout->addWidget(addressLabel);
  layout->addWidget(addressEdit);

  // Port
  auto* portLabel = new QLabel("Port:", dialog);
  auto* portEdit = new QLineEdit(QString::number(server.port), dialog);
  portEdit->setPlaceholderText("4433");
  layout->addWidget(portLabel);
  layout->addWidget(portEdit);

  // Key File (optional)
  auto* keyFileLabel = new QLabel("Key File (optional, uses global if empty):", dialog);
  auto* keyFileLayout = new QHBoxLayout();
  auto* keyFileEdit = new QLineEdit(server.keyFilePath, dialog);
  auto* browseKeyButton = new QPushButton("Browse...", dialog);
  connect(browseKeyButton, &QPushButton::clicked, [keyFileEdit, dialog]() {
    QString path = QFileDialog::getOpenFileName(dialog, "Select Key File", "", "All Files (*)");
    if (!path.isEmpty()) keyFileEdit->setText(path);
  });
  keyFileLayout->addWidget(keyFileEdit);
  keyFileLayout->addWidget(browseKeyButton);
  layout->addWidget(keyFileLabel);
  layout->addLayout(keyFileLayout);

  // Notes
  auto* notesLabel = new QLabel("Notes:", dialog);
  auto* notesEdit = new QLineEdit(server.notes, dialog);
  notesEdit->setPlaceholderText("Optional notes about this server");
  layout->addWidget(notesLabel);
  layout->addWidget(notesEdit);

  // Buttons
  auto* buttonLayout = new QHBoxLayout();
  auto* cancelButton = new QPushButton("Cancel", dialog);
  auto* saveButton = new QPushButton(isNew ? "Add" : "Save", dialog);
  connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(saveButton, &QPushButton::clicked, [this, server, isNew, dialog, nameEdit, addressEdit, portEdit, keyFileEdit, notesEdit]() {
    ServerConfig edited = server;
    edited.name = nameEdit->text().trimmed();
    edited.address = addressEdit->text().trimmed();
    edited.port = static_cast<uint16_t>(portEdit->text().toInt());
    edited.keyFilePath = keyFileEdit->text().trimmed();
    edited.notes = notesEdit->text().trimmed();

    if (edited.name.isEmpty() || edited.address.isEmpty()) {
      QMessageBox::warning(dialog, "Validation Error", "Name and address are required.");
      return;
    }

    if (isNew) {
      serverManager_->addServer(edited);
    } else {
      serverManager_->updateServer(edited.id, edited);
    }

    refreshServerList();
    dialog->accept();
  });

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(saveButton);
  layout->addLayout(buttonLayout);

  dialog->exec();
  dialog->deleteLater();
}

void ServerListWidget::pingServerAsync(const QString& serverId) {
  auto server = serverManager_->getServer(serverId);
  if (!server) return;

  // Find widget and update UI
  for (int i = 0; i < serverList_->count(); ++i) {
    QListWidgetItem* item = serverList_->item(i);
    ServerListItem* widget = qobject_cast<ServerListItem*>(serverList_->itemWidget(item));
    if (widget && widget->serverId() == serverId) {
      // Simple TCP connect latency test
      auto* socket = new QTcpSocket(this);
      QElapsedTimer timer;
      timer.start();

      connect(socket, &QTcpSocket::connected, [this, &timer, serverId, widget, socket]() {
        int latency = static_cast<int>(timer.elapsed());
        serverManager_->updateLatency(serverId, latency);
        auto updatedServer = serverManager_->getServer(serverId);
        if (updatedServer) {
          widget->updateServer(*updatedServer);
        }
        socket->disconnectFromHost();
        socket->deleteLater();
      });

      connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
              [this, serverId, widget, socket](QAbstractSocket::SocketError) {
        serverManager_->updateLatency(serverId, -1);
        auto updatedServer = serverManager_->getServer(serverId);
        if (updatedServer) {
          widget->updateServer(*updatedServer);
        }
        socket->deleteLater();
      });

      // Set timeout
      QTimer::singleShot(5000, socket, [socket]() {
        if (socket->state() == QAbstractSocket::ConnectingState ||
            socket->state() == QAbstractSocket::HostLookupState) {
          socket->abort();
          socket->deleteLater();
        }
      });

      socket->connectToHost(server->address, server->port);
      break;
    }
  }
}

}  // namespace veil::gui
