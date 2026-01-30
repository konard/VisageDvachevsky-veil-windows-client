#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>

namespace veil::gui {

/// Validation state for input fields
enum class ValidationState {
  kNeutral,  // Not yet validated (gray)
  kValid,    // Passed validation (green checkmark)
  kInvalid   // Failed validation (red X)
};

/// Settings widget for configuring VEIL VPN client options
class SettingsWidget : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsWidget(QWidget* parent = nullptr);

 signals:
  void backRequested();
  void settingsSaved();

 public slots:
  /// Load settings from config file
  void loadSettings();

  /// Save current settings
  void saveSettings();

  /// Get current settings values
  QString serverAddress() const;
  uint16_t serverPort() const;
  QString keyFilePath() const;
  QString obfuscationSeedPath() const;

 private slots:
  void onServerAddressChanged();
  void onPortChanged();
  void onDpiModeChanged(int index);
  void onBrowseKeyFile();
  void onBrowseObfuscationSeed();
  void validateSettings();
  void onValidationDebounceTimeout();

 private:
  void setupUi();
  void createServerSection(QWidget* parent);
  void createCryptoSection(QWidget* parent);
  void createRoutingSection(QWidget* parent);
  void createConnectionSection(QWidget* parent);
  void createDpiBypassSection(QWidget* parent);
  void createTunInterfaceSection(QWidget* parent);
  void createAdvancedSection(QWidget* parent);

  bool isValidHostname(const QString& hostname) const;
  bool isValidIpAddress(const QString& ip) const;
  bool isValidFilePath(const QString& path) const;

  void setFieldValidationState(QLineEdit* field, QLabel* indicator,
                                ValidationState state, const QString& message = "");
  void updateValidationSummary();

  // Validation summary banner
  QLabel* validationSummaryBanner_;

  // Server Configuration
  QLineEdit* serverAddressEdit_;
  QSpinBox* portSpinBox_;
  QLabel* serverValidationLabel_;
  QLabel* serverValidationIndicator_;

  // Crypto Configuration
  QLineEdit* keyFileEdit_;
  QPushButton* browseKeyFileButton_;
  QLabel* keyFileValidationLabel_;
  QLabel* keyFileValidationIndicator_;
  QLineEdit* obfuscationSeedEdit_;
  QPushButton* browseObfuscationSeedButton_;
  QLabel* obfuscationSeedValidationLabel_;
  QLabel* obfuscationSeedValidationIndicator_;

  // Routing
  QCheckBox* routeAllTrafficCheck_;
  QCheckBox* splitTunnelCheck_;
  QLineEdit* customRoutesEdit_;

  // Connection
  QCheckBox* autoReconnectCheck_;
  QSpinBox* reconnectIntervalSpinBox_;
  QSpinBox* maxReconnectAttemptsSpinBox_;

  // DPI Bypass
  QComboBox* dpiModeCombo_;
  QLabel* dpiDescLabel_;

  // TUN Interface
  QLineEdit* tunDeviceNameEdit_;
  QLineEdit* tunIpAddressEdit_;
  QLineEdit* tunNetmaskEdit_;
  QSpinBox* tunMtuSpinBox_;
  QLabel* tunIpValidationLabel_;
  QLabel* tunIpValidationIndicator_;
  QLabel* tunNetmaskValidationLabel_;
  QLabel* tunNetmaskValidationIndicator_;

  // Advanced
  QCheckBox* obfuscationCheck_;
  QCheckBox* verboseLoggingCheck_;
  QCheckBox* developerModeCheck_;

  // Buttons
  QPushButton* saveButton_;
  QPushButton* resetButton_;

  // Validation debounce timer
  QTimer* validationDebounceTimer_;

  // State
  bool hasUnsavedChanges_{false};
};

}  // namespace veil::gui
