#include "app_split_tunnel_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QScrollArea>
#include <algorithm>
#include <utility>

#include "common/gui/theme.h"

namespace veil::gui {

// AppListItem implementation

AppListItem::AppListItem(std::string appName,
                         std::string exePath,
                         bool isSystemApp,
                         QWidget* parent)
    : QWidget(parent),
      appName_(std::move(appName)),
      exePath_(std::move(exePath)),
      isSystemApp_(isSystemApp) {
  setupUi();
}

void AppListItem::setupUi() {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 4, 8, 4);

  // App icon/badge (for system apps)
  if (isSystemApp_) {
    badgeLabel_ = new QLabel("\U0001F6E1", this);  // Shield emoji for system apps
    badgeLabel_->setToolTip("System Application");
    badgeLabel_->setStyleSheet("font-size: 16px;");
    layout->addWidget(badgeLabel_);
  }

  // App information
  auto* infoLayout = new QVBoxLayout();
  infoLayout->setSpacing(2);

  nameLabel_ = new QLabel(QString::fromStdString(appName_), this);
  nameLabel_->setProperty("textStyle", "primary");
  nameLabel_->setStyleSheet("font-weight: bold;");
  infoLayout->addWidget(nameLabel_);

  pathLabel_ = new QLabel(QString::fromStdString(exePath_), this);
  pathLabel_->setProperty("textStyle", "secondary");
  pathLabel_->setStyleSheet("font-size: 10px; color: #888;");
  pathLabel_->setWordWrap(false);
  pathLabel_->setMaximumWidth(400);
  pathLabel_->setToolTip(QString::fromStdString(exePath_));
  infoLayout->addWidget(pathLabel_);

  layout->addLayout(infoLayout, 1);
  layout->addStretch();

  // Action buttons
  addToVpnButton_ = new QPushButton("Always VPN", this);
  addToVpnButton_->setToolTip("Add to 'Always use VPN' list");
  addToVpnButton_->setMaximumWidth(100);
  connect(addToVpnButton_, &QPushButton::clicked, [this]() {
    emit addToVpnRequested(exePath_);
  });
  layout->addWidget(addToVpnButton_);

  addToBypassButton_ = new QPushButton("Never VPN", this);
  addToBypassButton_->setToolTip("Add to 'Never use VPN' (bypass) list");
  addToBypassButton_->setMaximumWidth(100);
  connect(addToBypassButton_, &QPushButton::clicked, [this]() {
    emit addToBypassRequested(exePath_);
  });
  layout->addWidget(addToBypassButton_);
}

// AppSplitTunnelWidget implementation

AppSplitTunnelWidget::AppSplitTunnelWidget(QWidget* parent)
    : QWidget(parent) {
  setupUi();
  loadFromSettings();
}

void AppSplitTunnelWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);

  // Header
  auto* headerLabel = new QLabel("Per-Application Split Tunneling", this);
  headerLabel->setProperty("textStyle", "title");
  headerLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
  mainLayout->addWidget(headerLabel);

  auto* descLabel = new QLabel(
    "Configure which applications should always use VPN or bypass it. "
    "Browse installed or running applications, or add custom executable paths.",
    this);
  descLabel->setProperty("textStyle", "secondary");
  descLabel->setWordWrap(true);
  mainLayout->addWidget(descLabel);

  // Main content in horizontal layout: Browse | VPN List | Bypass List
  auto* contentLayout = new QHBoxLayout();

  // Left: Browse Applications
  auto* browseGroup = new QGroupBox("Browse Applications", this);
  auto* browseLayout = new QVBoxLayout(browseGroup);

  // App list type selector
  auto* typeRow = new QHBoxLayout();
  typeRow->addWidget(new QLabel("Show:", this));

  appListTypeCombo_ = new QComboBox(this);
  appListTypeCombo_->addItem("Installed Applications");
  appListTypeCombo_->addItem("Running Processes");
  connect(appListTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &AppSplitTunnelWidget::onAppListTypeChanged);
  typeRow->addWidget(appListTypeCombo_, 1);
  browseLayout->addLayout(typeRow);

  // Search bar
  searchEdit_ = new QLineEdit(this);
  searchEdit_->setPlaceholderText("Search applications...");
  connect(searchEdit_, &QLineEdit::textChanged,
          this, &AppSplitTunnelWidget::onSearchTextChanged);
  browseLayout->addWidget(searchEdit_);

  // Show system apps checkbox
  showSystemAppsCheck_ = new QCheckBox("Show system applications", this);
  connect(showSystemAppsCheck_, &QCheckBox::toggled,
          this, &AppSplitTunnelWidget::onShowSystemAppsToggled);
  browseLayout->addWidget(showSystemAppsCheck_);

  // Loading progress
  loadingProgress_ = new QProgressBar(this);
  loadingProgress_->setRange(0, 0);  // Indeterminate
  loadingProgress_->hide();
  browseLayout->addWidget(loadingProgress_);

  // Status label
  statusLabel_ = new QLabel(this);
  statusLabel_->setProperty("textStyle", "secondary");
  statusLabel_->setStyleSheet("font-size: 11px;");
  browseLayout->addWidget(statusLabel_);

  // App list (scrollable)
  browsableAppsList_ = new QListWidget(this);
  browsableAppsList_->setMinimumHeight(300);
  browseLayout->addWidget(browsableAppsList_);

  // Refresh buttons
  auto* refreshRow = new QHBoxLayout();
  refreshInstalledButton_ = new QPushButton("\U0001F504 Refresh", this);
  connect(refreshInstalledButton_, &QPushButton::clicked,
          this, &AppSplitTunnelWidget::onRefreshInstalledApps);
  refreshRow->addWidget(refreshInstalledButton_);
  browseLayout->addLayout(refreshRow);

  contentLayout->addWidget(browseGroup, 1);

  // Middle: VPN Apps List
  auto* vpnGroup = new QGroupBox("Always Use VPN", this);
  auto* vpnLayout = new QVBoxLayout(vpnGroup);

  vpnAppsCountLabel_ = new QLabel("0 applications", this);
  vpnAppsCountLabel_->setProperty("textStyle", "secondary");
  vpnLayout->addWidget(vpnAppsCountLabel_);

  vpnAppsList_ = new QListWidget(this);
  vpnAppsList_->setMinimumHeight(200);
  vpnAppsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  vpnLayout->addWidget(vpnAppsList_);

  removeVpnButton_ = new QPushButton("Remove Selected", this);
  removeVpnButton_->setEnabled(false);
  connect(removeVpnButton_, &QPushButton::clicked,
          this, &AppSplitTunnelWidget::onRemoveFromVpnList);
  connect(vpnAppsList_, &QListWidget::itemSelectionChanged, [this]() {
    removeVpnButton_->setEnabled(vpnAppsList_->selectedItems().count() > 0);
  });
  vpnLayout->addWidget(removeVpnButton_);

  contentLayout->addWidget(vpnGroup, 1);

  // Right: Bypass Apps List
  auto* bypassGroup = new QGroupBox("Never Use VPN (Bypass)", this);
  auto* bypassLayout = new QVBoxLayout(bypassGroup);

  bypassAppsCountLabel_ = new QLabel("0 applications", this);
  bypassAppsCountLabel_->setProperty("textStyle", "secondary");
  bypassLayout->addWidget(bypassAppsCountLabel_);

  bypassAppsList_ = new QListWidget(this);
  bypassAppsList_->setMinimumHeight(200);
  bypassAppsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  bypassLayout->addWidget(bypassAppsList_);

  removeBypassButton_ = new QPushButton("Remove Selected", this);
  removeBypassButton_->setEnabled(false);
  connect(removeBypassButton_, &QPushButton::clicked,
          this, &AppSplitTunnelWidget::onRemoveFromBypassList);
  connect(bypassAppsList_, &QListWidget::itemSelectionChanged, [this]() {
    removeBypassButton_->setEnabled(bypassAppsList_->selectedItems().count() > 0);
  });
  bypassLayout->addWidget(removeBypassButton_);

  contentLayout->addWidget(bypassGroup, 1);

  mainLayout->addLayout(contentLayout);

  // Bottom: Add custom path
  auto* customGroup = new QGroupBox("Add Custom Executable", this);
  auto* customLayout = new QHBoxLayout(customGroup);

  customPathEdit_ = new QLineEdit(this);
  customPathEdit_->setPlaceholderText(R"(C:\Path\to\application.exe)");
  customLayout->addWidget(customPathEdit_, 1);

  browseCustomButton_ = new QPushButton("Browse...", this);
  connect(browseCustomButton_, &QPushButton::clicked,
          this, &AppSplitTunnelWidget::onAddCustomPath);
  customLayout->addWidget(browseCustomButton_);

  mainLayout->addWidget(customGroup);

  // Load apps initially
  onRefreshInstalledApps();
}

void AppSplitTunnelWidget::onRefreshInstalledApps() {
#ifdef _WIN32
  isLoading_ = true;
  loadingProgress_->show();
  statusLabel_->setText("Loading installed applications...");

  // In a real implementation, this should be done in a background thread
  installedApps_ = veil::windows::AppEnumerator::GetInstalledApplications();

  populateInstalledApps();

  statusLabel_->setText(QString("Found %1 applications").arg(installedApps_.size()));
  loadingProgress_->hide();
  isLoading_ = false;
#else
  statusLabel_->setText("App enumeration is only available on Windows");
#endif
}

void AppSplitTunnelWidget::onRefreshRunningApps() {
#ifdef _WIN32
  isLoading_ = true;
  loadingProgress_->show();
  statusLabel_->setText("Loading running processes...");

  runningApps_ = veil::windows::AppEnumerator::GetRunningProcesses();

  populateRunningApps();

  statusLabel_->setText(QString("Found %1 running processes").arg(runningApps_.size()));
  loadingProgress_->hide();
  isLoading_ = false;
#else
  statusLabel_->setText("Process enumeration is only available on Windows");
#endif
}

void AppSplitTunnelWidget::populateInstalledApps() {
#ifdef _WIN32
  browsableAppsList_->clear();

  for (const auto& app : installedApps_) {
    // Filter system apps if checkbox is not checked
    if (app.isSystemApp && !showSystemApps_) {
      continue;
    }

    // Apply search filter
    if (!matchesSearch(app.name, app.executable)) {
      continue;
    }

    // Skip apps without executable
    if (app.executable.empty()) {
      continue;
    }

    auto* itemWidget = new AppListItem(app.name, app.executable, app.isSystemApp, this);
    connect(itemWidget, &AppListItem::addToVpnRequested,
            this, &AppSplitTunnelWidget::onAddToVpnList);
    connect(itemWidget, &AppListItem::addToBypassRequested,
            this, &AppSplitTunnelWidget::onAddToBypassList);

    auto* listItem = new QListWidgetItem(browsableAppsList_);
    listItem->setSizeHint(itemWidget->sizeHint());
    browsableAppsList_->setItemWidget(listItem, itemWidget);
  }
#endif
}

void AppSplitTunnelWidget::populateRunningApps() {
#ifdef _WIN32
  browsableAppsList_->clear();

  for (const auto& app : runningApps_) {
    // Filter system apps if checkbox is not checked
    if (app.isSystemApp && !showSystemApps_) {
      continue;
    }

    // Apply search filter
    if (!matchesSearch(app.name, app.executable)) {
      continue;
    }

    auto* itemWidget = new AppListItem(app.name, app.executable, app.isSystemApp, this);
    connect(itemWidget, &AppListItem::addToVpnRequested,
            this, &AppSplitTunnelWidget::onAddToVpnList);
    connect(itemWidget, &AppListItem::addToBypassRequested,
            this, &AppSplitTunnelWidget::onAddToBypassList);

    auto* listItem = new QListWidgetItem(browsableAppsList_);
    listItem->setSizeHint(itemWidget->sizeHint());
    browsableAppsList_->setItemWidget(listItem, itemWidget);
  }
#endif
}

void AppSplitTunnelWidget::onSearchTextChanged(const QString& text) {
  currentSearch_ = text;
  applySearchFilter();
}

void AppSplitTunnelWidget::applySearchFilter() {
  if (appListTypeCombo_->currentIndex() == 0) {
    populateInstalledApps();
  } else {
    populateRunningApps();
  }
}

bool AppSplitTunnelWidget::matchesSearch(const std::string& appName, const std::string& exePath) const {
  if (currentSearch_.isEmpty()) return true;

  QString search = currentSearch_.toLower();
  QString name = QString::fromStdString(appName).toLower();
  QString path = QString::fromStdString(exePath).toLower();

  return name.contains(search) || path.contains(search);
}

void AppSplitTunnelWidget::onAppListTypeChanged(int index) {
  if (index == 0) {
    onRefreshInstalledApps();
  } else {
    onRefreshRunningApps();
  }
}

void AppSplitTunnelWidget::onAddToVpnList(const std::string& exePath) {
  // Check if already in list
  if (std::find(vpnApps_.begin(), vpnApps_.end(), exePath) != vpnApps_.end()) {
    QMessageBox::information(this, "Already Added",
                             "This application is already in the VPN list.");
    return;
  }

  // Remove from bypass list if present
  auto it = std::find(bypassApps_.begin(), bypassApps_.end(), exePath);
  if (it != bypassApps_.end()) {
    bypassApps_.erase(it);
    populateAppList(bypassApps_, bypassAppsList_);
  }

  vpnApps_.push_back(exePath);
  populateAppList(vpnApps_, vpnAppsList_);

  emit settingsChanged();
}

void AppSplitTunnelWidget::onAddToBypassList(const std::string& exePath) {
  // Check if already in list
  if (std::find(bypassApps_.begin(), bypassApps_.end(), exePath) != bypassApps_.end()) {
    QMessageBox::information(this, "Already Added",
                             "This application is already in the bypass list.");
    return;
  }

  // Remove from VPN list if present
  auto it = std::find(vpnApps_.begin(), vpnApps_.end(), exePath);
  if (it != vpnApps_.end()) {
    vpnApps_.erase(it);
    populateAppList(vpnApps_, vpnAppsList_);
  }

  bypassApps_.push_back(exePath);
  populateAppList(bypassApps_, bypassAppsList_);

  emit settingsChanged();
}

void AppSplitTunnelWidget::onRemoveFromVpnList() {
  auto selected = vpnAppsList_->selectedItems();
  for (auto* item : selected) {
    std::string exePath = item->text().toStdString();
    auto it = std::find(vpnApps_.begin(), vpnApps_.end(), exePath);
    if (it != vpnApps_.end()) {
      vpnApps_.erase(it);
    }
  }

  populateAppList(vpnApps_, vpnAppsList_);
  emit settingsChanged();
}

void AppSplitTunnelWidget::onRemoveFromBypassList() {
  auto selected = bypassAppsList_->selectedItems();
  for (auto* item : selected) {
    std::string exePath = item->text().toStdString();
    auto it = std::find(bypassApps_.begin(), bypassApps_.end(), exePath);
    if (it != bypassApps_.end()) {
      bypassApps_.erase(it);
    }
  }

  populateAppList(bypassApps_, bypassAppsList_);
  emit settingsChanged();
}

void AppSplitTunnelWidget::onAddCustomPath() {
  QString path = QFileDialog::getOpenFileName(
    this,
    "Select Executable",
    "C:\\",
    "Executable Files (*.exe *.com *.bat *.cmd);;All Files (*.*)"
  );

  if (path.isEmpty()) return;

#ifdef _WIN32
  if (!veil::windows::AppEnumerator::IsValidExecutable(path.toStdString())) {
    QMessageBox::warning(this, "Invalid Executable",
                         "The selected file is not a valid executable.");
    return;
  }
#endif

  customPathEdit_->setText(path);

  // Ask user which list to add to
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Add to List");
  msgBox.setText("Add this application to:");
  QPushButton* vpnButton = msgBox.addButton("Always VPN", QMessageBox::AcceptRole);
  QPushButton* bypassButton = msgBox.addButton("Never VPN (Bypass)", QMessageBox::AcceptRole);
  msgBox.addButton(QMessageBox::Cancel);

  msgBox.exec();

  if (msgBox.clickedButton() == vpnButton) {
    onAddToVpnList(path.toStdString());
  } else if (msgBox.clickedButton() == bypassButton) {
    onAddToBypassList(path.toStdString());
  }

  customPathEdit_->clear();
}

void AppSplitTunnelWidget::onShowSystemAppsToggled(bool checked) {
  showSystemApps_ = checked;
  applySearchFilter();
}

void AppSplitTunnelWidget::populateAppList(const std::vector<std::string>& apps, QListWidget* listWidget) {
  listWidget->clear();

  for (const auto& app : apps) {
    listWidget->addItem(QString::fromStdString(app));
  }

  // Update count labels
  if (listWidget == vpnAppsList_) {
    vpnAppsCountLabel_->setText(QString("%1 application(s)").arg(apps.size()));
  } else if (listWidget == bypassAppsList_) {
    bypassAppsCountLabel_->setText(QString("%1 application(s)").arg(apps.size()));
  }
}

void AppSplitTunnelWidget::loadFromSettings() {
  QSettings settings;

  // Load VPN apps list
  QStringList vpnAppsQt = settings.value("routing/vpnApps", QStringList()).toStringList();
  vpnApps_.clear();
  for (const auto& app : vpnAppsQt) {
    vpnApps_.push_back(app.toStdString());
  }

  // Load bypass apps list
  QStringList bypassAppsQt = settings.value("routing/bypassApps", QStringList()).toStringList();
  bypassApps_.clear();
  for (const auto& app : bypassAppsQt) {
    bypassApps_.push_back(app.toStdString());
  }

  populateAppList(vpnApps_, vpnAppsList_);
  populateAppList(bypassApps_, bypassAppsList_);
}

void AppSplitTunnelWidget::saveToSettings() {
  QSettings settings;

  // Save VPN apps list
  QStringList vpnAppsQt;
  for (const auto& app : vpnApps_) {
    vpnAppsQt.append(QString::fromStdString(app));
  }
  settings.setValue("routing/vpnApps", vpnAppsQt);

  // Save bypass apps list
  QStringList bypassAppsQt;
  for (const auto& app : bypassApps_) {
    bypassAppsQt.append(QString::fromStdString(app));
  }
  settings.setValue("routing/bypassApps", bypassAppsQt);
}

std::vector<std::string> AppSplitTunnelWidget::getVpnApps() const {
  return vpnApps_;
}

std::vector<std::string> AppSplitTunnelWidget::getBypassApps() const {
  return bypassApps_;
}

void AppSplitTunnelWidget::setVpnApps(const std::vector<std::string>& apps) {
  vpnApps_ = apps;
  populateAppList(vpnApps_, vpnAppsList_);
}

void AppSplitTunnelWidget::setBypassApps(const std::vector<std::string>& apps) {
  bypassApps_ = apps;
  populateAppList(bypassApps_, bypassAppsList_);
}

}  // namespace veil::gui
