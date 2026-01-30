#pragma once

#include <QString>
#include <QColor>

#ifdef _WIN32
#include <QSettings>
#endif

namespace veil::gui {

/// Theme selection options
enum class Theme {
  kDark,    // Dark theme
  kLight,   // Light theme
  kSystem   // Follow system theme (Windows dark mode setting)
};

/// Color palette as defined in client_ui_design.md
namespace colors {
namespace dark {
// Background colors
constexpr const char* kBackgroundPrimary = "#0d1117";
constexpr const char* kBackgroundSecondary = "#161b22";
constexpr const char* kBackgroundTertiary = "#21262d";
constexpr const char* kBackgroundCard = "rgba(255, 255, 255, 0.03)";

// Text colors
constexpr const char* kTextPrimary = "#f0f6fc";
constexpr const char* kTextSecondary = "#8b949e";
constexpr const char* kTextTertiary = "#6e7681";

// Accent colors - vibrant cyan/teal palette
constexpr const char* kAccentPrimary = "#58a6ff";
constexpr const char* kAccentSecondary = "#1f6feb";
constexpr const char* kAccentSuccess = "#3fb950";
constexpr const char* kAccentWarning = "#d29922";
constexpr const char* kAccentError = "#f85149";

// Glassmorphism - enhanced with better blur support
constexpr const char* kGlassOverlay = "rgba(255, 255, 255, 0.04)";
constexpr const char* kGlassBorder = "rgba(255, 255, 255, 0.08)";
constexpr const char* kGlassHighlight = "rgba(255, 255, 255, 0.12)";
constexpr const char* kShadow = "rgba(0, 0, 0, 0.4)";
constexpr const char* kShadowLight = "rgba(0, 0, 0, 0.2)";

// Glow effects for connected state
constexpr const char* kGlowSuccess = "rgba(63, 185, 80, 0.4)";
constexpr const char* kGlowPrimary = "rgba(88, 166, 255, 0.4)";
constexpr const char* kGlowError = "rgba(248, 81, 73, 0.4)";
}  // namespace dark

namespace light {
// Background colors
constexpr const char* kBackgroundPrimary = "#f8f9fa";
constexpr const char* kBackgroundSecondary = "#e9ecef";
constexpr const char* kBackgroundTertiary = "#ffffff";

// Text colors
constexpr const char* kTextPrimary = "#212529";
constexpr const char* kTextSecondary = "#6c757d";
constexpr const char* kTextTertiary = "#adb5bd";

// Accent colors
constexpr const char* kAccentPrimary = "#0d6efd";
constexpr const char* kAccentSuccess = "#20c997";
constexpr const char* kAccentWarning = "#fd7e14";
constexpr const char* kAccentError = "#dc3545";
}  // namespace light
}  // namespace colors

/// Font settings
namespace fonts {
constexpr const char* kFontFamily = "'Inter', 'SF Pro Display', -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif";
constexpr const char* kFontFamilyMono = "'JetBrains Mono', 'Fira Code', 'SF Mono', 'Consolas', monospace";

constexpr int kFontSizeHero = 42;
constexpr int kFontSizeHeadline = 28;
constexpr int kFontSizeTitle = 20;
constexpr int kFontSizeLarge = 17;
constexpr int kFontSizeBody = 15;
constexpr int kFontSizeCaption = 13;
constexpr int kFontSizeSmall = 11;
constexpr int kFontSizeMono = 13;
}  // namespace fonts

/// Animation durations in milliseconds
namespace animations {
constexpr int kDurationInstant = 100;
constexpr int kDurationFast = 150;
constexpr int kDurationNormal = 250;
constexpr int kDurationSlow = 350;
constexpr int kDurationPulse = 2000;
constexpr int kDurationGlow = 3000;
}  // namespace animations

/// Spacing and sizing
namespace spacing {
constexpr int kPaddingTiny = 4;
constexpr int kPaddingSmall = 8;
constexpr int kPaddingMedium = 16;
constexpr int kPaddingLarge = 24;
constexpr int kPaddingXLarge = 32;
constexpr int kPaddingXXLarge = 48;

constexpr int kBorderRadiusSmall = 8;
constexpr int kBorderRadiusMedium = 12;
constexpr int kBorderRadiusLarge = 16;
constexpr int kBorderRadiusXLarge = 24;
constexpr int kBorderRadiusRound = 9999;

// Component sizes
constexpr int kButtonHeight = 56;
constexpr int kButtonHeightSmall = 40;
constexpr int kIconSize = 24;
constexpr int kIconSizeLarge = 32;
constexpr int kStatusIndicatorSize = 120;
}  // namespace spacing

/// Returns the complete dark theme stylesheet
inline QString getDarkThemeStylesheet() {
  return R"(
    /* === Global Focus Indicator === */
    *:focus {
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }

    /* === Main Window & Containers === */
    QMainWindow, QWidget {
      background-color: #0d1117;
      color: #f0f6fc;
      font-family: 'Inter', 'SF Pro Display', -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif;
      font-size: 15px;
    }

    /* === Menu Bar === */
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
      background-color: #58a6ff;
      color: white;
    }

    QMenu::separator {
      height: 1px;
      background-color: rgba(255, 255, 255, 0.06);
      margin: 8px 12px;
    }

    /* === Primary Buttons (Gradient) === */
    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #238636, stop:1 #2ea043);
      border: none;
      border-radius: 12px;
      padding: 16px 32px;
      color: white;
      font-size: 16px;
      font-weight: 600;
      letter-spacing: 0.3px;
    }

    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2ea043, stop:1 #3fb950);
    }

    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #1a7f37, stop:1 #238636);
    }

    QPushButton:focus {
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }

    QPushButton:disabled {
      background: #21262d;
      color: #484f58;
    }

    /* Secondary Buttons */
    QPushButton[buttonStyle="secondary"] {
      background: #21262d;
      border: 1px solid rgba(255, 255, 255, 0.1);
      color: #f0f6fc;
    }

    QPushButton[buttonStyle="secondary"]:hover {
      background: #30363d;
      border-color: rgba(255, 255, 255, 0.15);
    }

    /* Ghost Buttons */
    QPushButton[buttonStyle="ghost"] {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: #8b949e;
    }

    QPushButton[buttonStyle="ghost"]:hover {
      background: rgba(255, 255, 255, 0.04);
      border-color: rgba(255, 255, 255, 0.2);
      color: #f0f6fc;
    }

    /* Danger Buttons */
    QPushButton[buttonStyle="danger"] {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #da3633, stop:1 #f85149);
    }

    QPushButton[buttonStyle="danger"]:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #f85149, stop:1 #ff6b6b);
    }

    QPushButton[buttonStyle="danger"]:pressed {
      background: #b62324;
    }

    /* === Group Boxes (Glassmorphism Cards) === */
    QGroupBox {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
      margin-top: 20px;
      padding: 24px 20px 20px 20px;
      font-size: 15px;
      font-weight: 500;
    }

    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 20px;
      padding: 0 12px;
      color: #8b949e;
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 1.5px;
    }

    /* === Form Inputs === */
    QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
      padding: 12px 16px;
      color: #f0f6fc;
      font-size: 14px;
      selection-background-color: #58a6ff;
    }

    QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
      border-color: #58a6ff;
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }

    QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
      background-color: #0d1117;
      color: #484f58;
    }

    QComboBox::drop-down {
      border: none;
      width: 36px;
    }

    QComboBox::down-arrow {
      image: none;
      border-left: 5px solid transparent;
      border-right: 5px solid transparent;
      border-top: 6px solid #8b949e;
      margin-right: 12px;
    }

    QComboBox QAbstractItemView {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
      padding: 8px;
      selection-background-color: #58a6ff;
    }

    /* === Checkboxes === */
    QCheckBox {
      spacing: 12px;
      color: #f0f6fc;
      font-size: 14px;
    }

    QCheckBox::indicator {
      width: 22px;
      height: 22px;
      border: 2px solid rgba(255, 255, 255, 0.15);
      border-radius: 6px;
      background-color: #161b22;
    }

    QCheckBox::indicator:checked {
      background-color: #238636;
      border-color: #238636;
    }

    QCheckBox::indicator:hover {
      border-color: #58a6ff;
    }

    QCheckBox:focus {
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }

    /* === Labels === */
    QLabel {
      color: #f0f6fc;
    }

    QLabel[textStyle="secondary"] {
      color: #8b949e;
    }

    QLabel[textStyle="caption"] {
      color: #6e7681;
      font-size: 13px;
    }

    QLabel[textStyle="mono"] {
      font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', 'Consolas', monospace;
      font-size: 13px;
      color: #79c0ff;
    }

    /* === Tables === */
    QTableWidget {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      gridline-color: rgba(255, 255, 255, 0.04);
    }

    QTableWidget::item {
      padding: 10px;
      color: #f0f6fc;
    }

    QTableWidget::item:selected {
      background-color: rgba(88, 166, 255, 0.2);
    }

    QHeaderView::section {
      background-color: #0d1117;
      color: #8b949e;
      padding: 14px 10px;
      border: none;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      font-weight: 600;
      text-transform: uppercase;
      font-size: 11px;
      letter-spacing: 1.5px;
    }

    /* === Text Edit / Log === */
    QTextEdit, QPlainTextEdit {
      background-color: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      padding: 16px;
      color: #f0f6fc;
      font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', 'Consolas', monospace;
      font-size: 13px;
      selection-background-color: #58a6ff;
      line-height: 1.5;
    }

    /* === Scroll Bars (Minimal) === */
    QScrollBar:vertical {
      background-color: transparent;
      width: 10px;
      margin: 4px 2px;
      border-radius: 5px;
    }

    QScrollBar::handle:vertical {
      background-color: rgba(255, 255, 255, 0.1);
      border-radius: 5px;
      min-height: 40px;
    }

    QScrollBar::handle:vertical:hover {
      background-color: rgba(255, 255, 255, 0.2);
    }

    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
      background: none;
      height: 0;
    }

    QScrollBar:horizontal {
      background-color: transparent;
      height: 10px;
      margin: 2px 4px;
      border-radius: 5px;
    }

    QScrollBar::handle:horizontal {
      background-color: rgba(255, 255, 255, 0.1);
      border-radius: 5px;
      min-width: 40px;
    }

    QScrollBar::handle:horizontal:hover {
      background-color: rgba(255, 255, 255, 0.2);
    }

    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
    QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
      background: none;
      width: 0;
    }

    /* === Tooltips === */
    QToolTip {
      background-color: #21262d;
      color: #f0f6fc;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      padding: 10px 14px;
      font-size: 13px;
    }

    /* === Progress Bar === */
    QProgressBar {
      background-color: #21262d;
      border: none;
      border-radius: 6px;
      height: 10px;
      text-align: center;
    }

    QProgressBar::chunk {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                  stop:0 #238636, stop:1 #3fb950);
      border-radius: 6px;
    }

    /* === Sliders === */
    QSlider::groove:horizontal {
      background-color: #21262d;
      height: 8px;
      border-radius: 4px;
    }

    QSlider::handle:horizontal {
      background-color: #58a6ff;
      width: 20px;
      height: 20px;
      margin: -6px 0;
      border-radius: 10px;
    }

    QSlider::handle:horizontal:hover {
      background-color: #79c0ff;
    }

    QSlider::sub-page:horizontal {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                  stop:0 #1f6feb, stop:1 #58a6ff);
      border-radius: 4px;
    }

    /* === Tab Widget === */
    QTabWidget::pane {
      background-color: #0d1117;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      padding: 12px;
    }

    QTabBar::tab {
      background-color: #161b22;
      color: #8b949e;
      padding: 12px 24px;
      margin-right: 4px;
      border-top-left-radius: 10px;
      border-top-right-radius: 10px;
    }

    QTabBar::tab:selected {
      background-color: #238636;
      color: white;
    }

    QTabBar::tab:hover:!selected {
      background-color: #21262d;
      color: #f0f6fc;
    }

    QTabBar::tab:focus {
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }

    QMenuBar::item:focus {
      outline: 2px solid #58a6ff;
      outline-offset: 2px;
    }
  )";
}

/// Returns the light theme stylesheet
inline QString getLightThemeStylesheet() {
  return R"(
    QMainWindow, QWidget {
      background-color: #f8f9fa;
      color: #212529;
      font-family: -apple-system, BlinkMacSystemFont, 'Inter', 'Segoe UI', system-ui, sans-serif;
      font-size: 15px;
    }

    QMenuBar {
      background-color: #ffffff;
      color: #212529;
      border-bottom: 1px solid #e9ecef;
    }

    QMenuBar::item:selected {
      background-color: #e9ecef;
    }

    QPushButton {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #0d6efd, stop:1 #20c997);
      border: none;
      border-radius: 12px;
      padding: 16px 32px;
      color: white;
      font-size: 16px;
      font-weight: 600;
    }

    QGroupBox {
      background-color: #ffffff;
      border: 1px solid #e9ecef;
      border-radius: 16px;
    }

    QGroupBox::title {
      color: #6c757d;
    }

    QLineEdit, QSpinBox, QComboBox {
      background-color: #ffffff;
      border: 1px solid #ced4da;
      border-radius: 8px;
      padding: 10px 12px;
      color: #212529;
    }

    QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
      border-color: #0d6efd;
    }
  )";
}

/// Detects if the system is using dark mode (Windows only)
inline bool isSystemDarkMode() {
#ifdef _WIN32
  QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     QSettings::NativeFormat);
  // AppsUseLightTheme == 0 means dark mode is enabled
  return settings.value("AppsUseLightTheme", 1).toInt() == 0;
#else
  // Default to dark mode on non-Windows platforms
  return true;
#endif
}

/// Resolves the effective theme based on the theme setting
inline Theme resolveTheme(Theme theme) {
  if (theme == Theme::kSystem) {
    return isSystemDarkMode() ? Theme::kDark : Theme::kLight;
  }
  return theme;
}

/// Gets the stylesheet for a specific theme
inline QString getThemeStylesheet(Theme theme) {
  Theme effectiveTheme = resolveTheme(theme);
  return (effectiveTheme == Theme::kDark) ? getDarkThemeStylesheet() : getLightThemeStylesheet();
}

}  // namespace veil::gui
