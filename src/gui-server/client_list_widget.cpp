#include "client_list_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFrame>
#include <QDateTime>
#include <QMessageBox>

#include "common/gui/theme.h"

namespace veil::gui {

ClientListWidget::ClientListWidget(QWidget* parent) : QWidget(parent) {
  setupUi();

  // Setup demo timer
  demoTimer_ = new QTimer(this);
  connect(demoTimer_, &QTimer::timeout, this, &ClientListWidget::updateDemoData);

  // Start demo mode - add some sample clients
  QTimer::singleShot(2500, this, [this]() {
    // Add some demo clients
    ClientInfo client1;
    client1.sessionId = "a1b2c3d4";
    client1.tunnelIp = "10.0.0.2";
    client1.endpoint = "192.168.1.100:54321";
    client1.connectedAt = QDateTime::currentSecsSinceEpoch() - 3600;
    client1.bytesSent = 15728640;
    client1.bytesReceived = 52428800;
    client1.latencyMs = 25;
    client1.dpiMode = "IoT Mimic";
    demoClients_.append(client1);
    addClient(client1);

    ClientInfo client2;
    client2.sessionId = "e5f6g7h8";
    client2.tunnelIp = "10.0.0.3";
    client2.endpoint = "192.168.1.101:54322";
    client2.connectedAt = QDateTime::currentSecsSinceEpoch() - 1800;
    client2.bytesSent = 5242880;
    client2.bytesReceived = 10485760;
    client2.latencyMs = 42;
    client2.dpiMode = "QUIC-Like";
    demoClients_.append(client2);
    addClient(client2);

    ClientInfo client3;
    client3.sessionId = "i9j0k1l2";
    client3.tunnelIp = "10.0.0.4";
    client3.endpoint = "192.168.1.102:54323";
    client3.connectedAt = QDateTime::currentSecsSinceEpoch() - 600;
    client3.bytesSent = 1048576;
    client3.bytesReceived = 2097152;
    client3.latencyMs = 120;
    client3.dpiMode = "Random-Noise";
    demoClients_.append(client3);
    addClient(client3);

    demoTimer_->start(2000);
  });
}

void ClientListWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // === Header Row ===
  auto* headerRow = new QHBoxLayout();
  headerRow->setSpacing(12);

  auto* titleLabel = new QLabel("Connected Clients", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 600;")
                                .arg(fonts::kFontSizeTitle()));
  headerRow->addWidget(titleLabel);

  clientCountLabel_ = new QLabel("(0)", this);
  clientCountLabel_->setStyleSheet(QString("font-size: %1px; color: %2;")
                                       .arg(fonts::kFontSizeBody())
                                       .arg(colors::dark::kTextSecondary));
  headerRow->addWidget(clientCountLabel_);

  headerRow->addStretch();

  // Search box
  searchEdit_ = new QLineEdit(this);
  searchEdit_->setPlaceholderText("Search clients...");
  searchEdit_->setFixedWidth(200);
  searchEdit_->setStyleSheet(QString(R"(
    QLineEdit {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: %1px;
      padding: 8px 12px;
      color: %2;
    }
    QLineEdit:focus {
      border-color: %3;
    }
  )").arg(spacing::kBorderRadiusSmall())
    .arg(colors::dark::kTextPrimary)
    .arg(colors::dark::kAccentPrimary));
  connect(searchEdit_, &QLineEdit::textChanged, this, &ClientListWidget::onSearchTextChanged);
  headerRow->addWidget(searchEdit_);

  // Refresh button
  refreshButton_ = new QPushButton("Refresh", this);
  refreshButton_->setCursor(Qt::PointingHandCursor);
  refreshButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.2);
      border-radius: %1px;
      color: %2;
      padding: 8px 16px;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.05);
      border-color: %3;
    }
  )").arg(spacing::kBorderRadiusSmall())
    .arg(colors::dark::kTextSecondary)
    .arg(colors::dark::kAccentPrimary));
  headerRow->addWidget(refreshButton_);

  // Disconnect All button
  disconnectAllButton_ = new QPushButton("Disconnect All", this);
  disconnectAllButton_->setCursor(Qt::PointingHandCursor);
  disconnectAllButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: 1px solid %1;
      border-radius: %2px;
      color: %1;
      padding: 8px 16px;
    }
    QPushButton:hover {
      background: rgba(255, 107, 107, 0.1);
    }
  )").arg(colors::dark::kAccentError)
    .arg(spacing::kBorderRadiusSmall()));
  connect(disconnectAllButton_, &QPushButton::clicked, this, [this]() {
    if (tableWidget_->rowCount() > 0) {
      auto reply = QMessageBox::question(this, "Disconnect All Clients",
                                          "Are you sure you want to disconnect all clients?",
                                          QMessageBox::Yes | QMessageBox::No);
      if (reply == QMessageBox::Yes) {
        clearAllClients();
      }
    }
  });
  headerRow->addWidget(disconnectAllButton_);

  mainLayout->addLayout(headerRow);

  // === Table ===
  auto* tableContainer = new QFrame(this);
  tableContainer->setStyleSheet(QString(R"(
    QFrame {
      background: rgba(30, 35, 45, 0.8);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: %1px;
    }
  )").arg(spacing::kBorderRadiusMedium()));

  auto* tableLayout = new QVBoxLayout(tableContainer);
  tableLayout->setContentsMargins(0, 0, 0, 0);

  tableWidget_ = new QTableWidget(0, 7, this);
  tableWidget_->setHorizontalHeaderLabels({
      "Session ID", "Tunnel IP", "Endpoint", "Uptime", "Traffic", "Latency", "DPI Mode"});

  // Table styling
  tableWidget_->setStyleSheet(QString(R"(
    QTableWidget {
      background: transparent;
      border: none;
      gridline-color: rgba(255, 255, 255, 0.05);
      selection-background-color: rgba(58, 175, 255, 0.2);
    }
    QTableWidget::item {
      padding: 12px 8px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    }
    QTableWidget::item:selected {
      background: rgba(58, 175, 255, 0.15);
    }
    QHeaderView::section {
      background: rgba(255, 255, 255, 0.03);
      color: %1;
      font-weight: 600;
      padding: 12px 8px;
      border: none;
      border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    }
  )").arg(colors::dark::kTextSecondary));

  tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
  tableWidget_->setShowGrid(false);
  tableWidget_->setAlternatingRowColors(false);
  tableWidget_->verticalHeader()->setVisible(false);
  tableWidget_->horizontalHeader()->setStretchLastSection(true);
  tableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

  // Column widths
  tableWidget_->setColumnWidth(0, 100);  // Session ID
  tableWidget_->setColumnWidth(1, 100);  // Tunnel IP
  tableWidget_->setColumnWidth(2, 160);  // Endpoint
  tableWidget_->setColumnWidth(3, 100);  // Uptime
  tableWidget_->setColumnWidth(4, 140);  // Traffic
  tableWidget_->setColumnWidth(5, 80);   // Latency
  tableWidget_->setColumnWidth(6, 120);  // DPI Mode

  // Context menu
  tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tableWidget_, &QTableWidget::customContextMenuRequested,
          this, &ClientListWidget::onTableContextMenu);

  contextMenu_ = new QMenu(this);
  contextMenu_->setStyleSheet(QString(R"(
    QMenu {
      background: %1;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      padding: 4px;
    }
    QMenu::item {
      padding: 8px 24px;
      border-radius: 4px;
    }
    QMenu::item:selected {
      background: rgba(58, 175, 255, 0.2);
    }
  )").arg(colors::dark::kBackgroundSecondary));

  viewDetailsAction_ = contextMenu_->addAction("View Details");
  connect(viewDetailsAction_, &QAction::triggered, this, &ClientListWidget::onViewClientDetails);

  contextMenu_->addSeparator();

  disconnectAction_ = contextMenu_->addAction("Disconnect");
  disconnectAction_->setIcon(QIcon());  // Could add icon here
  connect(disconnectAction_, &QAction::triggered, this, &ClientListWidget::onDisconnectClient);

  tableLayout->addWidget(tableWidget_);
  mainLayout->addWidget(tableContainer, 1);
}

void ClientListWidget::addClient(const ClientInfo& client) {
  int row = tableWidget_->rowCount();
  tableWidget_->insertRow(row);
  updateClientRow(row, client);
  clientCountLabel_->setText(QString("(%1)").arg(tableWidget_->rowCount()));
}

void ClientListWidget::removeClient(const QString& sessionId) {
  int row = findClientRow(sessionId);
  if (row >= 0) {
    tableWidget_->removeRow(row);
    clientCountLabel_->setText(QString("(%1)").arg(tableWidget_->rowCount()));
  }
}

void ClientListWidget::updateClient(const ClientInfo& client) {
  int row = findClientRow(client.sessionId);
  if (row >= 0) {
    updateClientRow(row, client);
  }
}

void ClientListWidget::clearAllClients() {
  tableWidget_->setRowCount(0);
  clientCountLabel_->setText("(0)");
  demoClients_.clear();
}

void ClientListWidget::onSearchTextChanged(const QString& text) {
  QString searchText = text.toLower();
  for (int row = 0; row < tableWidget_->rowCount(); ++row) {
    bool match = false;
    for (int col = 0; col < tableWidget_->columnCount(); ++col) {
      QTableWidgetItem* item = tableWidget_->item(row, col);
      if ((item != nullptr) && item->text().toLower().contains(searchText)) {
        match = true;
        break;
      }
    }
    tableWidget_->setRowHidden(row, !match && !searchText.isEmpty());
  }
}

void ClientListWidget::onTableContextMenu(const QPoint& pos) {
  QTableWidgetItem* item = tableWidget_->itemAt(pos);
  if (item != nullptr) {
    tableWidget_->selectRow(item->row());
    contextMenu_->popup(tableWidget_->viewport()->mapToGlobal(pos));
  }
}

void ClientListWidget::onDisconnectClient() {
  int row = tableWidget_->currentRow();
  if (row >= 0) {
    QTableWidgetItem* item = tableWidget_->item(row, 0);
    if (item != nullptr) {
      QString sessionId = item->text();
      emit clientDisconnectRequested(sessionId);

      // Demo: remove from local list
      for (int i = 0; i < demoClients_.size(); ++i) {
        if (demoClients_[i].sessionId == sessionId) {
          demoClients_.removeAt(i);
          break;
        }
      }
      removeClient(sessionId);
    }
  }
}

void ClientListWidget::onViewClientDetails() {
  int row = tableWidget_->currentRow();
  if (row >= 0) {
    QTableWidgetItem* item = tableWidget_->item(row, 0);
    if (item != nullptr) {
      emit clientDetailsRequested(item->text());

      // Demo: show message box with details
      QString details;
      for (int col = 0; col < tableWidget_->columnCount(); ++col) {
        QTableWidgetItem* colItem = tableWidget_->item(row, col);
        if (colItem != nullptr) {
          details += tableWidget_->horizontalHeaderItem(col)->text() + ": " +
                     colItem->text() + "\n";
        }
      }
      QMessageBox::information(this, "Client Details", details);
    }
  }
}

void ClientListWidget::updateDemoData() {
  // Update demo client metrics
  for (auto& client : demoClients_) {
    client.bytesSent += static_cast<uint64_t>(1024 + (rand() % 10240));
    client.bytesReceived += static_cast<uint64_t>(512 + (rand() % 20480));
    client.latencyMs = qMax(10, client.latencyMs + (rand() % 20) - 10);
    updateClient(client);
  }

  // Occasionally add/remove clients for demo
  if (rand() % 20 == 0 && demoClients_.size() < 10) {
    ClientInfo newClient;
    newClient.sessionId = QString("%1%2%3%4")
                              .arg(QChar('a' + rand() % 26))
                              .arg(rand() % 10)
                              .arg(QChar('a' + rand() % 26))
                              .arg(rand() % 10);
    newClient.tunnelIp = QString("10.0.0.%1").arg(5 + demoClients_.size());
    newClient.endpoint = QString("192.168.1.%1:%2")
                             .arg(100 + rand() % 50)
                             .arg(50000 + rand() % 10000);
    newClient.connectedAt = QDateTime::currentSecsSinceEpoch();
    newClient.bytesSent = 0;
    newClient.bytesReceived = 0;
    newClient.latencyMs = 20 + rand() % 80;

    static const char* modes[] = {"IoT Mimic", "QUIC-Like", "Random-Noise", "Trickle"};
    newClient.dpiMode = modes[rand() % 4];

    demoClients_.append(newClient);
    addClient(newClient);
  }
}

void ClientListWidget::updateClientRow(int row, const ClientInfo& client) {
  auto createItem = [](const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
  };

  // Session ID (monospace style)
  auto* sessionItem = createItem(client.sessionId);
  sessionItem->setFont(QFont("Consolas, Monaco, monospace", 11));
  tableWidget_->setItem(row, 0, sessionItem);

  // Tunnel IP
  tableWidget_->setItem(row, 1, createItem(client.tunnelIp));

  // Endpoint
  auto* endpointItem = createItem(client.endpoint);
  endpointItem->setForeground(QColor(colors::dark::kTextSecondary));
  tableWidget_->setItem(row, 2, endpointItem);

  // Uptime
  tableWidget_->setItem(row, 3, createItem(formatUptime(client.connectedAt)));

  // Traffic (TX/RX)
  QString traffic = QString("%1 / %2")
                        .arg(formatBytes(client.bytesSent))
                        .arg(formatBytes(client.bytesReceived));
  tableWidget_->setItem(row, 4, createItem(traffic));

  // Latency (color-coded)
  auto* latencyItem = createItem(formatLatency(client.latencyMs));
  if (client.latencyMs < 50) {
    latencyItem->setForeground(QColor(colors::dark::kAccentSuccess));
  } else if (client.latencyMs < 100) {
    latencyItem->setForeground(QColor(colors::dark::kAccentWarning));
  } else {
    latencyItem->setForeground(QColor(colors::dark::kAccentError));
  }
  tableWidget_->setItem(row, 5, latencyItem);

  // DPI Mode
  auto* modeItem = createItem(client.dpiMode);
  modeItem->setForeground(QColor(colors::dark::kAccentPrimary));
  tableWidget_->setItem(row, 6, modeItem);
}

int ClientListWidget::findClientRow(const QString& sessionId) const {
  for (int row = 0; row < tableWidget_->rowCount(); ++row) {
    QTableWidgetItem* item = tableWidget_->item(row, 0);
    if ((item != nullptr) && item->text() == sessionId) {
      return row;
    }
  }
  return -1;
}

QString ClientListWidget::formatBytes(uint64_t bytes) const {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unitIndex = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unitIndex < 4) {
    size /= 1024;
    unitIndex++;
  }

  if (unitIndex == 0) {
    return QString("%1 %2").arg(bytes).arg(units[unitIndex]);
  }
  return QString("%1 %2").arg(size, 0, 'f', 1).arg(units[unitIndex]);
}

QString ClientListWidget::formatUptime(qint64 connectedAt) const {
  qint64 seconds = QDateTime::currentSecsSinceEpoch() - connectedAt;
  int hours = static_cast<int>(seconds / 3600);
  int minutes = static_cast<int>((seconds % 3600) / 60);
  int secs = static_cast<int>(seconds % 60);

  if (hours > 0) {
    return QString("%1h %2m").arg(hours).arg(minutes);
  } else if (minutes > 0) {
    return QString("%1m %2s").arg(minutes).arg(secs);
  } else {
    return QString("%1s").arg(secs);
  }
}

QString ClientListWidget::formatLatency(int latencyMs) const {
  return QString("%1 ms").arg(latencyMs);
}

}  // namespace veil::gui
