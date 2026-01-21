#include "mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QApplication>
#include <QIcon>

#include "common/gui/theme.h"
#include "connection_widget.h"
#include "diagnostics_widget.h"
#include "ipc_client_manager.h"
#include "settings_widget.h"

namespace veil::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      stackedWidget_(new QStackedWidget(this)),
      connectionWidget_(new ConnectionWidget(this)),
      settingsWidget_(new SettingsWidget(this)),
      diagnosticsWidget_(new DiagnosticsWidget(this)),
      ipcManager_(std::make_unique<IpcClientManager>()) {
  setupUi();
  setupMenuBar();
  setupStatusBar();
  applyDarkTheme();

  // Connect to daemon
  ipcManager_->connectToDaemon();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
  setWindowTitle("VEIL VPN Client");
  setMinimumSize(480, 720);
  resize(480, 720);

  // Set window icon from embedded resources
  setWindowIcon(QIcon(":/icons/icon_disconnected.svg"));

  // Set window flags for modern appearance
  setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);

  // Add widgets to stacked widget
  stackedWidget_->addWidget(connectionWidget_);
  stackedWidget_->addWidget(settingsWidget_);
  stackedWidget_->addWidget(diagnosticsWidget_);

  // Set central widget
  setCentralWidget(stackedWidget_);

  // Connect signals
  connect(connectionWidget_, &ConnectionWidget::settingsRequested,
          this, &MainWindow::showSettingsView);
  connect(settingsWidget_, &SettingsWidget::backRequested,
          this, &MainWindow::showConnectionView);
  connect(diagnosticsWidget_, &DiagnosticsWidget::backRequested,
          this, &MainWindow::showConnectionView);
}

void MainWindow::setupMenuBar() {
  // Set menu bar style with new color scheme
  menuBar()->setStyleSheet(R"(
    QMenuBar {
      background-color: #0d1117;
      color: #f0f6fc;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      padding: 6px 12px;
    }
    QMenuBar::item {
      padding: 8px 16px;
      border-radius: 6px;
      margin: 2px;
    }
    QMenuBar::item:selected {
      background-color: rgba(255, 255, 255, 0.08);
    }
    QMenu {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 12px;
      padding: 8px;
    }
    QMenu::item {
      padding: 10px 24px;
      border-radius: 8px;
      margin: 2px 0;
    }
    QMenu::item:selected {
      background-color: #238636;
      color: white;
    }
    QMenu::separator {
      height: 1px;
      background-color: rgba(255, 255, 255, 0.06);
      margin: 8px 12px;
    }
  )");

  auto* viewMenu = menuBar()->addMenu(tr("&View"));
  viewMenu->addAction(tr("&Connection"), this, &MainWindow::showConnectionView);
  viewMenu->addAction(tr("&Settings"), this, &MainWindow::showSettingsView);
  viewMenu->addAction(tr("&Diagnostics"), this, &MainWindow::showDiagnosticsView);

  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(tr("&About VEIL"), this, &MainWindow::showAboutDialog);
  helpMenu->addAction(tr("Check for &Updates"), []() {
    // TODO: Implement update check
  });
}

void MainWindow::setupStatusBar() {
  statusBar()->setStyleSheet(R"(
    QStatusBar {
      background-color: #0d1117;
      color: #8b949e;
      border-top: 1px solid rgba(255, 255, 255, 0.06);
      padding: 6px 12px;
      font-size: 12px;
    }
    QStatusBar::item {
      border: none;
    }
  )");

  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::applyDarkTheme() {
  // Apply comprehensive dark theme stylesheet from theme.h
  setStyleSheet(getDarkThemeStylesheet());

  // Additional window-specific styles
  QString windowStyle = R"(
    QMainWindow {
      background-color: #0d1117;
    }
    QStackedWidget {
      background-color: #0d1117;
    }
  )";

  // Append to existing stylesheet
  setStyleSheet(styleSheet() + windowStyle);
}

void MainWindow::showConnectionView() {
  stackedWidget_->setCurrentWidget(connectionWidget_);
  statusBar()->showMessage(tr("Connection"));
}

void MainWindow::showSettingsView() {
  stackedWidget_->setCurrentWidget(settingsWidget_);
  statusBar()->showMessage(tr("Settings"));
}

void MainWindow::showDiagnosticsView() {
  stackedWidget_->setCurrentWidget(diagnosticsWidget_);
  statusBar()->showMessage(tr("Diagnostics"));
}

void MainWindow::showAboutDialog() {
  // Create a modern about dialog
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(tr("About VEIL"));
  dialog->setModal(true);
  dialog->setFixedSize(420, 380);

  dialog->setStyleSheet(R"(
    QDialog {
      background-color: #0d1117;
      color: #f0f6fc;
    }
    QLabel {
      color: #f0f6fc;
    }
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 10px;
      padding: 12px 32px;
      color: white;
      font-weight: 600;
      font-size: 14px;
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }
  )");

  auto* layout = new QVBoxLayout(dialog);
  layout->setSpacing(20);
  layout->setContentsMargins(40, 40, 40, 40);

  // Logo placeholder
  auto* logoWidget = new QWidget(dialog);
  logoWidget->setFixedSize(64, 64);
  logoWidget->setStyleSheet(R"(
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #238636, stop:1 #3fb950);
    border-radius: 16px;
  )");
  layout->addWidget(logoWidget, 0, Qt::AlignCenter);

  auto* titleLabel = new QLabel("VEIL VPN", dialog);
  titleLabel->setStyleSheet("font-size: 28px; font-weight: 700; color: #f0f6fc; letter-spacing: 2px;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  auto* versionLabel = new QLabel("Version 1.0.0", dialog);
  versionLabel->setStyleSheet(R"(
    color: #8b949e;
    font-size: 14px;
    padding: 4px 16px;
    background: rgba(255, 255, 255, 0.04);
    border-radius: 12px;
  )");
  versionLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(versionLabel, 0, Qt::AlignCenter);

  layout->addSpacing(8);

  auto* descLabel = new QLabel(
      "A secure UDP-based VPN client with\n"
      "DPI evasion capabilities.\n\n"
      "Modern cryptography (X25519, ChaCha20-Poly1305)\n"
      "Advanced traffic morphing techniques",
      dialog);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("color: #8b949e; font-size: 14px; line-height: 1.6;");
  descLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(descLabel);

  layout->addStretch();

  auto* closeButton = new QPushButton(tr("Close"), dialog);
  connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeButton, 0, Qt::AlignCenter);

  dialog->exec();
  dialog->deleteLater();
}

}  // namespace veil::gui
