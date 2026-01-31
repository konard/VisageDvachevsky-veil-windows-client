#include "collapsible_section.h"

#include <QVBoxLayout>
#include <QLabel>

#include "common/gui/theme.h"

namespace veil::gui {

// NOLINTBEGIN(readability-implicit-bool-conversion)

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent) {
  setupUi();
  setTitle(title);
}

void CollapsibleSection::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Toggle button
  toggleButton_ = new QPushButton(this);
  toggleButton_->setCursor(Qt::PointingHandCursor);
  toggleButton_->setStyleSheet(QString(R"(
    QPushButton {
      background: transparent;
      border: none;
      color: %1;
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      padding: 12px 16px;
      text-align: left;
    }
    QPushButton:hover {
      color: %2;
      background: rgba(255, 255, 255, 0.02);
    }
    QPushButton:focus {
      outline: 2px solid %3;
      outline-offset: 2px;
      border-radius: 8px;
    }
  )").arg(colors::dark::kTextSecondary,
          colors::dark::kTextPrimary,
          colors::dark::kAccentPrimary));

  connect(toggleButton_, &QPushButton::clicked, this, &CollapsibleSection::onToggleClicked);
  mainLayout->addWidget(toggleButton_);

  // Content container with animation
  contentContainer_ = new QFrame(this);
  contentContainer_->setFrameShape(QFrame::NoFrame);
  contentContainer_->setStyleSheet("QFrame { background: transparent; border: none; }");

  contentLayout_ = new QVBoxLayout(contentContainer_);
  contentLayout_->setSpacing(0);
  contentLayout_->setContentsMargins(0, 0, 0, 0);

  mainLayout->addWidget(contentContainer_);

  // Setup animation
  animation_ = new QPropertyAnimation(this, "contentHeight", this);
  animation_->setDuration(animations::kDurationNormal);
  animation_->setEasingCurve(QEasingCurve::InOutCubic);

  updateArrowIcon();
}

void CollapsibleSection::setContent(QWidget* content) {
  if (contentWidget_ != nullptr) {
    contentLayout_->removeWidget(contentWidget_);
  }

  contentWidget_ = content;
  if (contentWidget_ != nullptr) {
    contentLayout_->addWidget(contentWidget_);
    contentWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  }

  // Update expanded height
  if (!collapsed_) {
    contentContainer_->adjustSize();
    expandedHeight_ = contentWidget_ != nullptr ? contentWidget_->sizeHint().height() : 0;
  }
}

void CollapsibleSection::setCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }

  collapsed_ = collapsed;

  if (contentWidget_ != nullptr) {
    // Store expanded height before collapsing
    if (collapsed) {
      expandedHeight_ = contentContainer_->height();
      animation_->setStartValue(expandedHeight_);
      animation_->setEndValue(0);
    } else {
      // Calculate target height
      contentWidget_->adjustSize();
      expandedHeight_ = contentWidget_->sizeHint().height();
      animation_->setStartValue(0);
      animation_->setEndValue(expandedHeight_);
    }

    animation_->start();
  }

  updateArrowIcon();
  emit toggled(collapsed_);
}

void CollapsibleSection::setCollapsedImmediate(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }

  collapsed_ = collapsed;

  if (contentWidget_ != nullptr) {
    if (collapsed) {
      contentContainer_->setMaximumHeight(0);
      contentWidget_->hide();
    } else {
      contentContainer_->setMaximumHeight(QWIDGETSIZE_MAX);
      contentWidget_->show();
    }
  }

  updateArrowIcon();
  emit toggled(collapsed_);
}

void CollapsibleSection::setTitle(const QString& title) {
  toggleButton_->setText(QString("▼  %1").arg(title));
  updateArrowIcon();
}

QString CollapsibleSection::title() const {
  QString text = toggleButton_->text();
  // Remove arrow prefix
  return text.mid(3).trimmed();
}

void CollapsibleSection::onToggleClicked() {
  setCollapsed(!collapsed_);
}

void CollapsibleSection::updateArrowIcon() {
  QString text = toggleButton_->text();
  QString titleText = text.mid(3).trimmed();

  if (collapsed_) {
    toggleButton_->setText(QString("▶  %1").arg(titleText));
  } else {
    toggleButton_->setText(QString("▼  %1").arg(titleText));
  }
}

int CollapsibleSection::contentHeight() const {
  return contentContainer_->maximumHeight();
}

void CollapsibleSection::setContentHeight(int height) {
  contentContainer_->setMaximumHeight(height);

  // Show/hide content widget based on height
  if (contentWidget_ != nullptr) {
    if (height <= 0) {
      contentWidget_->hide();
    } else {
      contentWidget_->show();
    }
  }
}

// NOLINTEND(readability-implicit-bool-conversion)

}  // namespace veil::gui
