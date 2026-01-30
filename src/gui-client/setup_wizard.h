#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

namespace veil::gui {

/// First-run setup wizard for new users
///
/// Provides a guided setup experience with the following steps:
///   0. Welcome screen with VEIL branding
///   1. Server configuration (address + port)
///   2. Key file setup (browse or generate)
///   3. Optional features configuration (DPI bypass, routing)
///   4. Finish with test connection option
///
/// Also supports configuration import from `.veil` config files.
class SetupWizard : public QWidget {
  Q_OBJECT

 public:
  explicit SetupWizard(QWidget* parent = nullptr);

  /// Check whether the wizard should be shown (first run)
  static bool isFirstRun();

  /// Mark the first-run flag as completed
  static void markFirstRunComplete();

  /// Reset the first-run flag so the wizard shows again
  static void resetFirstRun();

 signals:
  /// Emitted when the wizard is completed (user finished or skipped)
  void wizardCompleted();

  /// Emitted when the user clicks "Skip" to configure manually
  void wizardSkipped();

 private slots:
  void onNextClicked();
  void onBackClicked();
  void onSkipClicked();
  void onFinishClicked();
  void onImportConfig();
  void onBrowseKeyFile();
  void onTestConnection();

 private:
  void setupUi();

  // Page creation
  QWidget* createWelcomePage();
  QWidget* createServerPage();
  QWidget* createKeyFilePage();
  QWidget* createFeaturesPage();
  QWidget* createFinishPage();

  // Navigation
  void navigateToPage(int index);
  void updateNavigationButtons();
  void saveAllSettings();
  bool validateCurrentPage();
  bool importConfigFromFile(const QString& filePath);

  // Animated transition between pages
  void animatePageTransition(int fromIndex, int toIndex);

  // Pages
  QStackedWidget* pageStack_;
  int currentPageIndex_{0};
  static constexpr int kPageCount = 5;

  // Navigation buttons
  QPushButton* backButton_;
  QPushButton* nextButton_;
  QPushButton* skipButton_;

  // Step indicators
  QWidget* stepIndicatorContainer_;
  QList<QWidget*> stepDots_;
  QList<QLabel*> stepLabels_;

  // === Page 1: Server Configuration ===
  QLineEdit* serverAddressEdit_;
  QSpinBox* serverPortSpinBox_;
  QLabel* serverValidationLabel_;

  // === Page 2: Key File ===
  QLineEdit* keyFileEdit_;
  QPushButton* browseKeyFileButton_;
  QLabel* keyFileStatusLabel_;

  // === Page 3: Features ===
  QCheckBox* obfuscationCheck_;
  QComboBox* dpiModeCombo_;
  QCheckBox* routeAllTrafficCheck_;
  QCheckBox* autoReconnectCheck_;

  // === Page 4: Finish ===
  QPushButton* testConnectionButton_;
  QLabel* testResultLabel_;
  QLabel* configSummaryLabel_;

  // Animation state
  bool isAnimating_{false};
};

}  // namespace veil::gui
