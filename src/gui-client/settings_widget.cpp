#include "settings_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QRegularExpression>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QApplication>

#include "common/gui/theme.h"

namespace veil::gui {

SettingsWidget::SettingsWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
  loadSettings();
}

void SettingsWidget::setupUi() {
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
  connect(backButton, &QPushButton::clicked, this, &SettingsWidget::backRequested);
  headerLayout->addWidget(backButton);

  headerLayout->addStretch();
  mainLayout->addLayout(headerLayout);

  // Title
  auto* titleLabel = new QLabel("Settings", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 700; color: #f0f6fc; margin-bottom: 8px;")
                                .arg(fonts::kFontSizeHeadline));
  mainLayout->addWidget(titleLabel);

  // === Scrollable content ===
  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

  auto* scrollWidget = new QWidget();
  scrollWidget->setStyleSheet("background: transparent;");
  auto* scrollLayout = new QVBoxLayout(scrollWidget);
  scrollLayout->setSpacing(16);
  scrollLayout->setContentsMargins(0, 0, 12, 0);  // Right margin for scrollbar

  // Create sections
  createServerSection(scrollWidget);
  createCryptoSection(scrollWidget);
  createTunInterfaceSection(scrollWidget);
  createRoutingSection(scrollWidget);
  createConnectionSection(scrollWidget);
  createDpiBypassSection(scrollWidget);
  createAdvancedSection(scrollWidget);

  scrollLayout->addStretch();
  scrollArea->setWidget(scrollWidget);
  mainLayout->addWidget(scrollArea, 1);  // Stretch factor 1 to fill available space

  // === Footer buttons ===
  auto* footerLayout = new QHBoxLayout();
  footerLayout->setSpacing(12);

  resetButton_ = new QPushButton("Reset to Defaults", this);
  resetButton_->setCursor(Qt::PointingHandCursor);
  resetButton_->setStyleSheet(R"(
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
  connect(resetButton_, &QPushButton::clicked, this, &SettingsWidget::loadSettings);
  footerLayout->addWidget(resetButton_);

  footerLayout->addStretch();

  saveButton_ = new QPushButton("Save Changes", this);
  saveButton_->setCursor(Qt::PointingHandCursor);
  saveButton_->setStyleSheet(R"(
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 12px;
      padding: 14px 28px;
      color: white;
      font-size: 15px;
      font-weight: 600;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
  )");
  connect(saveButton_, &QPushButton::clicked, this, &SettingsWidget::saveSettings);
  footerLayout->addWidget(saveButton_);

  mainLayout->addLayout(footerLayout);
}

void SettingsWidget::createServerSection(QWidget* parent) {
  auto* group = new QGroupBox("Server Configuration", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  // Server Address
  auto* serverLabel = new QLabel("Server Address", group);
  serverLabel->setProperty("textStyle", "secondary");
  layout->addWidget(serverLabel);

  serverAddressEdit_ = new QLineEdit(group);
  serverAddressEdit_->setPlaceholderText("vpn.example.com or 192.168.1.1");
  connect(serverAddressEdit_, &QLineEdit::textChanged, this, &SettingsWidget::onServerAddressChanged);
  layout->addWidget(serverAddressEdit_);

  serverValidationLabel_ = new QLabel(group);
  serverValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  serverValidationLabel_->hide();
  layout->addWidget(serverValidationLabel_);

  // Port
  auto* portRow = new QHBoxLayout();
  auto* portLabel = new QLabel("Port", group);
  portLabel->setProperty("textStyle", "secondary");
  portRow->addWidget(portLabel);
  portRow->addStretch();

  portSpinBox_ = new QSpinBox(group);
  portSpinBox_->setRange(1, 65535);
  portSpinBox_->setValue(4433);
  portSpinBox_->setFixedWidth(100);
  connect(portSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onPortChanged);
  portRow->addWidget(portSpinBox_);

  layout->addLayout(portRow);

  parent->layout()->addWidget(group);
}

void SettingsWidget::createCryptoSection(QWidget* parent) {
  auto* group = new QGroupBox("Cryptographic Settings", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  // Pre-shared Key File
  auto* keyFileLabel = new QLabel("Pre-shared Key File (client.key)", group);
  keyFileLabel->setProperty("textStyle", "secondary");
  layout->addWidget(keyFileLabel);

  auto* keyFileRow = new QHBoxLayout();
  keyFileEdit_ = new QLineEdit(group);
  keyFileEdit_->setPlaceholderText("Path to client.key file");
  keyFileEdit_->setReadOnly(false);
  connect(keyFileEdit_, &QLineEdit::textChanged, [this]() {
    validateSettings();
    hasUnsavedChanges_ = true;
  });
  keyFileRow->addWidget(keyFileEdit_, 1);

  browseKeyFileButton_ = new QPushButton("\U0001F4C2", group);  // Folder icon
  browseKeyFileButton_->setFixedSize(40, 40);
  browseKeyFileButton_->setCursor(Qt::PointingHandCursor);
  browseKeyFileButton_->setToolTip("Browse for key file");
  browseKeyFileButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      font-size: 16px;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.2);
    }
  )");
  connect(browseKeyFileButton_, &QPushButton::clicked, this, &SettingsWidget::onBrowseKeyFile);
  keyFileRow->addWidget(browseKeyFileButton_);
  layout->addLayout(keyFileRow);

  keyFileValidationLabel_ = new QLabel(group);
  keyFileValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  keyFileValidationLabel_->hide();
  layout->addWidget(keyFileValidationLabel_);

  // Obfuscation Seed File
  auto* obfuscationSeedLabel = new QLabel("Obfuscation Seed File (obfuscation.seed)", group);
  obfuscationSeedLabel->setProperty("textStyle", "secondary");
  layout->addWidget(obfuscationSeedLabel);

  auto* obfuscationRow = new QHBoxLayout();
  obfuscationSeedEdit_ = new QLineEdit(group);
  obfuscationSeedEdit_->setPlaceholderText("Path to obfuscation.seed file (optional)");
  connect(obfuscationSeedEdit_, &QLineEdit::textChanged, [this]() {
    validateSettings();
    hasUnsavedChanges_ = true;
  });
  obfuscationRow->addWidget(obfuscationSeedEdit_, 1);

  browseObfuscationSeedButton_ = new QPushButton("\U0001F4C2", group);  // Folder icon
  browseObfuscationSeedButton_->setFixedSize(40, 40);
  browseObfuscationSeedButton_->setCursor(Qt::PointingHandCursor);
  browseObfuscationSeedButton_->setToolTip("Browse for obfuscation seed file");
  browseObfuscationSeedButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      font-size: 16px;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.2);
    }
  )");
  connect(browseObfuscationSeedButton_, &QPushButton::clicked, this, &SettingsWidget::onBrowseObfuscationSeed);
  obfuscationRow->addWidget(browseObfuscationSeedButton_);
  layout->addLayout(obfuscationRow);

  obfuscationSeedValidationLabel_ = new QLabel(group);
  obfuscationSeedValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  obfuscationSeedValidationLabel_->hide();
  layout->addWidget(obfuscationSeedValidationLabel_);

  // Info text
  auto* infoLabel = new QLabel(
      "The pre-shared key is required for secure handshake authentication.\n"
      "The obfuscation seed enables traffic morphing to evade DPI detection.",
      group);
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                   "background: rgba(88, 166, 255, 0.08); "
                                   "border: 1px solid rgba(88, 166, 255, 0.2); "
                                   "border-radius: 10px;")
                               .arg(colors::dark::kAccentPrimary));
  layout->addWidget(infoLabel);

  parent->layout()->addWidget(group);
}

void SettingsWidget::createRoutingSection(QWidget* parent) {
  auto* group = new QGroupBox("Routing", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  routeAllTrafficCheck_ = new QCheckBox("Route all traffic through VPN", group);
  routeAllTrafficCheck_->setToolTip("Send all internet traffic through the VPN tunnel");
  layout->addWidget(routeAllTrafficCheck_);

  splitTunnelCheck_ = new QCheckBox("Split tunnel mode", group);
  splitTunnelCheck_->setToolTip("Only route specific networks through VPN");
  layout->addWidget(splitTunnelCheck_);

  // Custom routes (only visible when split tunnel is enabled)
  auto* customRoutesLabel = new QLabel("Custom Routes (CIDR notation)", group);
  customRoutesLabel->setProperty("textStyle", "secondary");
  layout->addWidget(customRoutesLabel);

  customRoutesEdit_ = new QLineEdit(group);
  customRoutesEdit_->setPlaceholderText("10.0.0.0/8, 192.168.0.0/16");
  customRoutesEdit_->setEnabled(false);
  layout->addWidget(customRoutesEdit_);

  // Connect split tunnel checkbox to enable/disable custom routes
  connect(splitTunnelCheck_, &QCheckBox::toggled, customRoutesEdit_, &QLineEdit::setEnabled);
  connect(routeAllTrafficCheck_, &QCheckBox::toggled, [this](bool checked) {
    if (checked) {
      splitTunnelCheck_->setChecked(false);
    }
  });
  connect(splitTunnelCheck_, &QCheckBox::toggled, [this](bool checked) {
    if (checked) {
      routeAllTrafficCheck_->setChecked(false);
    }
  });

  parent->layout()->addWidget(group);
}

void SettingsWidget::createConnectionSection(QWidget* parent) {
  auto* group = new QGroupBox("Connection", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  autoReconnectCheck_ = new QCheckBox("Auto-reconnect on disconnect", group);
  autoReconnectCheck_->setToolTip("Automatically try to reconnect when connection is lost");
  layout->addWidget(autoReconnectCheck_);

  // Reconnect interval
  auto* intervalRow = new QHBoxLayout();
  auto* intervalLabel = new QLabel("Reconnect Interval", group);
  intervalLabel->setProperty("textStyle", "secondary");
  intervalRow->addWidget(intervalLabel);
  intervalRow->addStretch();

  reconnectIntervalSpinBox_ = new QSpinBox(group);
  reconnectIntervalSpinBox_->setRange(1, 60);
  reconnectIntervalSpinBox_->setValue(5);
  reconnectIntervalSpinBox_->setSuffix(" sec");
  reconnectIntervalSpinBox_->setFixedWidth(100);
  intervalRow->addWidget(reconnectIntervalSpinBox_);

  layout->addLayout(intervalRow);

  // Max reconnect attempts
  auto* attemptsRow = new QHBoxLayout();
  auto* attemptsLabel = new QLabel("Max Reconnect Attempts", group);
  attemptsLabel->setProperty("textStyle", "secondary");
  attemptsRow->addWidget(attemptsLabel);
  attemptsRow->addStretch();

  maxReconnectAttemptsSpinBox_ = new QSpinBox(group);
  maxReconnectAttemptsSpinBox_->setRange(0, 100);
  maxReconnectAttemptsSpinBox_->setValue(5);
  maxReconnectAttemptsSpinBox_->setSpecialValueText("Unlimited");
  maxReconnectAttemptsSpinBox_->setFixedWidth(100);
  attemptsRow->addWidget(maxReconnectAttemptsSpinBox_);

  layout->addLayout(attemptsRow);

  parent->layout()->addWidget(group);
}

void SettingsWidget::createDpiBypassSection(QWidget* parent) {
  auto* group = new QGroupBox("DPI Bypass Mode", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  auto* modeLabel = new QLabel("Select traffic obfuscation mode:", group);
  modeLabel->setProperty("textStyle", "secondary");
  layout->addWidget(modeLabel);

  dpiModeCombo_ = new QComboBox(group);
  dpiModeCombo_->addItem("IoT Mimic", "iot");
  dpiModeCombo_->addItem("QUIC-Like", "quic");
  dpiModeCombo_->addItem("Random-Noise Stealth", "random");
  dpiModeCombo_->addItem("Trickle Mode", "trickle");
  connect(dpiModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SettingsWidget::onDpiModeChanged);
  layout->addWidget(dpiModeCombo_);

  dpiDescLabel_ = new QLabel(group);
  dpiDescLabel_->setWordWrap(true);
  dpiDescLabel_->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                       "background: rgba(88, 166, 255, 0.08); "
                                       "border: 1px solid rgba(88, 166, 255, 0.2); "
                                       "border-radius: 10px;")
                                   .arg(colors::dark::kAccentPrimary));
  layout->addWidget(dpiDescLabel_);

  // Set initial description
  onDpiModeChanged(0);

  parent->layout()->addWidget(group);
}

void SettingsWidget::createTunInterfaceSection(QWidget* parent) {
  auto* group = new QGroupBox("TUN Interface", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  // Device Name
  auto* deviceNameLabel = new QLabel("Device Name", group);
  deviceNameLabel->setProperty("textStyle", "secondary");
  layout->addWidget(deviceNameLabel);

  tunDeviceNameEdit_ = new QLineEdit(group);
  tunDeviceNameEdit_->setPlaceholderText("veil0");
  tunDeviceNameEdit_->setToolTip("Name of the virtual network interface (e.g., veil0, tun0)");
  connect(tunDeviceNameEdit_, &QLineEdit::textChanged, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(tunDeviceNameEdit_);

  // IP Address
  auto* ipLabel = new QLabel("IP Address", group);
  ipLabel->setProperty("textStyle", "secondary");
  layout->addWidget(ipLabel);

  tunIpAddressEdit_ = new QLineEdit(group);
  tunIpAddressEdit_->setPlaceholderText("10.8.0.2");
  tunIpAddressEdit_->setToolTip("IP address assigned to the TUN interface");
  connect(tunIpAddressEdit_, &QLineEdit::textChanged, [this]() {
    validateSettings();
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(tunIpAddressEdit_);

  tunIpValidationLabel_ = new QLabel(group);
  tunIpValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  tunIpValidationLabel_->hide();
  layout->addWidget(tunIpValidationLabel_);

  // Netmask
  auto* netmaskLabel = new QLabel("Netmask", group);
  netmaskLabel->setProperty("textStyle", "secondary");
  layout->addWidget(netmaskLabel);

  tunNetmaskEdit_ = new QLineEdit(group);
  tunNetmaskEdit_->setPlaceholderText("255.255.255.0");
  tunNetmaskEdit_->setToolTip("Subnet mask for the TUN interface");
  connect(tunNetmaskEdit_, &QLineEdit::textChanged, [this]() {
    validateSettings();
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(tunNetmaskEdit_);

  tunNetmaskValidationLabel_ = new QLabel(group);
  tunNetmaskValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  tunNetmaskValidationLabel_->hide();
  layout->addWidget(tunNetmaskValidationLabel_);

  // MTU
  auto* mtuRow = new QHBoxLayout();
  auto* mtuLabel = new QLabel("MTU", group);
  mtuLabel->setProperty("textStyle", "secondary");
  mtuLabel->setToolTip("Maximum Transmission Unit (576-65535)");
  mtuRow->addWidget(mtuLabel);
  mtuRow->addStretch();

  tunMtuSpinBox_ = new QSpinBox(group);
  tunMtuSpinBox_->setRange(576, 65535);
  tunMtuSpinBox_->setValue(1400);
  tunMtuSpinBox_->setSuffix(" bytes");
  tunMtuSpinBox_->setFixedWidth(130);
  tunMtuSpinBox_->setToolTip("Recommended: 1400 for most networks");
  connect(tunMtuSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), [this]() {
    hasUnsavedChanges_ = true;
  });
  mtuRow->addWidget(tunMtuSpinBox_);
  layout->addLayout(mtuRow);

  // Info text
  auto* infoLabel = new QLabel(
      "The TUN interface creates a virtual network device for VPN traffic.\n"
      "Default values work for most configurations.",
      group);
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                   "background: rgba(88, 166, 255, 0.08); "
                                   "border: 1px solid rgba(88, 166, 255, 0.2); "
                                   "border-radius: 10px;")
                               .arg(colors::dark::kAccentPrimary));
  layout->addWidget(infoLabel);

  parent->layout()->addWidget(group);
}

void SettingsWidget::createAdvancedSection(QWidget* parent) {
  auto* group = new QGroupBox("Advanced", parent);
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  obfuscationCheck_ = new QCheckBox("Enable obfuscation", group);
  obfuscationCheck_->setToolTip("Enable traffic morphing with padding and timing jitter");
  layout->addWidget(obfuscationCheck_);

  verboseLoggingCheck_ = new QCheckBox("Verbose logging", group);
  verboseLoggingCheck_->setToolTip("Log detailed handshake and retransmission information");
  layout->addWidget(verboseLoggingCheck_);

  developerModeCheck_ = new QCheckBox("Developer mode", group);
  developerModeCheck_->setToolTip("Enable diagnostics screen with protocol metrics");
  layout->addWidget(developerModeCheck_);

  parent->layout()->addWidget(group);
}

void SettingsWidget::onServerAddressChanged() {
  validateSettings();
  hasUnsavedChanges_ = true;
}

void SettingsWidget::onPortChanged() {
  hasUnsavedChanges_ = true;
}

void SettingsWidget::onDpiModeChanged(int index) {
  static const char* descriptions[] = {
      "Simulates IoT sensor traffic with periodic heartbeats. "
      "Good balance of stealth and performance. Recommended for most users.",

      "Mimics modern HTTP/3 (QUIC) traffic patterns. "
      "Best for high-throughput scenarios where QUIC traffic is common.",

      "Maximum unpredictability with randomized packet sizes and timing. "
      "Use in extreme censorship environments. Higher overhead.",

      "Low-and-slow traffic with minimal bandwidth (10-50 kbit/s). "
      "Maximum stealth but not suitable for normal browsing."
  };

  if (index >= 0 && index < 4) {
    dpiDescLabel_->setText(descriptions[index]);
  }
  hasUnsavedChanges_ = true;
}

void SettingsWidget::onBrowseKeyFile() {
  QString dir = keyFileEdit_->text().isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
      : QFileInfo(keyFileEdit_->text()).absolutePath();

  QString fileName = QFileDialog::getOpenFileName(
      this,
      tr("Select Pre-shared Key File"),
      dir,
      tr("Key Files (*.key *.pem *.bin);;All Files (*)")
  );

  if (!fileName.isEmpty()) {
    keyFileEdit_->setText(fileName);
    hasUnsavedChanges_ = true;
    validateSettings();
  }
}

void SettingsWidget::onBrowseObfuscationSeed() {
  QString dir = obfuscationSeedEdit_->text().isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
      : QFileInfo(obfuscationSeedEdit_->text()).absolutePath();

  QString fileName = QFileDialog::getOpenFileName(
      this,
      tr("Select Obfuscation Seed File"),
      dir,
      tr("Seed Files (*.seed *.bin);;All Files (*)")
  );

  if (!fileName.isEmpty()) {
    obfuscationSeedEdit_->setText(fileName);
    hasUnsavedChanges_ = true;
    validateSettings();
  }
}

void SettingsWidget::validateSettings() {
  bool allValid = true;

  // Validate server address
  QString address = serverAddressEdit_->text().trimmed();
  bool addressValid = address.isEmpty() || isValidHostname(address) || isValidIpAddress(address);

  if (!addressValid && !address.isEmpty()) {
    serverValidationLabel_->setText("Invalid server address format");
    serverValidationLabel_->show();
    serverAddressEdit_->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
    allValid = false;
  } else {
    serverValidationLabel_->hide();
    serverAddressEdit_->setStyleSheet("");
  }

  // Validate key file
  QString keyPath = keyFileEdit_->text().trimmed();
  if (!keyPath.isEmpty() && !isValidFilePath(keyPath)) {
    keyFileValidationLabel_->setText("Key file not found");
    keyFileValidationLabel_->show();
    keyFileEdit_->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
    allValid = false;
  } else {
    keyFileValidationLabel_->hide();
    keyFileEdit_->setStyleSheet("");
  }

  // Validate obfuscation seed file (optional, but check if path is set)
  QString seedPath = obfuscationSeedEdit_->text().trimmed();
  if (!seedPath.isEmpty() && !isValidFilePath(seedPath)) {
    obfuscationSeedValidationLabel_->setText("Seed file not found");
    obfuscationSeedValidationLabel_->show();
    obfuscationSeedEdit_->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
    allValid = false;
  } else {
    obfuscationSeedValidationLabel_->hide();
    obfuscationSeedEdit_->setStyleSheet("");
  }

  // Validate TUN IP address
  QString tunIp = tunIpAddressEdit_->text().trimmed();
  if (!tunIp.isEmpty() && !isValidIpAddress(tunIp)) {
    tunIpValidationLabel_->setText("Invalid IP address format");
    tunIpValidationLabel_->show();
    tunIpAddressEdit_->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
    allValid = false;
  } else {
    tunIpValidationLabel_->hide();
    tunIpAddressEdit_->setStyleSheet("");
  }

  // Validate TUN netmask
  QString tunNetmask = tunNetmaskEdit_->text().trimmed();
  if (!tunNetmask.isEmpty() && !isValidIpAddress(tunNetmask)) {
    tunNetmaskValidationLabel_->setText("Invalid netmask format");
    tunNetmaskValidationLabel_->show();
    tunNetmaskEdit_->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
    allValid = false;
  } else {
    tunNetmaskValidationLabel_->hide();
    tunNetmaskEdit_->setStyleSheet("");
  }

  saveButton_->setEnabled(allValid);
}

bool SettingsWidget::isValidFilePath(const QString& path) const {
  if (path.isEmpty()) return true;
  QFileInfo fileInfo(path);
  return fileInfo.exists() && fileInfo.isFile() && fileInfo.isReadable();
}

bool SettingsWidget::isValidHostname(const QString& hostname) const {
  // Simple hostname validation
  static QRegularExpression hostnameRegex(
      "^([a-zA-Z0-9]([a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])?\\.)*[a-zA-Z]{2,}$");
  return hostnameRegex.match(hostname).hasMatch();
}

bool SettingsWidget::isValidIpAddress(const QString& ip) const {
  // IPv4 validation
  static QRegularExpression ipv4Regex(
      "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
  return ipv4Regex.match(ip).hasMatch();
}

void SettingsWidget::loadSettings() {
  // Load settings from QSettings
  QSettings settings("VEIL", "VPN Client");

  // Server Configuration
  serverAddressEdit_->setText(settings.value("server/address", "vpn.example.com").toString());
  portSpinBox_->setValue(settings.value("server/port", 4433).toInt());

  // Crypto Configuration
  keyFileEdit_->setText(settings.value("crypto/keyFile", "").toString());
  obfuscationSeedEdit_->setText(settings.value("crypto/obfuscationSeedFile", "").toString());

  // TUN Interface Configuration
  tunDeviceNameEdit_->setText(settings.value("tun/deviceName", "veil0").toString());
  tunIpAddressEdit_->setText(settings.value("tun/ipAddress", "10.8.0.2").toString());
  tunNetmaskEdit_->setText(settings.value("tun/netmask", "255.255.255.0").toString());
  tunMtuSpinBox_->setValue(settings.value("tun/mtu", 1400).toInt());

  // Routing
  routeAllTrafficCheck_->setChecked(settings.value("routing/routeAllTraffic", true).toBool());
  splitTunnelCheck_->setChecked(settings.value("routing/splitTunnel", false).toBool());
  customRoutesEdit_->setText(settings.value("routing/customRoutes", "").toString());

  // Connection
  autoReconnectCheck_->setChecked(settings.value("connection/autoReconnect", true).toBool());
  reconnectIntervalSpinBox_->setValue(settings.value("connection/reconnectInterval", 5).toInt());
  maxReconnectAttemptsSpinBox_->setValue(settings.value("connection/maxReconnectAttempts", 5).toInt());

  // DPI Bypass
  dpiModeCombo_->setCurrentIndex(settings.value("dpi/mode", 0).toInt());

  // Advanced
  obfuscationCheck_->setChecked(settings.value("advanced/obfuscation", true).toBool());
  verboseLoggingCheck_->setChecked(settings.value("advanced/verboseLogging", false).toBool());
  developerModeCheck_->setChecked(settings.value("advanced/developerMode", false).toBool());

  hasUnsavedChanges_ = false;
  validateSettings();
}

void SettingsWidget::saveSettings() {
  // Validate before saving
  validateSettings();

  if (!saveButton_->isEnabled()) {
    QMessageBox::warning(this, "Invalid Settings",
                         "Please fix the validation errors before saving.");
    return;
  }

  // Show loading state
  saveButton_->setEnabled(false);
  saveButton_->setText("Saving...");
  saveButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: %1;
      color: %2;
    }
  )").arg(colors::dark::kBackgroundSecondary, colors::dark::kTextSecondary));

  // Process events to update UI immediately
  QApplication::processEvents();

  // Save settings to QSettings
  QSettings settings("VEIL", "VPN Client");

  // Server Configuration
  settings.setValue("server/address", serverAddressEdit_->text().trimmed());
  settings.setValue("server/port", portSpinBox_->value());

  // Crypto Configuration
  settings.setValue("crypto/keyFile", keyFileEdit_->text().trimmed());
  settings.setValue("crypto/obfuscationSeedFile", obfuscationSeedEdit_->text().trimmed());

  // TUN Interface Configuration
  settings.setValue("tun/deviceName", tunDeviceNameEdit_->text().trimmed());
  settings.setValue("tun/ipAddress", tunIpAddressEdit_->text().trimmed());
  settings.setValue("tun/netmask", tunNetmaskEdit_->text().trimmed());
  settings.setValue("tun/mtu", tunMtuSpinBox_->value());

  // Routing
  settings.setValue("routing/routeAllTraffic", routeAllTrafficCheck_->isChecked());
  settings.setValue("routing/splitTunnel", splitTunnelCheck_->isChecked());
  settings.setValue("routing/customRoutes", customRoutesEdit_->text().trimmed());

  // Connection
  settings.setValue("connection/autoReconnect", autoReconnectCheck_->isChecked());
  settings.setValue("connection/reconnectInterval", reconnectIntervalSpinBox_->value());
  settings.setValue("connection/maxReconnectAttempts", maxReconnectAttemptsSpinBox_->value());

  // DPI Bypass
  settings.setValue("dpi/mode", dpiModeCombo_->currentIndex());

  // Advanced
  settings.setValue("advanced/obfuscation", obfuscationCheck_->isChecked());
  settings.setValue("advanced/verboseLogging", verboseLoggingCheck_->isChecked());
  settings.setValue("advanced/developerMode", developerModeCheck_->isChecked());

  settings.sync();
  hasUnsavedChanges_ = false;

  // Show success confirmation
  saveButton_->setText("Saved!");
  saveButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: %1;
    }
  )").arg(colors::dark::kAccentSuccess));

  // Reset button after 2 seconds
  QTimer::singleShot(2000, this, [this]() {
    saveButton_->setText("Save Changes");
    saveButton_->setStyleSheet("");
    saveButton_->setEnabled(true);
  });

  emit settingsSaved();
}

QString SettingsWidget::serverAddress() const {
  return serverAddressEdit_->text().trimmed();
}

uint16_t SettingsWidget::serverPort() const {
  return static_cast<uint16_t>(portSpinBox_->value());
}

QString SettingsWidget::keyFilePath() const {
  return keyFileEdit_->text().trimmed();
}

QString SettingsWidget::obfuscationSeedPath() const {
  return obfuscationSeedEdit_->text().trimmed();
}

}  // namespace veil::gui
