#include "notification_history_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

#include "common/gui/theme.h"
#include "notification_preferences.h"

namespace veil::gui {

NotificationHistoryDialog::NotificationHistoryDialog(QWidget* parent)
    : QDialog(parent) {
  setupUi();
  refreshHistory();
}

void NotificationHistoryDialog::setupUi() {
  setWindowTitle("Notification History");
  setModal(true);
  setMinimumSize(600, 400);

  // Apply dark theme styling
  setStyleSheet(QString(R"(
    QDialog {
      background-color: %1;
      color: %2;
    }
    QLabel {
      color: %2;
    }
    QListWidget {
      background-color: %3;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
      padding: 8px;
      color: %2;
      font-size: 13px;
    }
    QListWidget::item {
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      padding: 12px;
      margin: 2px 0;
    }
    QListWidget::item:hover {
      background: rgba(255, 255, 255, 0.05);
      border-radius: 6px;
    }
    QPushButton {
      border: none;
      border-radius: 6px;
      padding: 10px 20px;
      font-weight: 600;
      font-size: 13px;
    }
    QPushButton#clearBtn {
      background: %4;
      color: white;
    }
    QPushButton#clearBtn:hover {
      background: %5;
    }
    QPushButton#closeBtn {
      background: rgba(255, 255, 255, 0.08);
      color: %6;
    }
    QPushButton#closeBtn:hover {
      background: rgba(255, 255, 255, 0.12);
    }
  )").arg(colors::dark::kBackgroundPrimary,
          colors::dark::kTextPrimary,
          colors::dark::kBackgroundSecondary,
          colors::dark::kAccentError,
          "#f85149",
          colors::dark::kTextSecondary));

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(24, 24, 24, 24);

  // Title
  auto* titleLabel = new QLabel("Notification History", this);
  titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: 700; color: %2;")
                                .arg(fonts::kFontSizeHeadline())
                                .arg(colors::dark::kTextPrimary));
  mainLayout->addWidget(titleLabel);

  // Description
  auto* descLabel = new QLabel("Recent notifications from VEIL VPN", this);
  descLabel->setStyleSheet(QString("font-size: 13px; color: %1; margin-bottom: 8px;")
                               .arg(colors::dark::kTextSecondary));
  mainLayout->addWidget(descLabel);

  // History list
  historyList_ = new QListWidget(this);
  mainLayout->addWidget(historyList_, 1);

  // Buttons
  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(12);

  clearButton_ = new QPushButton("Clear History", this);
  clearButton_->setObjectName("clearBtn");
  clearButton_->setToolTip("Delete all notification history");
  connect(clearButton_, &QPushButton::clicked, this, &NotificationHistoryDialog::onClearHistory);
  buttonLayout->addWidget(clearButton_);

  buttonLayout->addStretch();

  closeButton_ = new QPushButton("Close", this);
  closeButton_->setObjectName("closeBtn");
  connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton_);

  mainLayout->addLayout(buttonLayout);
}

void NotificationHistoryDialog::refreshHistory() {
  historyList_->clear();

  auto& prefs = NotificationPreferences::instance();
  const auto& history = prefs.getHistory();

  if (history.isEmpty()) {
    auto* item = new QListWidgetItem("No notifications yet");
    item->setForeground(QColor(colors::dark::kTextSecondary));
    historyList_->addItem(item);
    clearButton_->setEnabled(false);
    return;
  }

  clearButton_->setEnabled(true);

  for (const auto& event : history) {
    QString itemText = QString("%1\n%2: %3")
                           .arg(event.timestamp.toString("MMM dd, yyyy hh:mm:ss"),
                                event.title,
                                event.message);

    auto* item = new QListWidgetItem(itemText);

    // Color code by event type
    if (event.eventType == "error") {
      item->setForeground(QColor(colors::dark::kAccentError));
    } else if (event.eventType == "connection_established") {
      item->setForeground(QColor(colors::dark::kAccentSuccess));
    } else if (event.eventType == "connection_lost") {
      item->setForeground(QColor(colors::dark::kAccentWarning));
    } else if (event.eventType == "update") {
      item->setForeground(QColor(colors::dark::kAccentPrimary));
    } else {
      item->setForeground(QColor(colors::dark::kTextPrimary));
    }

    historyList_->addItem(item);
  }
}

void NotificationHistoryDialog::onClearHistory() {
  auto reply = QMessageBox::question(
      this,
      "Clear History",
      "Are you sure you want to clear all notification history?",
      QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    auto& prefs = NotificationPreferences::instance();
    prefs.clearHistory();
    refreshHistory();
  }
}

}  // namespace veil::gui
