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
#include "notification_preferences.h"
#include "notification_history_dialog.h"

#ifdef _WIN32
#include "windows/shortcut_manager.h"
#endif

namespace veil::gui {

// NOLINTBEGIN(readability-implicit-bool-conversion)

SettingsWidget::SettingsWidget(QWidget* parent) : QWidget(parent) {
  // Initialize validation debounce timer
  validationDebounceTimer_ = new QTimer(this);
  validationDebounceTimer_->setSingleShot(true);
  validationDebounceTimer_->setInterval(200);
  connect(validationDebounceTimer_, &QTimer::timeout, this, &SettingsWidget::onValidationDebounceTimeout);

  setupUi();
  loadSettings();
}

void SettingsWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(20);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge(), spacing::kPaddingMedium(),
                                  spacing::kPaddingXLarge(), spacing::kPaddingMedium());

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

  // Title and Search
  auto* titleRow = new QHBoxLayout();
  auto* titleLabel = new QLabel("Settings", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 700; color: #f0f6fc; margin-bottom: 8px;")
                                .arg(fonts::kFontSizeHeadline()));
  titleRow->addWidget(titleLabel);

  titleRow->addStretch();

  // Search/filter box
  searchEdit_ = new QLineEdit(this);
  searchEdit_->setPlaceholderText("🔍 Search settings...");
  searchEdit_->setFixedWidth(250);
  searchEdit_->setStyleSheet(R"(
    QLineEdit {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
      padding: 10px 16px;
      color: #f0f6fc;
      font-size: 14px;
    }
    QLineEdit:focus {
      border-color: #58a6ff;
    }
  )");
  connect(searchEdit_, &QLineEdit::textChanged, [this](const QString& text) {
    QString lowerText = text.toLower();
    // Filter sections based on search text
    bool showAll = lowerText.isEmpty();

    serverSection_->setVisible(showAll || serverSection_->title().toLower().contains(lowerText) ||
                               QString("server address port").contains(lowerText));
    cryptoSection_->setVisible(showAll || cryptoSection_->title().toLower().contains(lowerText) ||
                               QString("key crypto obfuscation seed").contains(lowerText));
    startupSection_->setVisible(showAll || startupSection_->title().toLower().contains(lowerText) ||
                                QString("startup minimized auto-connect launch windows tray").contains(lowerText));
    tunInterfaceSection_->setVisible(showAll || tunInterfaceSection_->title().toLower().contains(lowerText) ||
                                     QString("tun interface ip netmask mtu").contains(lowerText));
    routingSection_->setVisible(showAll || routingSection_->title().toLower().contains(lowerText) ||
                                QString("routing tunnel split traffic").contains(lowerText));
    connectionSection_->setVisible(showAll || connectionSection_->title().toLower().contains(lowerText) ||
                                   QString("connection reconnect").contains(lowerText));
    dpiBypassSection_->setVisible(showAll || dpiBypassSection_->title().toLower().contains(lowerText) ||
                                  QString("dpi bypass obfuscation mode").contains(lowerText));
    notificationSection_->setVisible(showAll || notificationSection_->title().toLower().contains(lowerText) ||
                                     QString("notification alerts sound tray minimize update history").contains(lowerText));
    advancedSection_->setVisible(showAll || advancedSection_->title().toLower().contains(lowerText) ||
                                 QString("advanced developer logging").contains(lowerText));
  });
  titleRow->addWidget(searchEdit_);

  mainLayout->addLayout(titleRow);

  // Validation summary banner
  validationSummaryBanner_ = new QLabel(this);
  validationSummaryBanner_->setWordWrap(true);
  validationSummaryBanner_->setStyleSheet(QString(
      "color: %1; "
      "background: rgba(248, 81, 73, 0.08); "
      "border: 1px solid rgba(248, 81, 73, 0.3); "
      "border-radius: 10px; "
      "padding: 12px 16px; "
      "font-size: 14px; "
      "font-weight: 500;")
      .arg(colors::dark::kAccentError));
  validationSummaryBanner_->hide();
  mainLayout->addWidget(validationSummaryBanner_);

  // Advanced mode toggle
  showAdvancedCheck_ = new QCheckBox("Show Advanced Settings", this);
  showAdvancedCheck_->setChecked(true);  // Show all by default initially
  showAdvancedCheck_->setStyleSheet(R"(
    QCheckBox {
      color: #8b949e;
      font-size: 13px;
      font-weight: 500;
      padding: 8px 0;
    }
    QCheckBox:hover {
      color: #f0f6fc;
    }
  )");
  connect(showAdvancedCheck_, &QCheckBox::toggled, this, &SettingsWidget::onAdvancedModeToggled);
  mainLayout->addWidget(showAdvancedCheck_);

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

  // Create collapsible sections
  serverSection_ = new CollapsibleSection("Server Configuration", scrollWidget);
  serverSection_->setContent(createServerSection());
  serverSection_->setCollapsedImmediate(false);  // Expanded by default
  scrollLayout->addWidget(serverSection_);

  cryptoSection_ = new CollapsibleSection("Cryptographic Settings", scrollWidget);
  cryptoSection_->setContent(createCryptoSection());
  cryptoSection_->setCollapsedImmediate(true);  // Collapsed by default
  scrollLayout->addWidget(cryptoSection_);

  startupSection_ = new CollapsibleSection("Startup Options", scrollWidget);
  startupSection_->setContent(createStartupSection());
  startupSection_->setCollapsedImmediate(true);  // Collapsed by default
  scrollLayout->addWidget(startupSection_);

  tunInterfaceSection_ = new CollapsibleSection("TUN Interface", scrollWidget);
  tunInterfaceSection_->setContent(createTunInterfaceSection());
  tunInterfaceSection_->setCollapsedImmediate(true);  // Collapsed by default (advanced)
  scrollLayout->addWidget(tunInterfaceSection_);

  routingSection_ = new CollapsibleSection("Routing", scrollWidget);
  routingSection_->setContent(createRoutingSection());
  routingSection_->setCollapsedImmediate(true);  // Collapsed by default
  scrollLayout->addWidget(routingSection_);

  connectionSection_ = new CollapsibleSection("Connection", scrollWidget);
  connectionSection_->setContent(createConnectionSection());
  connectionSection_->setCollapsedImmediate(true);  // Collapsed by default
  scrollLayout->addWidget(connectionSection_);

  dpiBypassSection_ = new CollapsibleSection("DPI Bypass Mode", scrollWidget);
  dpiBypassSection_->setContent(createDpiBypassSection());
  dpiBypassSection_->setCollapsedImmediate(true);  // Collapsed by default (advanced)
  scrollLayout->addWidget(dpiBypassSection_);

  notificationSection_ = new CollapsibleSection("Notifications", scrollWidget);
  notificationSection_->setContent(createNotificationSection());
  notificationSection_->setCollapsedImmediate(true);  // Collapsed by default
  scrollLayout->addWidget(notificationSection_);

  advancedSection_ = new CollapsibleSection("Advanced", scrollWidget);
  advancedSection_->setContent(createAdvancedSection());
  advancedSection_->setCollapsedImmediate(true);  // Collapsed by default (advanced)
  scrollLayout->addWidget(advancedSection_);

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

QWidget* SettingsWidget::createServerSection() {
  auto* group = new QGroupBox();
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  // Server Address
  auto* serverLabel = new QLabel("Server Address", group);
  serverLabel->setProperty("textStyle", "secondary");
  layout->addWidget(serverLabel);

  auto* serverRow = new QHBoxLayout();
  serverAddressEdit_ = new QLineEdit(group);
  serverAddressEdit_->setPlaceholderText("vpn.example.com or 192.168.1.1");
  connect(serverAddressEdit_, &QLineEdit::textChanged, this, &SettingsWidget::onServerAddressChanged);
  serverRow->addWidget(serverAddressEdit_, 1);

  serverValidationIndicator_ = new QLabel(group);
  serverValidationIndicator_->setFixedSize(scaleDpi(24), scaleDpi(24));
  serverValidationIndicator_->setAlignment(Qt::AlignCenter);
  serverValidationIndicator_->setStyleSheet("font-size: 18px;");
  serverRow->addWidget(serverValidationIndicator_);
  layout->addLayout(serverRow);

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
  portSpinBox_->setFixedWidth(scaleDpi(100));
  connect(portSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onPortChanged);
  portRow->addWidget(portSpinBox_);

  layout->addLayout(portRow);

  return group;
}

QWidget* SettingsWidget::createCryptoSection() {
  auto* group = new QGroupBox();
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
    validationDebounceTimer_->start();
    hasUnsavedChanges_ = true;
  });
  keyFileRow->addWidget(keyFileEdit_, 1);

  keyFileValidationIndicator_ = new QLabel(group);
  keyFileValidationIndicator_->setFixedSize(scaleDpi(24), scaleDpi(24));
  keyFileValidationIndicator_->setAlignment(Qt::AlignCenter);
  keyFileValidationIndicator_->setStyleSheet("font-size: 18px;");
  keyFileRow->addWidget(keyFileValidationIndicator_);

  browseKeyFileButton_ = new QPushButton("\U0001F4C2", group);  // Folder icon
  browseKeyFileButton_->setFixedSize(scaleDpi(40), scaleDpi(40));
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
    validationDebounceTimer_->start();
    hasUnsavedChanges_ = true;
  });
  obfuscationRow->addWidget(obfuscationSeedEdit_, 1);

  obfuscationSeedValidationIndicator_ = new QLabel(group);
  obfuscationSeedValidationIndicator_->setFixedSize(scaleDpi(24), scaleDpi(24));
  obfuscationSeedValidationIndicator_->setAlignment(Qt::AlignCenter);
  obfuscationSeedValidationIndicator_->setStyleSheet("font-size: 18px;");
  obfuscationRow->addWidget(obfuscationSeedValidationIndicator_);

  browseObfuscationSeedButton_ = new QPushButton("\U0001F4C2", group);  // Folder icon
  browseObfuscationSeedButton_->setFixedSize(scaleDpi(40), scaleDpi(40));
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

  return group;
}

QWidget* SettingsWidget::createStartupSection() {
  auto* group = new QGroupBox();
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  startMinimizedCheck_ = new QCheckBox("Start minimized to tray", group);
  startMinimizedCheck_->setToolTip("Launch application minimized to system tray instead of showing main window");
  layout->addWidget(startMinimizedCheck_);

  autoConnectOnStartupCheck_ = new QCheckBox("Auto-connect on startup", group);
  autoConnectOnStartupCheck_->setToolTip("Automatically connect to VPN when application starts");
  layout->addWidget(autoConnectOnStartupCheck_);

  launchOnWindowsStartupCheck_ = new QCheckBox("Launch on Windows startup", group);
  launchOnWindowsStartupCheck_->setToolTip("Automatically start VEIL VPN when Windows starts (requires administrator privileges)");
  connect(launchOnWindowsStartupCheck_, &QCheckBox::stateChanged, this, &SettingsWidget::onLaunchOnStartupChanged);
  layout->addWidget(launchOnWindowsStartupCheck_);

  // Add spacing
  layout->addSpacing(12);

  // Desktop shortcut section
  auto* desktopShortcutLabel = new QLabel("Desktop Shortcut", group);
  desktopShortcutLabel->setStyleSheet("font-weight: 600; color: #f0f6fc; font-size: 13px;");
  layout->addWidget(desktopShortcutLabel);

  auto* desktopShortcutRow = new QHBoxLayout();
  desktopShortcutRow->setSpacing(12);

  createDesktopShortcutButton_ = new QPushButton("Create Desktop Shortcut", group);
  createDesktopShortcutButton_->setToolTip("Create a shortcut on the desktop for quick access");
  createDesktopShortcutButton_->setCursor(Qt::PointingHandCursor);
  createDesktopShortcutButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(88, 166, 255, 0.15);
      border: 1px solid rgba(88, 166, 255, 0.3);
      border-radius: 8px;
      color: #58a6ff;
      padding: 8px 16px;
      font-size: 13px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(88, 166, 255, 0.25);
      border-color: #58a6ff;
    }
    QPushButton:disabled {
      background: rgba(139, 148, 158, 0.1);
      border-color: rgba(139, 148, 158, 0.2);
      color: #8b949e;
    }
  )");
  connect(createDesktopShortcutButton_, &QPushButton::clicked, this, &SettingsWidget::onCreateDesktopShortcut);
  desktopShortcutRow->addWidget(createDesktopShortcutButton_);

  desktopShortcutStatusLabel_ = new QLabel("", group);
  desktopShortcutStatusLabel_->setStyleSheet("color: #8b949e; font-size: 12px;");
  desktopShortcutRow->addWidget(desktopShortcutStatusLabel_);
  desktopShortcutRow->addStretch();

  layout->addLayout(desktopShortcutRow);

  // Start Menu shortcut section
  auto* startMenuShortcutLabel = new QLabel("Start Menu Entry", group);
  startMenuShortcutLabel->setStyleSheet("font-weight: 600; color: #f0f6fc; font-size: 13px; margin-top: 8px;");
  layout->addWidget(startMenuShortcutLabel);

  auto* startMenuShortcutRow = new QHBoxLayout();
  startMenuShortcutRow->setSpacing(12);

  createStartMenuShortcutButton_ = new QPushButton("Create Start Menu Entry", group);
  createStartMenuShortcutButton_->setToolTip("Create a shortcut in the Start Menu for easy access");
  createStartMenuShortcutButton_->setCursor(Qt::PointingHandCursor);
  createStartMenuShortcutButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(88, 166, 255, 0.15);
      border: 1px solid rgba(88, 166, 255, 0.3);
      border-radius: 8px;
      color: #58a6ff;
      padding: 8px 16px;
      font-size: 13px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: rgba(88, 166, 255, 0.25);
      border-color: #58a6ff;
    }
    QPushButton:disabled {
      background: rgba(139, 148, 158, 0.1);
      border-color: rgba(139, 148, 158, 0.2);
      color: #8b949e;
    }
  )");
  connect(createStartMenuShortcutButton_, &QPushButton::clicked, this, &SettingsWidget::onCreateStartMenuShortcut);
  startMenuShortcutRow->addWidget(createStartMenuShortcutButton_);

  startMenuShortcutStatusLabel_ = new QLabel("", group);
  startMenuShortcutStatusLabel_->setStyleSheet("color: #8b949e; font-size: 12px;");
  startMenuShortcutRow->addWidget(startMenuShortcutStatusLabel_);
  startMenuShortcutRow->addStretch();

  layout->addLayout(startMenuShortcutRow);

  // Info text
  auto* infoLabel = new QLabel(
      "Startup options control how the application behaves when launched.\n"
      "Note: Windows service auto-starts by default; this controls the GUI application.\n\n"
      "Shortcuts provide quick access to the application from the Desktop or Start Menu.",
      group);
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                   "background: rgba(88, 166, 255, 0.08); "
                                   "border: 1px solid rgba(88, 166, 255, 0.2); "
                                   "border-radius: 10px; margin-top: 12px;")
                               .arg(colors::dark::kAccentPrimary));
  layout->addWidget(infoLabel);

  return group;
}

QWidget* SettingsWidget::createRoutingSection() {
  auto* group = new QGroupBox();
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

  // Per-application routing (Phase 1: UI/UX foundation)
  layout->addSpacing(12);

  enablePerAppRoutingCheck_ = new QCheckBox("Enable per-application routing (Experimental)", group);
  enablePerAppRoutingCheck_->setToolTip(
    "Configure VPN routing on a per-application basis.\n"
    "Note: This is a UI preview. Backend routing is not yet implemented.");
  enablePerAppRoutingCheck_->setEnabled(false);  // Disabled until backend is ready
  layout->addWidget(enablePerAppRoutingCheck_);

  // App split tunnel widget (collapsible)
  appSplitTunnelWidget_ = new AppSplitTunnelWidget(group);
  appSplitTunnelWidget_->hide();  // Hidden by default
  layout->addWidget(appSplitTunnelWidget_);

  connect(enablePerAppRoutingCheck_, &QCheckBox::toggled, [this](bool checked) {
    appSplitTunnelWidget_->setVisible(checked);
    if (checked) {
      hasUnsavedChanges_ = true;
    }
  });

  connect(appSplitTunnelWidget_, &AppSplitTunnelWidget::settingsChanged, [this]() {
    hasUnsavedChanges_ = true;
  });

  // Add informational label about experimental status
  auto* infoLabel = new QLabel(
    "\U0001F6A7 <b>Experimental Feature:</b> Per-application routing UI is available for preview. "
    "Full routing functionality requires daemon integration and will be implemented in Phase 2.",
    group);
  infoLabel->setProperty("textStyle", "secondary");
  infoLabel->setStyleSheet(QString("color: %1; font-size: 11px; padding: 8px; background-color: rgba(255, 165, 0, 0.1); border-radius: 4px;")
                           .arg(colors::dark::kAccentWarning));
  infoLabel->setWordWrap(true);
  infoLabel->hide();  // Hidden by default
  layout->addWidget(infoLabel);

  connect(enablePerAppRoutingCheck_, &QCheckBox::toggled, infoLabel, &QLabel::setVisible);

  return group;
}

QWidget* SettingsWidget::createConnectionSection() {
  auto* group = new QGroupBox();
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
  reconnectIntervalSpinBox_->setFixedWidth(scaleDpi(100));
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
  maxReconnectAttemptsSpinBox_->setFixedWidth(scaleDpi(100));
  attemptsRow->addWidget(maxReconnectAttemptsSpinBox_);

  layout->addLayout(attemptsRow);

  return group;
}

QWidget* SettingsWidget::createDpiBypassSection() {
  auto* group = new QGroupBox();
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

  return group;
}

QWidget* SettingsWidget::createTunInterfaceSection() {
  auto* group = new QGroupBox();
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

  auto* ipRow = new QHBoxLayout();
  tunIpAddressEdit_ = new QLineEdit(group);
  tunIpAddressEdit_->setPlaceholderText("10.8.0.2");
  tunIpAddressEdit_->setToolTip("IP address assigned to the TUN interface");
  connect(tunIpAddressEdit_, &QLineEdit::textChanged, [this]() {
    validationDebounceTimer_->start();
    hasUnsavedChanges_ = true;
  });
  ipRow->addWidget(tunIpAddressEdit_, 1);

  tunIpValidationIndicator_ = new QLabel(group);
  tunIpValidationIndicator_->setFixedSize(scaleDpi(24), scaleDpi(24));
  tunIpValidationIndicator_->setAlignment(Qt::AlignCenter);
  tunIpValidationIndicator_->setStyleSheet("font-size: 18px;");
  ipRow->addWidget(tunIpValidationIndicator_);
  layout->addLayout(ipRow);

  tunIpValidationLabel_ = new QLabel(group);
  tunIpValidationLabel_->setStyleSheet(QString("color: %1; font-size: 12px;").arg(colors::dark::kAccentError));
  tunIpValidationLabel_->hide();
  layout->addWidget(tunIpValidationLabel_);

  // Netmask
  auto* netmaskLabel = new QLabel("Netmask", group);
  netmaskLabel->setProperty("textStyle", "secondary");
  layout->addWidget(netmaskLabel);

  auto* netmaskRow = new QHBoxLayout();
  tunNetmaskEdit_ = new QLineEdit(group);
  tunNetmaskEdit_->setPlaceholderText("255.255.255.0");
  tunNetmaskEdit_->setToolTip("Subnet mask for the TUN interface");
  connect(tunNetmaskEdit_, &QLineEdit::textChanged, [this]() {
    validationDebounceTimer_->start();
    hasUnsavedChanges_ = true;
  });
  netmaskRow->addWidget(tunNetmaskEdit_, 1);

  tunNetmaskValidationIndicator_ = new QLabel(group);
  tunNetmaskValidationIndicator_->setFixedSize(scaleDpi(24), scaleDpi(24));
  tunNetmaskValidationIndicator_->setAlignment(Qt::AlignCenter);
  tunNetmaskValidationIndicator_->setStyleSheet("font-size: 18px;");
  netmaskRow->addWidget(tunNetmaskValidationIndicator_);
  layout->addLayout(netmaskRow);

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
  tunMtuSpinBox_->setFixedWidth(scaleDpi(130));
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

  return group;
}

QWidget* SettingsWidget::createNotificationSection() {
  auto* group = new QGroupBox();
  auto* layout = new QVBoxLayout(group);
  layout->setSpacing(12);

  // Global notification toggle
  notificationsEnabledCheck_ = new QCheckBox("Enable notifications", group);
  notificationsEnabledCheck_->setToolTip("Master toggle for all system tray notifications");
  connect(notificationsEnabledCheck_, &QCheckBox::toggled, [this](bool checked) {
    hasUnsavedChanges_ = true;
    // Enable/disable per-event checkboxes based on master toggle
    notificationSoundCheck_->setEnabled(checked);
    showNotificationDetailsCheck_->setEnabled(checked);
    connectionEstablishedCheck_->setEnabled(checked);
    connectionLostCheck_->setEnabled(checked);
    minimizeToTrayCheck_->setEnabled(checked);
    updatesAvailableCheck_->setEnabled(checked);
    errorNotificationsCheck_->setEnabled(checked);
  });
  layout->addWidget(notificationsEnabledCheck_);

  // Notification sound
  notificationSoundCheck_ = new QCheckBox("Play notification sound", group);
  notificationSoundCheck_->setToolTip("Play system sound when notifications appear");
  connect(notificationSoundCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(notificationSoundCheck_);

  // Show details
  showNotificationDetailsCheck_ = new QCheckBox("Show notification details", group);
  showNotificationDetailsCheck_->setToolTip("Include detailed information in notification messages");
  connect(showNotificationDetailsCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(showNotificationDetailsCheck_);

  // Separator
  auto* separator = new QFrame(group);
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("background-color: rgba(255, 255, 255, 0.08);");
  layout->addWidget(separator);

  // Per-event notification toggles
  auto* eventLabel = new QLabel("Notify me when:", group);
  eventLabel->setStyleSheet("font-weight: 600; color: #f0f6fc; margin-top: 8px;");
  layout->addWidget(eventLabel);

  connectionEstablishedCheck_ = new QCheckBox("Connection is established", group);
  connectionEstablishedCheck_->setToolTip("Show notification when VPN connection succeeds");
  connect(connectionEstablishedCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(connectionEstablishedCheck_);

  connectionLostCheck_ = new QCheckBox("Connection is lost or disconnected", group);
  connectionLostCheck_->setToolTip("Show notification when VPN connection drops");
  connect(connectionLostCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(connectionLostCheck_);

  minimizeToTrayCheck_ = new QCheckBox("Application is minimized to tray", group);
  minimizeToTrayCheck_->setToolTip("Show notification when window is minimized to system tray");
  connect(minimizeToTrayCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(minimizeToTrayCheck_);

  updatesAvailableCheck_ = new QCheckBox("Software updates are available", group);
  updatesAvailableCheck_->setToolTip("Show notification when new version is available");
  connect(updatesAvailableCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(updatesAvailableCheck_);

  errorNotificationsCheck_ = new QCheckBox("Connection errors occur", group);
  errorNotificationsCheck_->setToolTip("Show notification when connection or configuration errors happen");
  connect(errorNotificationsCheck_, &QCheckBox::toggled, [this]() {
    hasUnsavedChanges_ = true;
  });
  layout->addWidget(errorNotificationsCheck_);

  // Notification history
  auto* historyLabel = new QLabel("Notification History", group);
  historyLabel->setStyleSheet("font-weight: 600; color: #f0f6fc; margin-top: 16px;");
  layout->addWidget(historyLabel);

  auto* historyButtonRow = new QHBoxLayout();
  viewHistoryButton_ = new QPushButton("View History", group);
  viewHistoryButton_->setToolTip("View recent notification history");
  viewHistoryButton_->setStyleSheet(R"(
    QPushButton {
      background: #238636;
      color: #ffffff;
      border: none;
      border-radius: 6px;
      padding: 8px 16px;
      font-size: 14px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: #2ea043;
    }
  )");
  connect(viewHistoryButton_, &QPushButton::clicked, this, [this]() {
    auto* dialog = new NotificationHistoryDialog(this);
    dialog->exec();
    dialog->deleteLater();
  });
  historyButtonRow->addWidget(viewHistoryButton_);

  clearHistoryButton_ = new QPushButton("Clear History", group);
  clearHistoryButton_->setToolTip("Delete all notification history");
  clearHistoryButton_->setStyleSheet(R"(
    QPushButton {
      background: #da3633;
      color: #ffffff;
      border: none;
      border-radius: 6px;
      padding: 8px 16px;
      font-size: 14px;
      font-weight: 500;
    }
    QPushButton:hover {
      background: #f85149;
    }
  )");
  connect(clearHistoryButton_, &QPushButton::clicked, this, [this]() {
    auto reply = QMessageBox::question(
        this, "Clear History",
        "Are you sure you want to clear all notification history?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
      auto& prefs = NotificationPreferences::instance();
      prefs.clearHistory();
      QMessageBox::information(this, "History Cleared",
                              "Notification history has been cleared.");
    }
  });
  historyButtonRow->addWidget(clearHistoryButton_);
  historyButtonRow->addStretch();
  layout->addLayout(historyButtonRow);

  // Info text
  auto* infoLabel = new QLabel(
      "Configure which system tray notifications you want to receive. "
      "Notifications help you stay informed about VPN connection status and important events.",
      group);
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                   "background: rgba(88, 166, 255, 0.08); "
                                   "border: 1px solid rgba(88, 166, 255, 0.2); "
                                   "border-radius: 10px;")
                               .arg(colors::dark::kAccentPrimary));
  layout->addWidget(infoLabel);

  return group;
}

QWidget* SettingsWidget::createAdvancedSection() {
  auto* group = new QGroupBox();
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

  // Theme selector
  auto* themeLayout = new QHBoxLayout();
  auto* themeLabel = new QLabel("Theme:", group);
  themeCombo_ = new QComboBox(group);
  themeCombo_->addItem("Dark", static_cast<int>(Theme::kDark));
  themeCombo_->addItem("Light", static_cast<int>(Theme::kLight));
  themeCombo_->addItem("System", static_cast<int>(Theme::kSystem));
  themeCombo_->setToolTip("Choose application theme (System follows Windows dark mode setting)");
  connect(themeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this]() {
            hasUnsavedChanges_ = true;
            // Apply theme immediately for preview
            Theme selectedTheme = static_cast<Theme>(themeCombo_->currentData().toInt());
            emit themeChanged(selectedTheme);
          });
  themeLayout->addWidget(themeLabel);
  themeLayout->addWidget(themeCombo_, 1);
  layout->addLayout(themeLayout);

  // Language selector
  auto* languageLayout = new QHBoxLayout();
  auto* languageLabel = new QLabel("Language:", group);
  languageCombo_ = new QComboBox(group);
  languageCombo_->addItem("English", "en");
  languageCombo_->addItem("Русский (Russian)", "ru");
  languageCombo_->addItem("中文 (Chinese)", "zh");
  languageCombo_->setToolTip("Select application language (requires restart)");
  connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this]() {
            hasUnsavedChanges_ = true;
          });
  languageLayout->addWidget(languageLabel);
  languageLayout->addWidget(languageCombo_, 1);
  layout->addLayout(languageLayout);

  // Language change info label
  auto* langInfoLabel = new QLabel(
      "Note: Application must be restarted for language changes to take effect.",
      group);
  langInfoLabel->setWordWrap(true);
  langInfoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 12px; "
                                      "background: rgba(88, 166, 255, 0.08); "
                                      "border: 1px solid rgba(88, 166, 255, 0.2); "
                                      "border-radius: 10px;")
                                  .arg(colors::dark::kAccentPrimary));
  layout->addWidget(langInfoLabel);

  // Reset first-run wizard button
  auto* resetWizardButton = new QPushButton("Reset Setup Wizard", group);
  resetWizardButton->setProperty("buttonStyle", "ghost");
  resetWizardButton->setToolTip("Reset the first-run flag so the setup wizard shows on next launch");
  resetWizardButton->setCursor(Qt::PointingHandCursor);
  connect(resetWizardButton, &QPushButton::clicked, this, [this]() {
    QSettings settings("VEIL", "VPN Client");
    settings.setValue("app/firstRunCompleted", false);
    settings.sync();
    QMessageBox::information(this, tr("Setup Wizard Reset"),
        tr("The setup wizard will be shown the next time you start the application."));
  });
  layout->addWidget(resetWizardButton);

  return group;
}

void SettingsWidget::onServerAddressChanged() {
  validationDebounceTimer_->start();
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

void SettingsWidget::onLaunchOnStartupChanged([[maybe_unused]] int state) {
#ifdef _WIN32
  // Update Windows registry to add/remove application from startup
  QSettings registrySettings(
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      QSettings::NativeFormat);

  const QString appName = "VEIL VPN Client";

  if (state == Qt::Checked) {
    // Add to startup - get path to current executable
    QString appPath = QApplication::applicationFilePath();
    // Wrap path in quotes to handle spaces
    QString startupCommand = QString("\"%1\" --minimized").arg(appPath);
    registrySettings.setValue(appName, startupCommand);
    qDebug() << "[SettingsWidget] Added to Windows startup:" << startupCommand;
  } else {
    // Remove from startup
    registrySettings.remove(appName);
    qDebug() << "[SettingsWidget] Removed from Windows startup";
  }

  registrySettings.sync();
  hasUnsavedChanges_ = true;
#else
  (void)state;  // Used only on Windows
  // Not Windows - disable checkbox
  launchOnWindowsStartupCheck_->setChecked(false);
  launchOnWindowsStartupCheck_->setEnabled(false);
  launchOnWindowsStartupCheck_->setToolTip("This feature is only available on Windows");
#endif
}

void SettingsWidget::onCreateDesktopShortcut() {
#ifdef _WIN32
  // Get the path to the main launcher executable
  QString appPath = QApplication::applicationFilePath();

  // Replace veil-client-gui.exe with veil-vpn.exe (the unified launcher)
  QFileInfo appInfo(appPath);
  QString launcherPath = appInfo.absolutePath() + "/veil-vpn.exe";

  // Check if the launcher exists, otherwise fall back to the current executable
  if (!QFileInfo::exists(launcherPath)) {
    launcherPath = appPath;
    qWarning() << "[SettingsWidget] Launcher not found at" << launcherPath << ", using current executable";
  }

  std::string error;
  bool success = veil::windows::ShortcutManager::createShortcut(
      veil::windows::ShortcutManager::Location::kDesktop,
      "VEIL VPN",
      launcherPath.toStdString(),
      error,
      "",  // arguments
      "VEIL VPN Client - Secure VPN Connection",
      "",  // icon_path (use executable's icon)
      0,   // icon_index
      ""   // working_dir (use executable's directory)
  );

  if (success) {
    desktopShortcutStatusLabel_->setText("✓ Created");
    desktopShortcutStatusLabel_->setStyleSheet("color: #3fb950; font-size: 12px;");
    createDesktopShortcutButton_->setEnabled(false);
    qDebug() << "[SettingsWidget] Desktop shortcut created successfully";

    QMessageBox::information(this, "Success",
        "Desktop shortcut created successfully!\n\n"
        "You can now launch VEIL VPN from your desktop.");
  } else {
    desktopShortcutStatusLabel_->setText("✗ Failed");
    desktopShortcutStatusLabel_->setStyleSheet("color: #f85149; font-size: 12px;");
    qWarning() << "[SettingsWidget] Failed to create desktop shortcut:" << QString::fromStdString(error);

    QMessageBox::warning(this, "Error",
        QString("Failed to create desktop shortcut:\n\n%1").arg(QString::fromStdString(error)));
  }
#else
  QMessageBox::information(this, "Not Available",
      "Shortcut creation is only available on Windows.");
#endif
}

void SettingsWidget::onCreateStartMenuShortcut() {
#ifdef _WIN32
  // Get the path to the main launcher executable
  QString appPath = QApplication::applicationFilePath();

  // Replace veil-client-gui.exe with veil-vpn.exe (the unified launcher)
  QFileInfo appInfo(appPath);
  QString launcherPath = appInfo.absolutePath() + "/veil-vpn.exe";

  // Check if the launcher exists, otherwise fall back to the current executable
  if (!QFileInfo::exists(launcherPath)) {
    launcherPath = appPath;
    qWarning() << "[SettingsWidget] Launcher not found at" << launcherPath << ", using current executable";
  }

  std::string error;
  bool success = veil::windows::ShortcutManager::createShortcut(
      veil::windows::ShortcutManager::Location::kStartMenu,
      "VEIL VPN",
      launcherPath.toStdString(),
      error,
      "",  // arguments
      "VEIL VPN Client - Secure VPN Connection",
      "",  // icon_path (use executable's icon)
      0,   // icon_index
      ""   // working_dir (use executable's directory)
  );

  if (success) {
    startMenuShortcutStatusLabel_->setText("✓ Created");
    startMenuShortcutStatusLabel_->setStyleSheet("color: #3fb950; font-size: 12px;");
    createStartMenuShortcutButton_->setEnabled(false);
    qDebug() << "[SettingsWidget] Start Menu shortcut created successfully";

    QMessageBox::information(this, "Success",
        "Start Menu entry created successfully!\n\n"
        "You can now find VEIL VPN in your Start Menu.");
  } else {
    startMenuShortcutStatusLabel_->setText("✗ Failed");
    startMenuShortcutStatusLabel_->setStyleSheet("color: #f85149; font-size: 12px;");
    qWarning() << "[SettingsWidget] Failed to create Start Menu shortcut:" << QString::fromStdString(error);

    QMessageBox::warning(this, "Error",
        QString("Failed to create Start Menu entry:\n\n%1").arg(QString::fromStdString(error)));
  }
#else
  QMessageBox::information(this, "Not Available",
      "Shortcut creation is only available on Windows.");
#endif
}

void SettingsWidget::validateSettings() {
  bool allValid = true;

  // Validate server address
  QString address = serverAddressEdit_->text().trimmed();
  if (address.isEmpty()) {
    setFieldValidationState(serverAddressEdit_, serverValidationIndicator_,
                            ValidationState::kNeutral);
    serverValidationLabel_->hide();
  } else if (isValidHostname(address) || isValidIpAddress(address)) {
    setFieldValidationState(serverAddressEdit_, serverValidationIndicator_,
                            ValidationState::kValid);
    serverValidationLabel_->hide();
  } else {
    setFieldValidationState(serverAddressEdit_, serverValidationIndicator_,
                            ValidationState::kInvalid, "Invalid server address format");
    serverValidationLabel_->setText("Invalid server address format");
    serverValidationLabel_->show();
    allValid = false;
  }

  // Validate key file
  QString keyPath = keyFileEdit_->text().trimmed();
  if (keyPath.isEmpty()) {
    setFieldValidationState(keyFileEdit_, keyFileValidationIndicator_,
                            ValidationState::kNeutral);
    keyFileValidationLabel_->hide();
  } else if (isValidFilePath(keyPath)) {
    setFieldValidationState(keyFileEdit_, keyFileValidationIndicator_,
                            ValidationState::kValid);
    keyFileValidationLabel_->hide();
  } else {
    setFieldValidationState(keyFileEdit_, keyFileValidationIndicator_,
                            ValidationState::kInvalid, "Key file not found");
    keyFileValidationLabel_->setText("Key file not found");
    keyFileValidationLabel_->show();
    allValid = false;
  }

  // Validate obfuscation seed file (optional, but check if path is set)
  QString seedPath = obfuscationSeedEdit_->text().trimmed();
  if (seedPath.isEmpty()) {
    setFieldValidationState(obfuscationSeedEdit_, obfuscationSeedValidationIndicator_,
                            ValidationState::kNeutral);
    obfuscationSeedValidationLabel_->hide();
  } else if (isValidFilePath(seedPath)) {
    setFieldValidationState(obfuscationSeedEdit_, obfuscationSeedValidationIndicator_,
                            ValidationState::kValid);
    obfuscationSeedValidationLabel_->hide();
  } else {
    setFieldValidationState(obfuscationSeedEdit_, obfuscationSeedValidationIndicator_,
                            ValidationState::kInvalid, "Seed file not found");
    obfuscationSeedValidationLabel_->setText("Seed file not found");
    obfuscationSeedValidationLabel_->show();
    allValid = false;
  }

  // Validate TUN IP address
  QString tunIp = tunIpAddressEdit_->text().trimmed();
  if (tunIp.isEmpty()) {
    setFieldValidationState(tunIpAddressEdit_, tunIpValidationIndicator_,
                            ValidationState::kNeutral);
    tunIpValidationLabel_->hide();
  } else if (isValidIpAddress(tunIp)) {
    setFieldValidationState(tunIpAddressEdit_, tunIpValidationIndicator_,
                            ValidationState::kValid);
    tunIpValidationLabel_->hide();
  } else {
    setFieldValidationState(tunIpAddressEdit_, tunIpValidationIndicator_,
                            ValidationState::kInvalid, "Invalid IP address format");
    tunIpValidationLabel_->setText("Invalid IP address format");
    tunIpValidationLabel_->show();
    allValid = false;
  }

  // Validate TUN netmask
  QString tunNetmask = tunNetmaskEdit_->text().trimmed();
  if (tunNetmask.isEmpty()) {
    setFieldValidationState(tunNetmaskEdit_, tunNetmaskValidationIndicator_,
                            ValidationState::kNeutral);
    tunNetmaskValidationLabel_->hide();
  } else if (isValidIpAddress(tunNetmask)) {
    setFieldValidationState(tunNetmaskEdit_, tunNetmaskValidationIndicator_,
                            ValidationState::kValid);
    tunNetmaskValidationLabel_->hide();
  } else {
    setFieldValidationState(tunNetmaskEdit_, tunNetmaskValidationIndicator_,
                            ValidationState::kInvalid, "Invalid netmask format");
    tunNetmaskValidationLabel_->setText("Invalid netmask format");
    tunNetmaskValidationLabel_->show();
    allValid = false;
  }

  // Update validation summary banner
  updateValidationSummary();

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
  enablePerAppRoutingCheck_->setChecked(settings.value("routing/enablePerAppRouting", false).toBool());

  // Load per-app routing settings
  if (appSplitTunnelWidget_ != nullptr) {
    appSplitTunnelWidget_->loadFromSettings();
  }

  // Startup Options
  startMinimizedCheck_->setChecked(settings.value("startup/startMinimized", false).toBool());
  autoConnectOnStartupCheck_->setChecked(settings.value("startup/autoConnect", false).toBool());

  // Check Windows registry for actual startup state
#ifdef _WIN32
  QSettings registrySettings(
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      QSettings::NativeFormat);
  bool inStartup = registrySettings.contains("VEIL VPN Client");
  launchOnWindowsStartupCheck_->setChecked(inStartup);
  // Update our settings to match registry
  settings.setValue("startup/launchOnWindowsStartup", inStartup);

  // Check if shortcuts already exist
  bool desktopShortcutExists = veil::windows::ShortcutManager::shortcutExists(
      veil::windows::ShortcutManager::Location::kDesktop, "VEIL VPN");
  bool startMenuShortcutExists = veil::windows::ShortcutManager::shortcutExists(
      veil::windows::ShortcutManager::Location::kStartMenu, "VEIL VPN");

  if (desktopShortcutExists) {
    desktopShortcutStatusLabel_->setText("✓ Already exists");
    desktopShortcutStatusLabel_->setStyleSheet("color: #3fb950; font-size: 12px;");
    createDesktopShortcutButton_->setEnabled(false);
  } else {
    desktopShortcutStatusLabel_->setText("");
    createDesktopShortcutButton_->setEnabled(true);
  }

  if (startMenuShortcutExists) {
    startMenuShortcutStatusLabel_->setText("✓ Already exists");
    startMenuShortcutStatusLabel_->setStyleSheet("color: #3fb950; font-size: 12px;");
    createStartMenuShortcutButton_->setEnabled(false);
  } else {
    startMenuShortcutStatusLabel_->setText("");
    createStartMenuShortcutButton_->setEnabled(true);
  }
#else
  launchOnWindowsStartupCheck_->setChecked(false);
  launchOnWindowsStartupCheck_->setEnabled(false);
#endif

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

  // Theme
  int themeValue = settings.value("ui/theme", static_cast<int>(Theme::kDark)).toInt();
  // Find and set the combo box index for the theme
  int themeIndex = themeCombo_->findData(themeValue);
  if (themeIndex >= 0) {
    themeCombo_->setCurrentIndex(themeIndex);
  }

  // Language
  QString languageCode = settings.value("ui/language", "en").toString();
  // Find and set the combo box index for the language
  int languageIndex = languageCombo_->findData(languageCode);
  if (languageIndex >= 0) {
    languageCombo_->setCurrentIndex(languageIndex);
  }

  // Notifications
  auto& notificationPrefs = NotificationPreferences::instance();
  notificationPrefs.load();
  notificationsEnabledCheck_->setChecked(notificationPrefs.isNotificationsEnabled());
  notificationSoundCheck_->setChecked(notificationPrefs.isNotificationSoundEnabled());
  showNotificationDetailsCheck_->setChecked(notificationPrefs.isShowDetailsEnabled());
  connectionEstablishedCheck_->setChecked(notificationPrefs.isConnectionEstablishedEnabled());
  connectionLostCheck_->setChecked(notificationPrefs.isConnectionLostEnabled());
  minimizeToTrayCheck_->setChecked(notificationPrefs.isMinimizeToTrayEnabled());
  updatesAvailableCheck_->setChecked(notificationPrefs.isUpdatesAvailableEnabled());
  errorNotificationsCheck_->setChecked(notificationPrefs.isErrorNotificationsEnabled());

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

  // Startup Options
  settings.setValue("startup/startMinimized", startMinimizedCheck_->isChecked());
  settings.setValue("startup/autoConnect", autoConnectOnStartupCheck_->isChecked());
  settings.setValue("startup/launchOnWindowsStartup", launchOnWindowsStartupCheck_->isChecked());

  // Routing
  settings.setValue("routing/routeAllTraffic", routeAllTrafficCheck_->isChecked());
  settings.setValue("routing/splitTunnel", splitTunnelCheck_->isChecked());
  settings.setValue("routing/customRoutes", customRoutesEdit_->text().trimmed());
  settings.setValue("routing/enablePerAppRouting", enablePerAppRoutingCheck_->isChecked());

  // Save per-app routing settings
  if (appSplitTunnelWidget_ != nullptr) {
    appSplitTunnelWidget_->saveToSettings();
  }

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

  // Theme
  settings.setValue("ui/theme", themeCombo_->currentData().toInt());

  // Language
  QString currentLanguage = settings.value("ui/language", "en").toString();
  QString newLanguage = languageCombo_->currentData().toString();
  settings.setValue("ui/language", newLanguage);

  // Emit signal if language changed
  if (currentLanguage != newLanguage) {
    emit languageChanged(newLanguage);
  }

  // Notifications
  auto& notificationPrefs = NotificationPreferences::instance();
  notificationPrefs.setNotificationsEnabled(notificationsEnabledCheck_->isChecked());
  notificationPrefs.setNotificationSoundEnabled(notificationSoundCheck_->isChecked());
  notificationPrefs.setShowDetailsEnabled(showNotificationDetailsCheck_->isChecked());
  notificationPrefs.setConnectionEstablishedEnabled(connectionEstablishedCheck_->isChecked());
  notificationPrefs.setConnectionLostEnabled(connectionLostCheck_->isChecked());
  notificationPrefs.setMinimizeToTrayEnabled(minimizeToTrayCheck_->isChecked());
  notificationPrefs.setUpdatesAvailableEnabled(updatesAvailableCheck_->isChecked());
  notificationPrefs.setErrorNotificationsEnabled(errorNotificationsCheck_->isChecked());
  notificationPrefs.save();

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

void SettingsWidget::onValidationDebounceTimeout() {
  validateSettings();
}

void SettingsWidget::setFieldValidationState(QLineEdit* field, QLabel* indicator,
                                              ValidationState state, const QString& message) {
  switch (state) {
    case ValidationState::kValid:
      indicator->setText("✓");
      indicator->setStyleSheet(QString("font-size: 18px; color: %1; font-weight: bold;")
                               .arg(colors::dark::kAccentSuccess));
      indicator->setToolTip("Valid");
      field->setStyleSheet("");
      break;
    case ValidationState::kInvalid:
      indicator->setText("✗");
      indicator->setStyleSheet(QString("font-size: 18px; color: %1; font-weight: bold;")
                               .arg(colors::dark::kAccentError));
      indicator->setToolTip(message.isEmpty() ? "Invalid" : message);
      field->setStyleSheet(QString("border-color: %1;").arg(colors::dark::kAccentError));
      break;
    case ValidationState::kNeutral:
      indicator->setText("");
      indicator->setStyleSheet("");
      indicator->setToolTip("");
      field->setStyleSheet("");
      break;
  }
}

void SettingsWidget::updateValidationSummary() {
  QStringList errorFields;

  // Check each field for errors
  QString address = serverAddressEdit_->text().trimmed();
  if (!address.isEmpty() && !isValidHostname(address) && !isValidIpAddress(address)) {
    errorFields.append("Server Address");
  }

  QString keyPath = keyFileEdit_->text().trimmed();
  if (!keyPath.isEmpty() && !isValidFilePath(keyPath)) {
    errorFields.append("Key File");
  }

  QString seedPath = obfuscationSeedEdit_->text().trimmed();
  if (!seedPath.isEmpty() && !isValidFilePath(seedPath)) {
    errorFields.append("Obfuscation Seed");
  }

  QString tunIp = tunIpAddressEdit_->text().trimmed();
  if (!tunIp.isEmpty() && !isValidIpAddress(tunIp)) {
    errorFields.append("TUN IP Address");
  }

  QString tunNetmask = tunNetmaskEdit_->text().trimmed();
  if (!tunNetmask.isEmpty() && !isValidIpAddress(tunNetmask)) {
    errorFields.append("TUN Netmask");
  }

  // Update banner
  if (errorFields.isEmpty()) {
    validationSummaryBanner_->hide();
  } else {
    QString message = QString("⚠ %1 field%2 need%3 attention: %4")
                          .arg(errorFields.size())
                          .arg(errorFields.size() > 1 ? "s" : "")
                          .arg(errorFields.size() > 1 ? "" : "s")
                          .arg(errorFields.join(", "));
    validationSummaryBanner_->setText(message);
    validationSummaryBanner_->show();
  }
}

void SettingsWidget::onAdvancedModeToggled(bool showAdvanced) {
  // Show/hide advanced sections based on toggle
  // Advanced sections: TUN Interface, DPI Bypass, Advanced
  tunInterfaceSection_->setVisible(showAdvanced);
  dpiBypassSection_->setVisible(showAdvanced);
  advancedSection_->setVisible(showAdvanced);
}

// NOLINTEND(readability-implicit-bool-conversion)

}  // namespace veil::gui
