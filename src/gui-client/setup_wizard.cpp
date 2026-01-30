#include "setup_wizard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QApplication>
#include <QParallelAnimationGroup>
#include <QDebug>
#include <QTcpSocket>
#include <QTimer>

#include "common/gui/theme.h"
#include "common/version.h"

namespace veil::gui {

namespace {
constexpr const char* kFirstRunKey = "app/firstRunCompleted";
constexpr const char* kSettingsOrg = "VEIL";
constexpr const char* kSettingsApp = "VPN Client";

/// Step titles for the wizard
const QStringList kStepTitles = {
    "Welcome", "Server", "Key File", "Features", "Finish"};
}  // namespace

// ===================== Static Methods =====================

bool SetupWizard::isFirstRun() {
  QSettings settings(kSettingsOrg, kSettingsApp);
  return !settings.value(kFirstRunKey, false).toBool();
}

void SetupWizard::markFirstRunComplete() {
  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.setValue(kFirstRunKey, true);
  settings.sync();
  qDebug() << "[SetupWizard] First run marked as complete";
}

void SetupWizard::resetFirstRun() {
  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.setValue(kFirstRunKey, false);
  settings.sync();
  qDebug() << "[SetupWizard] First run flag reset";
}

// ===================== Constructor =====================

SetupWizard::SetupWizard(QWidget* parent) : QWidget(parent) {
  qDebug() << "[SetupWizard] Initializing setup wizard...";
  setupUi();
  updateNavigationButtons();
  qDebug() << "[SetupWizard] Setup wizard initialized";
}

// ===================== UI Setup =====================

void SetupWizard::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // --- Step indicator bar ---
  stepIndicatorContainer_ = new QWidget(this);
  stepIndicatorContainer_->setFixedHeight(60);
  stepIndicatorContainer_->setStyleSheet(
      "background-color: rgba(255, 255, 255, 0.02);"
      "border-bottom: 1px solid rgba(255, 255, 255, 0.06);");

  auto* stepLayout = new QHBoxLayout(stepIndicatorContainer_);
  stepLayout->setContentsMargins(32, 0, 32, 0);
  stepLayout->setSpacing(8);
  stepLayout->addStretch();

  for (int i = 0; i < kPageCount; ++i) {
    // Dot indicator
    auto* dot = new QWidget(stepIndicatorContainer_);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(
        i == 0 ? "background-color: #58a6ff; border-radius: 5px;"
               : "background-color: #30363d; border-radius: 5px;");
    stepDots_.append(dot);

    // Step label
    auto* label = new QLabel(kStepTitles[i], stepIndicatorContainer_);
    label->setStyleSheet(
        i == 0 ? "color: #f0f6fc; font-size: 12px; font-weight: 600;"
               : "color: #6e7681; font-size: 12px;");
    stepLabels_.append(label);

    auto* stepItem = new QHBoxLayout();
    stepItem->setSpacing(6);
    stepItem->addWidget(dot);
    stepItem->addWidget(label);
    stepLayout->addLayout(stepItem);

    // Connector line between steps
    if (i < kPageCount - 1) {
      auto* connector = new QWidget(stepIndicatorContainer_);
      connector->setFixedSize(24, 1);
      connector->setStyleSheet("background-color: #30363d;");
      stepLayout->addWidget(connector);
    }
  }
  stepLayout->addStretch();
  mainLayout->addWidget(stepIndicatorContainer_);

  // --- Page stack ---
  pageStack_ = new QStackedWidget(this);
  pageStack_->addWidget(createWelcomePage());
  pageStack_->addWidget(createServerPage());
  pageStack_->addWidget(createKeyFilePage());
  pageStack_->addWidget(createFeaturesPage());
  pageStack_->addWidget(createFinishPage());
  mainLayout->addWidget(pageStack_, 1);

  // --- Navigation button bar ---
  auto* navBar = new QWidget(this);
  navBar->setFixedHeight(72);
  navBar->setStyleSheet(
      "background-color: rgba(255, 255, 255, 0.02);"
      "border-top: 1px solid rgba(255, 255, 255, 0.06);");

  auto* navLayout = new QHBoxLayout(navBar);
  navLayout->setContentsMargins(32, 0, 32, 0);

  skipButton_ = new QPushButton(tr("Skip Setup"), navBar);
  skipButton_->setProperty("buttonStyle", "ghost");
  skipButton_->setFixedHeight(40);
  skipButton_->setCursor(Qt::PointingHandCursor);
  connect(skipButton_, &QPushButton::clicked, this, &SetupWizard::onSkipClicked);

  backButton_ = new QPushButton(tr("Back"), navBar);
  backButton_->setProperty("buttonStyle", "secondary");
  backButton_->setFixedHeight(40);
  backButton_->setFixedWidth(100);
  backButton_->setCursor(Qt::PointingHandCursor);
  connect(backButton_, &QPushButton::clicked, this, &SetupWizard::onBackClicked);

  nextButton_ = new QPushButton(tr("Next"), navBar);
  nextButton_->setFixedHeight(40);
  nextButton_->setFixedWidth(120);
  nextButton_->setCursor(Qt::PointingHandCursor);
  connect(nextButton_, &QPushButton::clicked, this, &SetupWizard::onNextClicked);

  navLayout->addWidget(skipButton_);
  navLayout->addStretch();
  navLayout->addWidget(backButton_);
  navLayout->addSpacing(12);
  navLayout->addWidget(nextButton_);

  mainLayout->addWidget(navBar);
}

// ===================== Page Creation =====================

QWidget* SetupWizard::createWelcomePage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(20);
  layout->setContentsMargins(48, 48, 48, 32);

  layout->addStretch();

  // Logo
  auto* logoWidget = new QWidget(page);
  logoWidget->setFixedSize(80, 80);
  logoWidget->setStyleSheet(
      "background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
      "  stop:0 #238636, stop:1 #3fb950);"
      "border-radius: 20px;");
  layout->addWidget(logoWidget, 0, Qt::AlignCenter);

  // Title
  auto* titleLabel = new QLabel(tr("Welcome to VEIL VPN"), page);
  titleLabel->setStyleSheet(
      "font-size: 28px; font-weight: 700; color: #f0f6fc; letter-spacing: 1px;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  // Version
  auto* versionLabel =
      new QLabel(QString("Version %1").arg(veil::kVersionString), page);
  versionLabel->setStyleSheet(
      "color: #8b949e; font-size: 14px; padding: 4px 16px;"
      "background: rgba(255, 255, 255, 0.04); border-radius: 12px;");
  versionLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(versionLabel, 0, Qt::AlignCenter);

  layout->addSpacing(16);

  // Description
  auto* descLabel = new QLabel(
      tr("This wizard will guide you through the initial setup.\n\n"
         "You will configure:\n"
         "  \u2022  VPN server address and port\n"
         "  \u2022  Pre-shared key file\n"
         "  \u2022  Optional features (DPI bypass, routing)\n\n"
         "You can also import an existing configuration file."),
      page);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet(
      "color: #8b949e; font-size: 15px; line-height: 1.6;");
  descLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(descLabel);

  layout->addSpacing(16);

  // Import config button
  auto* importButton = new QPushButton(tr("Import Configuration File..."), page);
  importButton->setProperty("buttonStyle", "secondary");
  importButton->setFixedHeight(44);
  importButton->setFixedWidth(280);
  importButton->setCursor(Qt::PointingHandCursor);
  connect(importButton, &QPushButton::clicked, this, &SetupWizard::onImportConfig);
  layout->addWidget(importButton, 0, Qt::AlignCenter);

  layout->addStretch();

  return page;
}

QWidget* SetupWizard::createServerPage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(16);
  layout->setContentsMargins(48, 32, 48, 32);

  layout->addStretch();

  // Title
  auto* titleLabel = new QLabel(tr("Server Configuration"), page);
  titleLabel->setStyleSheet(
      "font-size: 22px; font-weight: 700; color: #f0f6fc;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* subtitleLabel = new QLabel(
      tr("Enter the address and port of your VEIL VPN server."), page);
  subtitleLabel->setStyleSheet("color: #8b949e; font-size: 14px;");
  subtitleLabel->setAlignment(Qt::AlignCenter);
  subtitleLabel->setWordWrap(true);
  layout->addWidget(subtitleLabel);

  layout->addSpacing(24);

  // Server address
  auto* serverGroup = new QGroupBox(tr("SERVER"), page);
  auto* serverLayout = new QVBoxLayout(serverGroup);
  serverLayout->setSpacing(12);

  auto* addressLabel = new QLabel(tr("Server Address"), serverGroup);
  addressLabel->setStyleSheet("color: #8b949e; font-size: 13px;");
  serverLayout->addWidget(addressLabel);

  serverAddressEdit_ = new QLineEdit(serverGroup);
  serverAddressEdit_->setPlaceholderText("vpn.example.com or 192.168.1.1");
  serverAddressEdit_->setToolTip(
      tr("Enter the hostname or IP address of the VPN server"));
  serverLayout->addWidget(serverAddressEdit_);

  auto* portLabel = new QLabel(tr("Port"), serverGroup);
  portLabel->setStyleSheet("color: #8b949e; font-size: 13px;");
  serverLayout->addWidget(portLabel);

  serverPortSpinBox_ = new QSpinBox(serverGroup);
  serverPortSpinBox_->setRange(1, 65535);
  serverPortSpinBox_->setValue(4433);
  serverPortSpinBox_->setToolTip(
      tr("The port number the VPN server listens on (default: 4433)"));
  serverLayout->addWidget(serverPortSpinBox_);

  serverValidationLabel_ = new QLabel(serverGroup);
  serverValidationLabel_->setStyleSheet("color: #f85149; font-size: 12px;");
  serverValidationLabel_->setVisible(false);
  serverLayout->addWidget(serverValidationLabel_);

  layout->addWidget(serverGroup);

  layout->addStretch();

  return page;
}

QWidget* SetupWizard::createKeyFilePage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(16);
  layout->setContentsMargins(48, 32, 48, 32);

  layout->addStretch();

  // Title
  auto* titleLabel = new QLabel(tr("Key File Setup"), page);
  titleLabel->setStyleSheet(
      "font-size: 22px; font-weight: 700; color: #f0f6fc;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* subtitleLabel = new QLabel(
      tr("A pre-shared key file is used to authenticate with the server.\n"
         "The key file is provided by your VPN server administrator."),
      page);
  subtitleLabel->setStyleSheet("color: #8b949e; font-size: 14px;");
  subtitleLabel->setAlignment(Qt::AlignCenter);
  subtitleLabel->setWordWrap(true);
  layout->addWidget(subtitleLabel);

  layout->addSpacing(24);

  // Key file group
  auto* keyGroup = new QGroupBox(tr("PRE-SHARED KEY"), page);
  auto* keyLayout = new QVBoxLayout(keyGroup);
  keyLayout->setSpacing(12);

  auto* pathLabel = new QLabel(tr("Key File Path"), keyGroup);
  pathLabel->setStyleSheet("color: #8b949e; font-size: 13px;");
  keyLayout->addWidget(pathLabel);

  auto* fileRow = new QHBoxLayout();
  keyFileEdit_ = new QLineEdit(keyGroup);
  keyFileEdit_->setPlaceholderText(tr("Select key file provided by server..."));
  keyFileEdit_->setReadOnly(true);
  fileRow->addWidget(keyFileEdit_);

  browseKeyFileButton_ = new QPushButton(tr("Browse"), keyGroup);
  browseKeyFileButton_->setProperty("buttonStyle", "secondary");
  browseKeyFileButton_->setFixedWidth(90);
  browseKeyFileButton_->setFixedHeight(40);
  browseKeyFileButton_->setCursor(Qt::PointingHandCursor);
  connect(browseKeyFileButton_, &QPushButton::clicked,
          this, &SetupWizard::onBrowseKeyFile);
  fileRow->addWidget(browseKeyFileButton_);

  keyLayout->addLayout(fileRow);

  keyFileStatusLabel_ = new QLabel(keyGroup);
  keyFileStatusLabel_->setStyleSheet("font-size: 12px;");
  keyFileStatusLabel_->setVisible(false);
  keyLayout->addWidget(keyFileStatusLabel_);

  layout->addWidget(keyGroup);

  // Info label
  auto* infoLabel = new QLabel(
      tr("Note: The key and seed files are provided by your VPN server. "
         "You can configure them later in Settings if not available yet."),
      page);
  infoLabel->setStyleSheet(
      "color: #6e7681; font-size: 12px; padding: 8px 12px;"
      "background: rgba(255, 255, 255, 0.02); border-radius: 8px;");
  infoLabel->setWordWrap(true);
  layout->addWidget(infoLabel);

  layout->addStretch();

  return page;
}

QWidget* SetupWizard::createFeaturesPage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(16);
  layout->setContentsMargins(48, 32, 48, 32);

  layout->addStretch();

  // Title
  auto* titleLabel = new QLabel(tr("Features Configuration"), page);
  titleLabel->setStyleSheet(
      "font-size: 22px; font-weight: 700; color: #f0f6fc;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* subtitleLabel = new QLabel(
      tr("Configure optional features for your VPN connection."), page);
  subtitleLabel->setStyleSheet("color: #8b949e; font-size: 14px;");
  subtitleLabel->setAlignment(Qt::AlignCenter);
  subtitleLabel->setWordWrap(true);
  layout->addWidget(subtitleLabel);

  layout->addSpacing(24);

  // DPI bypass
  auto* dpiGroup = new QGroupBox(tr("DPI BYPASS"), page);
  auto* dpiLayout = new QVBoxLayout(dpiGroup);
  dpiLayout->setSpacing(12);

  obfuscationCheck_ = new QCheckBox(tr("Enable traffic obfuscation"), dpiGroup);
  obfuscationCheck_->setChecked(true);
  obfuscationCheck_->setToolTip(
      tr("Obfuscate VPN traffic to evade Deep Packet Inspection"));
  dpiLayout->addWidget(obfuscationCheck_);

  auto* dpiModeLabel = new QLabel(tr("DPI Bypass Mode"), dpiGroup);
  dpiModeLabel->setStyleSheet("color: #8b949e; font-size: 13px;");
  dpiLayout->addWidget(dpiModeLabel);

  dpiModeCombo_ = new QComboBox(dpiGroup);
  dpiModeCombo_->addItem(tr("IoT (Low bandwidth)"), 0);
  dpiModeCombo_->addItem(tr("QUIC (Medium bandwidth)"), 1);
  dpiModeCombo_->addItem(tr("Noise (High bandwidth)"), 2);
  dpiModeCombo_->addItem(tr("Trickle (Stealth)"), 3);
  dpiModeCombo_->setToolTip(
      tr("Choose how VPN traffic is disguised to avoid detection"));
  dpiLayout->addWidget(dpiModeCombo_);

  layout->addWidget(dpiGroup);

  // Routing
  auto* routingGroup = new QGroupBox(tr("ROUTING"), page);
  auto* routingLayout = new QVBoxLayout(routingGroup);
  routingLayout->setSpacing(12);

  routeAllTrafficCheck_ =
      new QCheckBox(tr("Route all traffic through VPN"), routingGroup);
  routeAllTrafficCheck_->setChecked(true);
  routeAllTrafficCheck_->setToolTip(
      tr("When enabled, all network traffic goes through the VPN tunnel"));
  routingLayout->addWidget(routeAllTrafficCheck_);

  autoReconnectCheck_ =
      new QCheckBox(tr("Auto-reconnect on disconnection"), routingGroup);
  autoReconnectCheck_->setChecked(true);
  autoReconnectCheck_->setToolTip(
      tr("Automatically reconnect if the VPN connection drops"));
  routingLayout->addWidget(autoReconnectCheck_);

  layout->addWidget(routingGroup);

  layout->addStretch();

  return page;
}

QWidget* SetupWizard::createFinishPage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(16);
  layout->setContentsMargins(48, 32, 48, 32);

  layout->addStretch();

  // Title
  auto* titleLabel = new QLabel(tr("Setup Complete"), page);
  titleLabel->setStyleSheet(
      "font-size: 22px; font-weight: 700; color: #f0f6fc;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* subtitleLabel = new QLabel(
      tr("Review your configuration and test the connection."), page);
  subtitleLabel->setStyleSheet("color: #8b949e; font-size: 14px;");
  subtitleLabel->setAlignment(Qt::AlignCenter);
  subtitleLabel->setWordWrap(true);
  layout->addWidget(subtitleLabel);

  layout->addSpacing(24);

  // Configuration summary
  auto* summaryGroup = new QGroupBox(tr("CONFIGURATION SUMMARY"), page);
  auto* summaryLayout = new QVBoxLayout(summaryGroup);

  configSummaryLabel_ = new QLabel(summaryGroup);
  configSummaryLabel_->setStyleSheet(
      "color: #8b949e; font-size: 13px; line-height: 1.6;");
  configSummaryLabel_->setWordWrap(true);
  summaryLayout->addWidget(configSummaryLabel_);

  layout->addWidget(summaryGroup);

  layout->addSpacing(16);

  // Test connection
  testConnectionButton_ = new QPushButton(tr("Test Connection"), page);
  testConnectionButton_->setFixedHeight(48);
  testConnectionButton_->setFixedWidth(220);
  testConnectionButton_->setCursor(Qt::PointingHandCursor);
  connect(testConnectionButton_, &QPushButton::clicked,
          this, &SetupWizard::onTestConnection);
  layout->addWidget(testConnectionButton_, 0, Qt::AlignCenter);

  testResultLabel_ = new QLabel(page);
  testResultLabel_->setStyleSheet("font-size: 13px;");
  testResultLabel_->setAlignment(Qt::AlignCenter);
  testResultLabel_->setVisible(false);
  layout->addWidget(testResultLabel_);

  // Info
  auto* infoLabel = new QLabel(
      tr("You can change any of these settings later from the Settings view."),
      page);
  infoLabel->setStyleSheet(
      "color: #6e7681; font-size: 12px; padding: 8px 12px;"
      "background: rgba(255, 255, 255, 0.02); border-radius: 8px;");
  infoLabel->setWordWrap(true);
  infoLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(infoLabel);

  layout->addStretch();

  return page;
}

// ===================== Navigation =====================

void SetupWizard::onNextClicked() {
  if (currentPageIndex_ == kPageCount - 1) {
    // Last page — finish
    onFinishClicked();
    return;
  }

  if (!validateCurrentPage()) {
    return;
  }

  navigateToPage(currentPageIndex_ + 1);
}

void SetupWizard::onBackClicked() {
  if (currentPageIndex_ > 0) {
    navigateToPage(currentPageIndex_ - 1);
  }
}

void SetupWizard::onSkipClicked() {
  qDebug() << "[SetupWizard] User chose to skip setup wizard";
  markFirstRunComplete();
  emit wizardSkipped();
}

void SetupWizard::onFinishClicked() {
  qDebug() << "[SetupWizard] User completed setup wizard";
  saveAllSettings();
  markFirstRunComplete();
  emit wizardCompleted();
}

void SetupWizard::navigateToPage(int index) {
  if (index < 0 || index >= kPageCount || index == currentPageIndex_) {
    return;
  }

  qDebug() << "[SetupWizard] Navigating from page" << currentPageIndex_
           << "to page" << index;

  // Update step indicators
  for (int i = 0; i < kPageCount; ++i) {
    if (i < index) {
      // Completed steps
      stepDots_[i]->setStyleSheet(
          "background-color: #3fb950; border-radius: 5px;");
      stepLabels_[i]->setStyleSheet(
          "color: #3fb950; font-size: 12px; font-weight: 600;");
    } else if (i == index) {
      // Current step
      stepDots_[i]->setStyleSheet(
          "background-color: #58a6ff; border-radius: 5px;");
      stepLabels_[i]->setStyleSheet(
          "color: #f0f6fc; font-size: 12px; font-weight: 600;");
    } else {
      // Future steps
      stepDots_[i]->setStyleSheet(
          "background-color: #30363d; border-radius: 5px;");
      stepLabels_[i]->setStyleSheet("color: #6e7681; font-size: 12px;");
    }
  }

  // Update finish page summary when navigating to the last page
  if (index == kPageCount - 1) {
    QString summary;
    summary += tr("Server: %1:%2\n")
                   .arg(serverAddressEdit_->text().isEmpty()
                            ? tr("(not set)")
                            : serverAddressEdit_->text())
                   .arg(serverPortSpinBox_->value());
    summary += tr("Key File: %1\n")
                   .arg(keyFileEdit_->text().isEmpty() ? tr("(not set)")
                                                       : keyFileEdit_->text());
    summary += tr("Obfuscation: %1\n")
                   .arg(obfuscationCheck_->isChecked() ? tr("Enabled")
                                                       : tr("Disabled"));
    summary += tr("DPI Mode: %1\n").arg(dpiModeCombo_->currentText());
    summary += tr("Route All Traffic: %1\n")
                   .arg(routeAllTrafficCheck_->isChecked() ? tr("Yes")
                                                           : tr("No"));
    summary += tr("Auto-Reconnect: %1")
                   .arg(autoReconnectCheck_->isChecked() ? tr("Yes")
                                                         : tr("No"));
    configSummaryLabel_->setText(summary);
  }

  currentPageIndex_ = index;
  pageStack_->setCurrentIndex(index);
  updateNavigationButtons();
}

void SetupWizard::updateNavigationButtons() {
  backButton_->setVisible(currentPageIndex_ > 0);

  if (currentPageIndex_ == kPageCount - 1) {
    nextButton_->setText(tr("Finish"));
  } else {
    nextButton_->setText(tr("Next"));
  }
}

bool SetupWizard::validateCurrentPage() {
  switch (currentPageIndex_) {
    case 1: {
      // Server page — address must not be empty
      QString address = serverAddressEdit_->text().trimmed();
      if (address.isEmpty()) {
        serverValidationLabel_->setText(
            tr("Please enter a server address."));
        serverValidationLabel_->setStyleSheet(
            "color: #f85149; font-size: 12px;");
        serverValidationLabel_->setVisible(true);
        return false;
      }
      serverValidationLabel_->setVisible(false);
      return true;
    }
    default:
      // Other pages have no required fields
      return true;
  }
}

// ===================== Settings =====================

void SetupWizard::saveAllSettings() {
  QSettings settings(kSettingsOrg, kSettingsApp);

  qDebug() << "[SetupWizard] Saving wizard settings...";

  // Server
  QString serverAddress = serverAddressEdit_->text().trimmed();
  if (!serverAddress.isEmpty()) {
    settings.setValue("server/address", serverAddress);
  }
  settings.setValue("server/port", serverPortSpinBox_->value());

  // Key file
  QString keyFile = keyFileEdit_->text().trimmed();
  if (!keyFile.isEmpty()) {
    settings.setValue("crypto/keyFile", keyFile);
  }

  // Features
  settings.setValue("advanced/obfuscation", obfuscationCheck_->isChecked());
  settings.setValue("dpi/mode", dpiModeCombo_->currentData().toInt());
  settings.setValue("routing/routeAllTraffic",
                    routeAllTrafficCheck_->isChecked());
  settings.setValue("connection/autoReconnect",
                    autoReconnectCheck_->isChecked());

  settings.sync();
  qDebug() << "[SetupWizard] Wizard settings saved successfully";
}

// ===================== Import Config =====================

void SetupWizard::onImportConfig() {
  QString filePath = QFileDialog::getOpenFileName(
      this, tr("Import VEIL Configuration"),
      QDir::homePath(),
      tr("VEIL Config Files (*.veil *.json);;All Files (*)"));

  if (filePath.isEmpty()) {
    return;
  }

  if (importConfigFromFile(filePath)) {
    QMessageBox::information(
        this, tr("Import Successful"),
        tr("Configuration imported successfully.\n"
           "Review the settings and click Finish to complete setup."));
    // Jump to finish page
    navigateToPage(kPageCount - 1);
  } else {
    QMessageBox::warning(
        this, tr("Import Failed"),
        tr("Failed to import configuration file.\n"
           "Please check the file format and try again."));
  }
}

bool SetupWizard::importConfigFromFile(const QString& filePath) {
  qDebug() << "[SetupWizard] Importing configuration from:" << filePath;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "[SetupWizard] Failed to open config file:" << filePath;
    return false;
  }

  QByteArray fileContents = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(fileContents, &parseError);
  if (doc.isNull() || !doc.isObject()) {
    qWarning() << "[SetupWizard] Failed to parse config JSON:"
               << parseError.errorString();
    return false;
  }

  QJsonObject root = doc.object();

  // Import server settings
  if (root.contains("server")) {
    QJsonObject server = root["server"].toObject();
    if (server.contains("address")) {
      serverAddressEdit_->setText(server["address"].toString());
    }
    if (server.contains("port")) {
      serverPortSpinBox_->setValue(server["port"].toInt(4433));
    }
  }

  // Import crypto settings
  if (root.contains("crypto")) {
    QJsonObject crypto = root["crypto"].toObject();
    if (crypto.contains("keyFile")) {
      keyFileEdit_->setText(crypto["keyFile"].toString());
    }
  }

  // Import feature settings
  if (root.contains("advanced")) {
    QJsonObject advanced = root["advanced"].toObject();
    if (advanced.contains("obfuscation")) {
      obfuscationCheck_->setChecked(advanced["obfuscation"].toBool(true));
    }
  }

  if (root.contains("dpi")) {
    QJsonObject dpi = root["dpi"].toObject();
    if (dpi.contains("mode")) {
      int mode = dpi["mode"].toInt(0);
      int idx = dpiModeCombo_->findData(mode);
      if (idx >= 0) {
        dpiModeCombo_->setCurrentIndex(idx);
      }
    }
  }

  if (root.contains("routing")) {
    QJsonObject routing = root["routing"].toObject();
    if (routing.contains("routeAllTraffic")) {
      routeAllTrafficCheck_->setChecked(
          routing["routeAllTraffic"].toBool(true));
    }
  }

  if (root.contains("connection")) {
    QJsonObject connection = root["connection"].toObject();
    if (connection.contains("autoReconnect")) {
      autoReconnectCheck_->setChecked(
          connection["autoReconnect"].toBool(true));
    }
  }

  qDebug() << "[SetupWizard] Configuration imported successfully";
  return true;
}

// ===================== Key File =====================

void SetupWizard::onBrowseKeyFile() {
  QString filePath = QFileDialog::getOpenFileName(
      this, tr("Select Key File"),
      QDir::homePath(),
      tr("Key Files (*.key *.pem *.bin);;All Files (*)"));

  if (!filePath.isEmpty()) {
    keyFileEdit_->setText(filePath);
    QFileInfo info(filePath);
    if (info.exists() && info.isFile()) {
      keyFileStatusLabel_->setText(
          tr("Key file found (%1 bytes)").arg(info.size()));
      keyFileStatusLabel_->setStyleSheet("color: #3fb950; font-size: 12px;");
    } else {
      keyFileStatusLabel_->setText(tr("File not found"));
      keyFileStatusLabel_->setStyleSheet("color: #f85149; font-size: 12px;");
    }
    keyFileStatusLabel_->setVisible(true);
  }
}

// ===================== Test Connection =====================

void SetupWizard::onTestConnection() {
  qDebug() << "[SetupWizard] Testing connection...";

  QString address = serverAddressEdit_->text().trimmed();
  int port = serverPortSpinBox_->value();

  if (address.isEmpty()) {
    testResultLabel_->setText(tr("No server address configured"));
    testResultLabel_->setStyleSheet("color: #f85149; font-size: 13px;");
    testResultLabel_->setVisible(true);
    return;
  }

  testConnectionButton_->setEnabled(false);
  testConnectionButton_->setText(tr("Testing..."));
  testResultLabel_->setText(tr("Connecting to %1:%2...").arg(address).arg(port));
  testResultLabel_->setStyleSheet("color: #d29922; font-size: 13px;");
  testResultLabel_->setVisible(true);

  // Perform a basic connectivity test using QTcpSocket
  // This is a simple reachability check — actual VPN authentication
  // happens through the daemon.
  auto* socket = new QTcpSocket(this);
  connect(socket, &QTcpSocket::connected, this, [this, socket]() {
    testResultLabel_->setText(tr("Server is reachable!"));
    testResultLabel_->setStyleSheet("color: #3fb950; font-size: 13px;");
    testConnectionButton_->setEnabled(true);
    testConnectionButton_->setText(tr("Test Connection"));
    socket->deleteLater();
  });

  connect(socket, &QTcpSocket::errorOccurred, this,
          [this, socket](QAbstractSocket::SocketError) {
    testResultLabel_->setText(
        tr("Could not reach server: %1").arg(socket->errorString()));
    testResultLabel_->setStyleSheet("color: #f85149; font-size: 13px;");
    testConnectionButton_->setEnabled(true);
    testConnectionButton_->setText(tr("Test Connection"));
    socket->deleteLater();
  });

  socket->connectToHost(address, static_cast<quint16>(port));

  // Timeout after 5 seconds
  QTimer::singleShot(5000, this, [this, socket]() {
    if (socket->state() != QAbstractSocket::ConnectedState) {
      socket->abort();
      testResultLabel_->setText(tr("Connection timed out"));
      testResultLabel_->setStyleSheet("color: #f85149; font-size: 13px;");
      testConnectionButton_->setEnabled(true);
      testConnectionButton_->setText(tr("Test Connection"));
    }
  });
}

}  // namespace veil::gui
