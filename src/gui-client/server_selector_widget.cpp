#include "server_selector_widget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTcpSocket>
#include <QElapsedTimer>

#include "common/gui/theme.h"

namespace veil::gui {

ServerSelectorWidget::ServerSelectorWidget(QWidget* parent)
    : QWidget(parent), serverManager_(std::make_unique<ServerListManager>()) {
  setupUi();
  refreshServers();

  // Auto-refresh latency every 60 seconds for current server
  autoRefreshTimer_ = new QTimer(this);
  autoRefreshTimer_->setInterval(60000);
  connect(autoRefreshTimer_, &QTimer::timeout, this, &ServerSelectorWidget::onRefreshLatency);
  autoRefreshTimer_->start();
}

void ServerSelectorWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(8);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Label
  auto* label = new QLabel("Server", this);
  label->setStyleSheet("color: #8b949e; font-size: 12px; font-weight: 500;");
  mainLayout->addWidget(label);

  // Server selection row
  auto* selectionLayout = new QHBoxLayout();
  selectionLayout->setSpacing(8);

  // Server dropdown
  serverCombo_ = new QComboBox(this);
  serverCombo_->setFixedHeight(36);
  serverCombo_->setStyleSheet(R"(
    QComboBox {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 8px;
      padding: 6px 12px;
      color: #f0f6fc;
      font-size: 14px;
      font-weight: 500;
    }
    QComboBox:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.2);
    }
    QComboBox::drop-down {
      border: none;
      width: 20px;
    }
    QComboBox::down-arrow {
      image: none;
      border-left: 4px solid transparent;
      border-right: 4px solid transparent;
      border-top: 6px solid #8b949e;
      margin-right: 6px;
    }
    QComboBox QAbstractItemView {
      background: #161b22;
      border: 1px solid #30363d;
      border-radius: 8px;
      color: #f0f6fc;
      selection-background-color: rgba(88, 166, 255, 0.15);
      selection-color: #58a6ff;
      padding: 4px;
    }
    QComboBox QAbstractItemView::item {
      padding: 8px 12px;
      border-radius: 6px;
    }
  )");
  connect(serverCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ServerSelectorWidget::onServerSelectionChanged);
  selectionLayout->addWidget(serverCombo_, 1);

  // Latency display
  latencyLabel_ = new QLabel(this);
  latencyLabel_->setFixedWidth(60);
  latencyLabel_->setAlignment(Qt::AlignCenter);
  latencyLabel_->setTextFormat(Qt::RichText);
  latencyLabel_->setStyleSheet(R"(
    QLabel {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 8px;
      padding: 6px;
    }
  )");
  selectionLayout->addWidget(latencyLabel_);

  // Refresh button
  refreshButton_ = new QPushButton("\u21BB", this);
  refreshButton_->setFixedSize(36, 36);
  refreshButton_->setCursor(Qt::PointingHandCursor);
  refreshButton_->setToolTip("Refresh latency");
  refreshButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(88, 166, 255, 0.1);
      border: 1px solid rgba(88, 166, 255, 0.3);
      border-radius: 8px;
      color: #58a6ff;
      font-size: 16px;
      font-weight: 600;
    }
    QPushButton:hover {
      background: rgba(88, 166, 255, 0.15);
      border-color: #58a6ff;
    }
  )");
  connect(refreshButton_, &QPushButton::clicked, this, &ServerSelectorWidget::onRefreshLatency);
  selectionLayout->addWidget(refreshButton_);

  // Manage servers button
  manageButton_ = new QPushButton("Manage", this);
  manageButton_->setFixedHeight(36);
  manageButton_->setCursor(Qt::PointingHandCursor);
  manageButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 8px;
      color: #8b949e;
      padding: 0 16px;
      font-size: 13px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.2);
      color: #f0f6fc;
    }
  )");
  connect(manageButton_, &QPushButton::clicked, this, &ServerSelectorWidget::onManageServersClicked);
  selectionLayout->addWidget(manageButton_);

  mainLayout->addLayout(selectionLayout);
}

void ServerSelectorWidget::refreshServers() {
  serverManager_->loadServers();

  QString currentId = getCurrentServerId();

  serverCombo_->clear();

  auto servers = serverManager_->getServersSortedByLatency();

  int selectedIndex = -1;
  for (int i = 0; i < servers.size(); ++i) {
    const auto& server = servers[i];
    QString displayText = server.name;
    if (server.isFavorite) {
      displayText = "★ " + displayText;
    }
    serverCombo_->addItem(displayText, server.id);

    if (server.id == currentId) {
      selectedIndex = i;
    }
  }

  if (selectedIndex >= 0) {
    serverCombo_->setCurrentIndex(selectedIndex);
  } else if (serverCombo_->count() > 0) {
    serverCombo_->setCurrentIndex(0);
  }

  updateLatencyDisplay();
}

QString ServerSelectorWidget::getCurrentServerId() const {
  if (serverCombo_->currentIndex() < 0) {
    return serverManager_->getCurrentServerId();
  }
  return serverCombo_->currentData().toString();
}

void ServerSelectorWidget::setCurrentServerId(const QString& id) {
  for (int i = 0; i < serverCombo_->count(); ++i) {
    if (serverCombo_->itemData(i).toString() == id) {
      serverCombo_->setCurrentIndex(i);
      break;
    }
  }
}

std::optional<ServerConfig> ServerSelectorWidget::getCurrentServer() const {
  return serverManager_->getServer(getCurrentServerId());
}

void ServerSelectorWidget::onServerSelectionChanged(int index) {
  if (index < 0) return;

  QString serverId = serverCombo_->itemData(index).toString();
  serverManager_->setCurrentServerId(serverId);

  updateLatencyDisplay();
  emit serverChanged(serverId);
}

void ServerSelectorWidget::onManageServersClicked() {
  emit manageServersRequested();
}

void ServerSelectorWidget::onRefreshLatency() {
  auto server = getCurrentServer();
  if (!server) {
    latencyLabel_->setText("<span style='color: #6e7681;'>—</span>");
    return;
  }

  // Show "pinging" state
  latencyLabel_->setText("<span style='color: #d29922;'>...</span>");

  // Perform async ping
  auto* socket = new QTcpSocket(this);
  QElapsedTimer timer;
  timer.start();

  QString serverId = server->id;
  connect(socket, &QTcpSocket::connected, [this, &timer, serverId, socket]() {
    int latency = static_cast<int>(timer.elapsed());
    serverManager_->updateLatency(serverId, latency);
    updateLatencyDisplay();
    socket->disconnectFromHost();
    socket->deleteLater();
  });

  connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
          [this, serverId, socket](QAbstractSocket::SocketError) {
    serverManager_->updateLatency(serverId, -1);
    updateLatencyDisplay();
    socket->deleteLater();
  });

  // Timeout after 5 seconds
  QTimer::singleShot(5000, socket, [socket]() {
    if (socket->state() == QAbstractSocket::ConnectingState ||
        socket->state() == QAbstractSocket::HostLookupState) {
      socket->abort();
      socket->deleteLater();
    }
  });

  socket->connectToHost(server->address, server->port);
}

void ServerSelectorWidget::updateLatencyDisplay() {
  auto server = getCurrentServer();
  if (!server) {
    latencyLabel_->setText("<span style='color: #6e7681;'>—</span>");
    return;
  }

  latencyLabel_->setText(formatLatencyBadge(server->lastLatencyMs));
}

QString ServerSelectorWidget::formatLatencyBadge(int latencyMs) const {
  if (latencyMs < 0) {
    return "<span style='color: #6e7681; font-size: 12px;'>—</span>";
  }

  QString color;
  if (latencyMs < 50) {
    color = "#3fb950";  // Green - excellent
  } else if (latencyMs < 100) {
    color = "#58a6ff";  // Blue - good
  } else if (latencyMs < 200) {
    color = "#d29922";  // Yellow - fair
  } else {
    color = "#f85149";  // Red - poor
  }

  return QString("<span style='color: %1; font-size: 13px; font-weight: 600;'>%2ms</span>")
      .arg(color)
      .arg(latencyMs);
}

}  // namespace veil::gui
