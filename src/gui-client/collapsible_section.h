#pragma once

#include <QWidget>
#include <QPushButton>
#include <QFrame>
#include <QVBoxLayout>
#include <QPropertyAnimation>

namespace veil::gui {

/// A collapsible section widget that can show/hide its content
///
/// This widget provides a toggle button with an arrow indicator that
/// expands/collapses the content widget. It uses smooth animations for
/// expanding and collapsing transitions.
class CollapsibleSection : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)

 public:
  explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

  /// Set the content widget to be shown/hidden
  void setContent(QWidget* content);

  /// Get the content widget
  QWidget* content() const { return contentWidget_; }

  /// Check if section is currently collapsed
  bool isCollapsed() const { return collapsed_; }

  /// Set collapsed state (with animation)
  void setCollapsed(bool collapsed);

  /// Set collapsed state (without animation)
  void setCollapsedImmediate(bool collapsed);

  /// Set the title text
  void setTitle(const QString& title);

  /// Get the title text
  QString title() const;

 signals:
  /// Emitted when the section is expanded or collapsed
  void toggled(bool collapsed);

 private slots:
  void onToggleClicked();

 private:
  void setupUi();
  void updateArrowIcon();

  // Property accessors for animation
  int contentHeight() const;
  void setContentHeight(int height);

  QPushButton* toggleButton_;
  QFrame* contentContainer_;
  QWidget* contentWidget_;
  QVBoxLayout* contentLayout_;
  QPropertyAnimation* animation_;
  bool collapsed_;
  int expandedHeight_;
};

}  // namespace veil::gui
