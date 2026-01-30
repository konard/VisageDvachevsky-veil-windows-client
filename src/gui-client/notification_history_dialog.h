#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>

namespace veil::gui {

/// Dialog to display notification history
class NotificationHistoryDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NotificationHistoryDialog(QWidget* parent = nullptr);

 private slots:
  void onClearHistory();
  void refreshHistory();

 private:
  void setupUi();

  QListWidget* historyList_;
  QPushButton* clearButton_;
  QPushButton* closeButton_;
};

}  // namespace veil::gui
