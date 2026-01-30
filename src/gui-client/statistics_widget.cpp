#include "statistics_widget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

#include "common/gui/theme.h"

namespace veil::gui {

// ===================== MiniGraphWidget Implementation =====================

MiniGraphWidget::MiniGraphWidget(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(140);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MiniGraphWidget::setLabels(const QString& title, const QString& unit) {
  title_ = title;
  unit_ = unit;
  update();
}

void MiniGraphWidget::setLineColor(const QColor& color) {
  lineColor_ = color;
  update();
}

void MiniGraphWidget::setDualSeries(bool dual) {
  dualSeries_ = dual;
}

void MiniGraphWidget::setSecondLineColor(const QColor& color) {
  secondLineColor_ = color;
}

void MiniGraphWidget::addDataPoint(double value) {
  data_.push_back(value);
  while (static_cast<int>(data_.size()) > maxPoints_) {
    data_.pop_front();
  }
  update();
}

void MiniGraphWidget::addSecondDataPoint(double value) {
  secondData_.push_back(value);
  while (static_cast<int>(secondData_.size()) > maxPoints_) {
    secondData_.pop_front();
  }
  update();
}

void MiniGraphWidget::setMaxPoints(int max) {
  maxPoints_ = max;
}

void MiniGraphWidget::clear() {
  data_.clear();
  secondData_.clear();
  update();
}

void MiniGraphWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int w = width();
  const int h = height();
  const int headerH = 28;
  const int graphMarginLeft = 8;
  const int graphMarginRight = 8;
  const int graphMarginBottom = 4;

  // Background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(22, 27, 34, 200));  // kBackgroundSecondary with alpha
  p.drawRoundedRect(rect(), 12, 12);

  // Border
  p.setPen(QPen(QColor(255, 255, 255, 15), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);

  // Title
  p.setPen(QColor(139, 148, 158));  // kTextSecondary
  QFont titleFont;
  titleFont.setPixelSize(12);
  titleFont.setWeight(QFont::DemiBold);
  titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  p.setFont(titleFont);
  p.drawText(QRect(12, 4, w - 24, headerH), Qt::AlignLeft | Qt::AlignVCenter,
             title_.toUpper());

  // Unit label on right
  p.drawText(QRect(12, 4, w - 24, headerH), Qt::AlignRight | Qt::AlignVCenter,
             unit_);

  // Graph area
  const int gx = graphMarginLeft;
  const int gy = headerH;
  const int gw = w - graphMarginLeft - graphMarginRight;
  const int gh = h - headerH - graphMarginBottom;

  if (data_.empty() || gw <= 0 || gh <= 0) {
    // No data placeholder
    p.setPen(QColor(110, 118, 129, 100));
    QFont placeholderFont;
    placeholderFont.setPixelSize(13);
    p.setFont(placeholderFont);
    p.drawText(QRect(gx, gy, gw, gh), Qt::AlignCenter, "No data yet");
    return;
  }

  // Draw subtle horizontal grid lines
  p.setPen(QPen(QColor(255, 255, 255, 10), 1, Qt::DotLine));
  for (int i = 1; i <= 3; ++i) {
    int y = gy + gh * i / 4;
    p.drawLine(gx, y, gx + gw, y);
  }

  // Helper to draw a series
  auto drawSeries = [&](const std::deque<double>& series, const QColor& color) {
    if (series.size() < 2) return;

    // Find max value for scaling
    double maxVal = 1.0;
    for (double v : series) {
      if (v > maxVal) maxVal = v;
    }
    // Add 10% headroom
    maxVal *= 1.1;

    const int n = static_cast<int>(series.size());
    const double xStep = static_cast<double>(gw) / (maxPoints_ - 1);
    const int xOffset = (maxPoints_ - n);

    // Build path
    QPainterPath path;
    QPainterPath fillPath;

    for (int i = 0; i < n; ++i) {
      double x = gx + (xOffset + i) * xStep;
      double y = gy + gh - (series[static_cast<size_t>(i)] / maxVal) * gh;
      if (i == 0) {
        path.moveTo(x, y);
        fillPath.moveTo(x, gy + gh);
        fillPath.lineTo(x, y);
      } else {
        path.lineTo(x, y);
        fillPath.lineTo(x, y);
      }
    }

    // Fill under curve
    double lastX = gx + (xOffset + n - 1) * xStep;
    fillPath.lineTo(lastX, gy + gh);
    fillPath.closeSubpath();

    QLinearGradient fillGradient(0, gy, 0, gy + gh);
    QColor fillColor = color;
    fillColor.setAlpha(40);
    fillGradient.setColorAt(0, fillColor);
    fillGradient.setColorAt(1, QColor(fillColor.red(), fillColor.green(), fillColor.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(fillGradient);
    p.drawPath(fillPath);

    // Draw line
    p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Latest value label
    if (n > 0) {
      double latest = series.back();
      QString valStr;
      if (latest >= 1048576) {
        valStr = QString("%1 MB").arg(latest / 1048576.0, 0, 'f', 1);
      } else if (latest >= 1024) {
        valStr = QString("%1 KB").arg(latest / 1024.0, 0, 'f', 1);
      } else {
        valStr = QString("%1").arg(latest, 0, 'f', 0);
      }
      QFont valFont;
      valFont.setPixelSize(11);
      valFont.setWeight(QFont::Medium);
      p.setFont(valFont);
      p.setPen(color);
      double lx = gx + (xOffset + n - 1) * xStep;
      double ly = gy + gh - (latest / maxVal) * gh;
      p.drawText(QPointF(lx - 40, ly - 6), valStr);
    }
  };

  drawSeries(data_, lineColor_);
  if (dualSeries_ && !secondData_.empty()) {
    drawSeries(secondData_, secondLineColor_);
  }
}

// ===================== StatisticsWidget Implementation =====================

StatisticsWidget::StatisticsWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
}

void StatisticsWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge, spacing::kPaddingLarge,
                                  spacing::kPaddingXLarge, spacing::kPaddingLarge);

  // === Header ===
  auto* headerWidget = new QWidget(this);
  auto* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, spacing::kPaddingMedium);

  auto* backButton = new QPushButton("\u2190", this);  // Left arrow
  backButton->setFixedSize(40, 40);
  backButton->setCursor(Qt::PointingHandCursor);
  backButton->setToolTip("Back (Escape)");
  backButton->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      font-size: 18px;
      color: #f0f6fc;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.15);
    }
  )");
  connect(backButton, &QPushButton::clicked, this, &StatisticsWidget::backRequested);
  headerLayout->addWidget(backButton);

  auto* titleLabel = new QLabel("Statistics", this);
  titleLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    font-weight: 600;
    color: %2;
    padding-left: 12px;
  )").arg(fonts::kFontSizeTitle).arg(colors::dark::kTextPrimary));
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  // Export button
  exportButton_ = new QPushButton("Export", this);
  exportButton_->setFixedHeight(36);
  exportButton_->setCursor(Qt::PointingHandCursor);
  exportButton_->setToolTip("Export statistics to JSON");
  exportButton_->setStyleSheet(R"(
    QPushButton {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      padding: 0 16px;
      font-size: 13px;
      color: #8b949e;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.15);
      color: #f0f6fc;
    }
  )");
  connect(exportButton_, &QPushButton::clicked, this, &StatisticsWidget::onExportClicked);
  headerLayout->addWidget(exportButton_);

  mainLayout->addWidget(headerWidget);

  // === Scrollable content ===
  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet("QScrollArea { background: transparent; }");

  auto* scrollContent = new QWidget(scrollArea);
  scrollContent->setStyleSheet("background: transparent;");
  auto* contentLayout = new QVBoxLayout(scrollContent);
  contentLayout->setSpacing(spacing::kPaddingMedium);
  contentLayout->setContentsMargins(0, 0, 0, 0);

  // Bandwidth graph section
  createBandwidthGraphSection(scrollContent);
  contentLayout->addWidget(bandwidthGraph_);

  // Latency graph section
  createLatencyGraphSection(scrollContent);
  contentLayout->addWidget(latencyGraph_);

  // Connection history section
  createConnectionHistorySection(scrollContent);

  contentLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea, 1);
}

void StatisticsWidget::createBandwidthGraphSection(QWidget* parent) {
  bandwidthGraph_ = new MiniGraphWidget(parent);
  bandwidthGraph_->setLabels("Bandwidth", "bytes/s");
  bandwidthGraph_->setDualSeries(true);
  bandwidthGraph_->setLineColor(QColor(88, 166, 255));    // Upload - blue
  bandwidthGraph_->setSecondLineColor(QColor(63, 185, 80));  // Download - green
  bandwidthGraph_->setMaxPoints(300);  // 5 minutes at 1 point/sec
}

void StatisticsWidget::createLatencyGraphSection(QWidget* parent) {
  latencyGraph_ = new MiniGraphWidget(parent);
  latencyGraph_->setLabels("Latency", "ms");
  latencyGraph_->setLineColor(QColor(210, 153, 34));  // Warning/yellow
  latencyGraph_->setMaxPoints(300);
}

void StatisticsWidget::createConnectionHistorySection(QWidget* parent) {
  auto* sectionCard = new QWidget(parent);
  sectionCard->setObjectName("historyCard");
  sectionCard->setStyleSheet(R"(
    #historyCard {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
    }
  )");

  auto* sectionLayout = new QVBoxLayout(sectionCard);
  sectionLayout->setSpacing(4);
  sectionLayout->setContentsMargins(16, 12, 16, 12);

  // Header row
  auto* sectionHeaderLayout = new QHBoxLayout();
  auto* sectionTitle = new QLabel("Connection History", sectionCard);
  sectionTitle->setStyleSheet(R"(
    font-size: 12px;
    font-weight: 600;
    color: #8b949e;
    letter-spacing: 1.2px;
  )");
  sectionHeaderLayout->addWidget(sectionTitle);
  sectionHeaderLayout->addStretch();

  clearButton_ = new QPushButton("Clear", sectionCard);
  clearButton_->setFixedHeight(28);
  clearButton_->setCursor(Qt::PointingHandCursor);
  clearButton_->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 6px;
      padding: 0 12px;
      font-size: 11px;
      color: #6e7681;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.04);
      color: #8b949e;
    }
  )");
  connect(clearButton_, &QPushButton::clicked, this, &StatisticsWidget::onClearHistoryClicked);
  sectionHeaderLayout->addWidget(clearButton_);

  sectionLayout->addLayout(sectionHeaderLayout);

  // Container for history entries
  historyContainer_ = new QWidget(sectionCard);
  historyContainer_->setStyleSheet("background: transparent;");
  new QVBoxLayout(historyContainer_);
  sectionLayout->addWidget(historyContainer_);

  // No-history placeholder
  noHistoryLabel_ = new QLabel("No connection history yet", sectionCard);
  noHistoryLabel_->setAlignment(Qt::AlignCenter);
  noHistoryLabel_->setStyleSheet("color: #6e7681; font-size: 13px; padding: 20px;");
  sectionLayout->addWidget(noHistoryLabel_);

  // Add to parent layout
  if (auto* parentLayout = parent->layout(); parentLayout != nullptr) {
    parentLayout->addWidget(sectionCard);
  }
}

void StatisticsWidget::recordBandwidth(uint64_t txBytesPerSec, uint64_t rxBytesPerSec) {
  bandwidthGraph_->addDataPoint(static_cast<double>(txBytesPerSec));
  bandwidthGraph_->addSecondDataPoint(static_cast<double>(rxBytesPerSec));
}

void StatisticsWidget::recordLatency(int latencyMs) {
  latencyGraph_->addDataPoint(static_cast<double>(latencyMs));
}

void StatisticsWidget::onSessionStarted(const QString& server, uint16_t port) {
  if (sessionActive_) {
    // Session already in progress; update server info if provided
    if (!server.isEmpty()) {
      currentServer_ = server;
      currentPort_ = port;
    }
    return;
  }

  sessionActive_ = true;
  currentSessionStart_ = QDateTime::currentDateTime();
  currentServer_ = server;
  currentPort_ = port;

  // Clear real-time graphs for new session
  bandwidthGraph_->clear();
  latencyGraph_->clear();
}

void StatisticsWidget::onSessionEnded(uint64_t totalTx, uint64_t totalRx) {
  if (!sessionActive_) return;
  sessionActive_ = false;

  ConnectionRecord record;
  record.startTime = currentSessionStart_;
  record.endTime = QDateTime::currentDateTime();
  record.serverAddress = currentServer_;
  record.serverPort = currentPort_;
  record.totalTxBytes = totalTx;
  record.totalRxBytes = totalRx;

  connectionHistory_.push_front(record);
  while (static_cast<int>(connectionHistory_.size()) > kMaxHistoryEntries) {
    connectionHistory_.pop_back();
  }

  updateHistoryDisplay();
}

void StatisticsWidget::updateHistoryDisplay() {
  // Remove old items
  auto* layout = historyContainer_->layout();
  QLayoutItem* item;
  while ((item = layout->takeAt(0)) != nullptr) {
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  noHistoryLabel_->setVisible(connectionHistory_.empty());

  for (const auto& record : connectionHistory_) {
    auto* entryWidget = new QWidget(historyContainer_);
    entryWidget->setStyleSheet(R"(
      QWidget {
        background: rgba(255, 255, 255, 0.02);
        border-radius: 8px;
      }
    )");

    auto* entryLayout = new QHBoxLayout(entryWidget);
    entryLayout->setContentsMargins(12, 8, 12, 8);
    entryLayout->setSpacing(12);

    // Time and server
    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);

    auto* serverLabel = new QLabel(
        QString("%1:%2").arg(record.serverAddress).arg(record.serverPort),
        entryWidget);
    serverLabel->setStyleSheet("color: #f0f6fc; font-size: 13px; font-weight: 500;");
    infoLayout->addWidget(serverLabel);

    qint64 durationSec = record.startTime.secsTo(record.endTime);
    auto* detailLabel = new QLabel(
        QString("%1  |  %2")
            .arg(record.startTime.toString("yyyy-MM-dd hh:mm"))
            .arg(formatDuration(durationSec)),
        entryWidget);
    detailLabel->setStyleSheet("color: #6e7681; font-size: 11px;");
    infoLayout->addWidget(detailLabel);

    entryLayout->addLayout(infoLayout, 1);

    // Data transferred
    auto* dataLabel = new QLabel(
        QString("\u2191 %1  \u2193 %2")
            .arg(formatBytes(record.totalTxBytes))
            .arg(formatBytes(record.totalRxBytes)),
        entryWidget);
    dataLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
    entryLayout->addWidget(dataLabel);

    layout->addWidget(entryWidget);
  }
}

void StatisticsWidget::onExportClicked() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Export Statistics", "veil_statistics.json",
      "JSON Files (*.json);;CSV Files (*.csv)");

  if (fileName.isEmpty()) return;

  if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
    // CSV export
    QString csv;
    csv += "Start Time,End Time,Server,Port,Duration (s),TX Bytes,RX Bytes\n";
    for (const auto& rec : connectionHistory_) {
      qint64 dur = rec.startTime.secsTo(rec.endTime);
      csv += QString("%1,%2,%3,%4,%5,%6,%7\n")
                 .arg(rec.startTime.toString(Qt::ISODate))
                 .arg(rec.endTime.toString(Qt::ISODate))
                 .arg(rec.serverAddress)
                 .arg(rec.serverPort)
                 .arg(dur)
                 .arg(rec.totalTxBytes)
                 .arg(rec.totalRxBytes);
    }

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(csv.toUtf8());
      file.close();
    }
  } else {
    // JSON export
    QJsonObject root;

    // Connection history
    QJsonArray historyArray;
    for (const auto& rec : connectionHistory_) {
      QJsonObject entry;
      entry["start_time"] = rec.startTime.toString(Qt::ISODate);
      entry["end_time"] = rec.endTime.toString(Qt::ISODate);
      entry["server"] = rec.serverAddress;
      entry["port"] = rec.serverPort;
      entry["duration_sec"] = rec.startTime.secsTo(rec.endTime);
      entry["tx_bytes"] = static_cast<qint64>(rec.totalTxBytes);
      entry["rx_bytes"] = static_cast<qint64>(rec.totalRxBytes);
      historyArray.append(entry);
    }
    root["connection_history"] = historyArray;
    root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
      file.close();
    }
  }
}

void StatisticsWidget::onClearHistoryClicked() {
  connectionHistory_.clear();
  updateHistoryDisplay();
}

QString StatisticsWidget::formatBytes(uint64_t bytes) const {
  if (bytes >= 1073741824ULL) {
    return QString("%1 GB").arg(static_cast<double>(bytes) / 1073741824.0, 0, 'f', 1);
  } else if (bytes >= 1048576ULL) {
    return QString("%1 MB").arg(static_cast<double>(bytes) / 1048576.0, 0, 'f', 1);
  } else if (bytes >= 1024ULL) {
    return QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  return QString("%1 B").arg(bytes);
}

QString StatisticsWidget::formatDuration(qint64 seconds) const {
  if (seconds < 60) {
    return QString("%1s").arg(seconds);
  } else if (seconds < 3600) {
    return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
  }
  qint64 hours = seconds / 3600;
  qint64 mins = (seconds % 3600) / 60;
  return QString("%1h %2m").arg(hours).arg(mins);
}

}  // namespace veil::gui
