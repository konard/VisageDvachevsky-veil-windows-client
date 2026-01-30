#include "data_usage_widget.h"

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

// ===================== UsageBarChart Implementation =====================

UsageBarChart::UsageBarChart(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void UsageBarChart::setData(const std::vector<BarData>& data) {
  data_ = data;
  update();
}

void UsageBarChart::setTitle(const QString& title) {
  title_ = title;
  update();
}

void UsageBarChart::clear() {
  data_.clear();
  update();
}

void UsageBarChart::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int w = width();
  const int h = height();
  const int headerH = 28;
  const int bottomMargin = 40;
  const int leftMargin = 8;
  const int rightMargin = 8;

  // Background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(22, 27, 34, 200));
  p.drawRoundedRect(rect(), 12, 12);

  // Border
  p.setPen(QPen(QColor(255, 255, 255, 15), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);

  // Title
  p.setPen(QColor(139, 148, 158));
  QFont titleFont;
  titleFont.setPixelSize(12);
  titleFont.setWeight(QFont::DemiBold);
  titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  p.setFont(titleFont);
  p.drawText(QRect(12, 4, w - 24, headerH), Qt::AlignLeft | Qt::AlignVCenter,
             title_.toUpper());

  // Chart area
  const int cx = leftMargin;
  const int cy = headerH;
  const int cw = w - leftMargin - rightMargin;
  const int ch = h - headerH - bottomMargin;

  if (data_.empty() || cw <= 0 || ch <= 0) {
    p.setPen(QColor(110, 118, 129, 100));
    QFont placeholderFont;
    placeholderFont.setPixelSize(13);
    p.setFont(placeholderFont);
    p.drawText(QRect(cx, cy, cw, ch), Qt::AlignCenter, "No usage data yet");
    return;
  }

  // Draw subtle horizontal grid lines
  p.setPen(QPen(QColor(255, 255, 255, 10), 1, Qt::DotLine));
  for (int i = 1; i <= 3; ++i) {
    int y = cy + ch * i / 4;
    p.drawLine(cx, y, cx + cw, y);
  }

  // Find max value for scaling
  uint64_t maxVal = 1;
  for (const auto& bar : data_) {
    if (bar.totalBytes() > maxVal) {
      maxVal = bar.totalBytes();
    }
  }
  // Add 10% headroom
  maxVal = static_cast<uint64_t>(maxVal * 1.1);

  const int barCount = static_cast<int>(data_.size());
  const double barWidth = static_cast<double>(cw) / barCount;
  const double barPadding = barWidth * 0.15;
  const double halfBarWidth = (barWidth - barPadding * 2) / 2.0;

  for (int i = 0; i < barCount; ++i) {
    const auto& bar = data_[static_cast<size_t>(i)];
    double barX = cx + i * barWidth + barPadding;

    // TX bar (upload - blue)
    double txHeight = (static_cast<double>(bar.txBytes) / static_cast<double>(maxVal)) * ch;
    QRectF txRect(barX, cy + ch - txHeight, halfBarWidth, txHeight);
    if (txHeight > 2) {
      QPainterPath txPath;
      txPath.addRoundedRect(txRect, 3, 3);
      QLinearGradient txGrad(txRect.topLeft(), txRect.bottomLeft());
      txGrad.setColorAt(0, QColor(88, 166, 255, 200));
      txGrad.setColorAt(1, QColor(88, 166, 255, 100));
      p.setPen(Qt::NoPen);
      p.setBrush(txGrad);
      p.drawPath(txPath);
    }

    // RX bar (download - green)
    double rxHeight = (static_cast<double>(bar.rxBytes) / static_cast<double>(maxVal)) * ch;
    QRectF rxRect(barX + halfBarWidth, cy + ch - rxHeight, halfBarWidth, rxHeight);
    if (rxHeight > 2) {
      QPainterPath rxPath;
      rxPath.addRoundedRect(rxRect, 3, 3);
      QLinearGradient rxGrad(rxRect.topLeft(), rxRect.bottomLeft());
      rxGrad.setColorAt(0, QColor(63, 185, 80, 200));
      rxGrad.setColorAt(1, QColor(63, 185, 80, 100));
      p.setPen(Qt::NoPen);
      p.setBrush(rxGrad);
      p.drawPath(rxPath);
    }

    // Label below bars
    p.setPen(QColor(110, 118, 129));
    QFont labelFont;
    labelFont.setPixelSize(10);
    p.setFont(labelFont);
    QRectF labelRect(barX - barPadding, cy + ch + 2,
                     barWidth, bottomMargin - 4);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, bar.label);

    // Value above the taller bar
    uint64_t total = bar.totalBytes();
    if (total > 0) {
      p.setPen(QColor(139, 148, 158, 180));
      labelFont.setPixelSize(9);
      p.setFont(labelFont);
      double topY = cy + ch - std::max(txHeight, rxHeight) - 14;
      QRectF valRect(barX - barPadding, topY, barWidth, 12);
      p.drawText(valRect, Qt::AlignHCenter | Qt::AlignBottom, formatBytes(total));
    }
  }

  // Legend at top right
  QFont legendFont;
  legendFont.setPixelSize(10);
  p.setFont(legendFont);

  int legendX = w - 160;
  int legendY = 8;

  // Upload legend
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(88, 166, 255, 200));
  p.drawRoundedRect(legendX, legendY + 2, 8, 8, 2, 2);
  p.setPen(QColor(139, 148, 158));
  p.drawText(legendX + 12, legendY + 10, "Upload");

  // Download legend
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(63, 185, 80, 200));
  p.drawRoundedRect(legendX + 70, legendY + 2, 8, 8, 2, 2);
  p.setPen(QColor(139, 148, 158));
  p.drawText(legendX + 82, legendY + 10, "Download");
}

QString UsageBarChart::formatBytes(uint64_t bytes) const {
  if (bytes >= 1073741824ULL) {
    return QString("%1 GB").arg(static_cast<double>(bytes) / 1073741824.0, 0, 'f', 1);
  } else if (bytes >= 1048576ULL) {
    return QString("%1 MB").arg(static_cast<double>(bytes) / 1048576.0, 0, 'f', 1);
  } else if (bytes >= 1024ULL) {
    return QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  return QString("%1 B").arg(bytes);
}

// ===================== DataUsageWidget Implementation =====================

DataUsageWidget::DataUsageWidget(UsageTracker* tracker, QWidget* parent)
    : QWidget(parent), tracker_(tracker) {
  setupUi();

  connect(tracker_, &UsageTracker::usageUpdated, this, &DataUsageWidget::refresh);
}

void DataUsageWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(spacing::kPaddingXLarge(), spacing::kPaddingLarge(),
                                  spacing::kPaddingXLarge(), spacing::kPaddingLarge());

  // === Header ===
  auto* headerWidget = new QWidget(this);
  auto* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, spacing::kPaddingMedium());

  auto* backButton = new QPushButton("\u2190", this);
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
  connect(backButton, &QPushButton::clicked, this, &DataUsageWidget::backRequested);
  headerLayout->addWidget(backButton);

  auto* titleLabel = new QLabel("Data Usage", this);
  titleLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    font-weight: 600;
    color: %2;
    padding-left: 12px;
  )").arg(fonts::kFontSizeTitle()).arg(colors::dark::kTextPrimary));
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  // Export button
  exportButton_ = new QPushButton("Export", this);
  exportButton_->setFixedHeight(36);
  exportButton_->setCursor(Qt::PointingHandCursor);
  exportButton_->setToolTip("Export usage data to CSV or JSON");
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
  connect(exportButton_, &QPushButton::clicked, this, &DataUsageWidget::onExportClicked);
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
  contentLayout->setSpacing(spacing::kPaddingMedium());
  contentLayout->setContentsMargins(0, 0, 0, 0);

  // Summary section
  createSummarySection(scrollContent);

  // Chart section
  createChartSection(scrollContent);

  // Alert section
  createAlertSection(scrollContent);

  contentLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea, 1);

  // Initial data load
  refresh();
}

void DataUsageWidget::createSummarySection(QWidget* parent) {
  auto* card = new QWidget(parent);
  card->setObjectName("summaryCard");
  card->setStyleSheet(R"(
    #summaryCard {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
    }
  )");

  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setSpacing(12);
  cardLayout->setContentsMargins(16, 12, 16, 12);

  // Today's usage header
  auto* todayTitle = new QLabel("TODAY", card);
  todayTitle->setStyleSheet(R"(
    font-size: 12px; font-weight: 600; color: #8b949e;
    letter-spacing: 1.2px;
  )");
  cardLayout->addWidget(todayTitle);

  auto* todayRow = new QHBoxLayout();
  todayRow->setSpacing(16);

  auto createStatLabel = [card](const QString& text) -> QLabel* {
    auto* label = new QLabel(text, card);
    label->setStyleSheet("font-size: 14px; font-weight: 500; color: #f0f6fc;");
    return label;
  };

  auto createCaptionLabel = [card](const QString& text) -> QLabel* {
    auto* label = new QLabel(text, card);
    label->setStyleSheet("font-size: 11px; color: #6e7681;");
    return label;
  };

  // Upload today
  auto* todayUpLayout = new QVBoxLayout();
  todayUpLayout->setSpacing(2);
  auto* upIcon = new QLabel("\u2191", card);
  upIcon->setStyleSheet("font-size: 11px; color: #58a6ff;");
  todayUpLayout->addWidget(upIcon);
  todayUploadLabel_ = createStatLabel("0 B");
  todayUpLayout->addWidget(todayUploadLabel_);
  todayUpLayout->addWidget(createCaptionLabel("Upload"));
  todayRow->addLayout(todayUpLayout);

  // Download today
  auto* todayDownLayout = new QVBoxLayout();
  todayDownLayout->setSpacing(2);
  auto* downIcon = new QLabel("\u2193", card);
  downIcon->setStyleSheet("font-size: 11px; color: #3fb950;");
  todayDownLayout->addWidget(downIcon);
  todayDownloadLabel_ = createStatLabel("0 B");
  todayDownLayout->addWidget(todayDownloadLabel_);
  todayDownLayout->addWidget(createCaptionLabel("Download"));
  todayRow->addLayout(todayDownLayout);

  // Total today
  auto* todayTotalLayout = new QVBoxLayout();
  todayTotalLayout->setSpacing(2);
  auto* totalIcon = new QLabel("\u2195", card);
  totalIcon->setStyleSheet("font-size: 11px; color: #d29922;");
  todayTotalLayout->addWidget(totalIcon);
  todayTotalLabel_ = createStatLabel("0 B");
  todayTotalLabel_->setStyleSheet("font-size: 14px; font-weight: 600; color: #f0f6fc;");
  todayTotalLayout->addWidget(todayTotalLabel_);
  todayTotalLayout->addWidget(createCaptionLabel("Total"));
  todayRow->addLayout(todayTotalLayout);

  todayRow->addStretch();
  cardLayout->addLayout(todayRow);

  // Separator
  auto* sep = new QWidget(card);
  sep->setFixedHeight(1);
  sep->setStyleSheet("background: rgba(255, 255, 255, 0.06);");
  cardLayout->addWidget(sep);

  // Monthly usage header
  auto* monthTitle = new QLabel("THIS MONTH", card);
  monthTitle->setStyleSheet(R"(
    font-size: 12px; font-weight: 600; color: #8b949e;
    letter-spacing: 1.2px;
  )");
  cardLayout->addWidget(monthTitle);

  auto* monthRow = new QHBoxLayout();
  monthRow->setSpacing(16);

  // Upload month
  auto* monthUpLayout = new QVBoxLayout();
  monthUpLayout->setSpacing(2);
  monthUploadLabel_ = createStatLabel("0 B");
  monthUpLayout->addWidget(monthUploadLabel_);
  monthUpLayout->addWidget(createCaptionLabel("Upload"));
  monthRow->addLayout(monthUpLayout);

  // Download month
  auto* monthDownLayout = new QVBoxLayout();
  monthDownLayout->setSpacing(2);
  monthDownloadLabel_ = createStatLabel("0 B");
  monthDownLayout->addWidget(monthDownloadLabel_);
  monthDownLayout->addWidget(createCaptionLabel("Download"));
  monthRow->addLayout(monthDownLayout);

  // Total month
  auto* monthTotalLayout = new QVBoxLayout();
  monthTotalLayout->setSpacing(2);
  monthTotalLabel_ = createStatLabel("0 B");
  monthTotalLabel_->setStyleSheet("font-size: 14px; font-weight: 600; color: #f0f6fc;");
  monthTotalLayout->addWidget(monthTotalLabel_);
  monthTotalLayout->addWidget(createCaptionLabel("Total"));
  monthRow->addLayout(monthTotalLayout);

  // Sessions
  auto* sessionsLayout = new QVBoxLayout();
  sessionsLayout->setSpacing(2);
  monthSessionsLabel_ = createStatLabel("0");
  sessionsLayout->addWidget(monthSessionsLabel_);
  sessionsLayout->addWidget(createCaptionLabel("Sessions"));
  monthRow->addLayout(sessionsLayout);

  // Duration
  auto* durationLayout = new QVBoxLayout();
  durationLayout->setSpacing(2);
  monthDurationLabel_ = createStatLabel("0s");
  durationLayout->addWidget(monthDurationLabel_);
  durationLayout->addWidget(createCaptionLabel("Duration"));
  monthRow->addLayout(durationLayout);

  monthRow->addStretch();
  cardLayout->addLayout(monthRow);

  if (auto* parentLayout = parent->layout(); parentLayout != nullptr) {
    parentLayout->addWidget(card);
  }
}

void DataUsageWidget::createChartSection(QWidget* parent) {
  auto* card = new QWidget(parent);
  card->setObjectName("chartCard");
  card->setStyleSheet(R"(
    #chartCard {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
    }
  )");

  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setSpacing(8);
  cardLayout->setContentsMargins(16, 12, 16, 12);

  // Period selector
  auto* headerRow = new QHBoxLayout();
  auto* chartTitle = new QLabel("USAGE CHART", card);
  chartTitle->setStyleSheet(R"(
    font-size: 12px; font-weight: 600; color: #8b949e;
    letter-spacing: 1.2px;
  )");
  headerRow->addWidget(chartTitle);
  headerRow->addStretch();

  periodCombo_ = new QComboBox(card);
  periodCombo_->addItem("Last 7 Days");
  periodCombo_->addItem("Last 30 Days");
  periodCombo_->addItem("Last 6 Months");
  periodCombo_->addItem("Last 12 Months");
  periodCombo_->setFixedHeight(28);
  periodCombo_->setStyleSheet(R"(
    QComboBox {
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 6px;
      padding: 0 8px;
      font-size: 11px;
      color: #8b949e;
      min-width: 120px;
    }
    QComboBox:hover {
      background: rgba(255, 255, 255, 0.08);
      color: #f0f6fc;
    }
    QComboBox::drop-down {
      border: none;
      width: 20px;
    }
    QComboBox::down-arrow {
      image: none;
      border-left: 4px solid transparent;
      border-right: 4px solid transparent;
      border-top: 5px solid #8b949e;
      margin-right: 6px;
    }
  )");
  connect(periodCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DataUsageWidget::onPeriodChanged);
  headerRow->addWidget(periodCombo_);

  // Clear history button
  clearButton_ = new QPushButton("Clear", card);
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
  connect(clearButton_, &QPushButton::clicked, this, &DataUsageWidget::onClearHistoryClicked);
  headerRow->addWidget(clearButton_);

  cardLayout->addLayout(headerRow);

  // Bar chart
  usageChart_ = new UsageBarChart(card);
  cardLayout->addWidget(usageChart_);

  if (auto* parentLayout = parent->layout(); parentLayout != nullptr) {
    parentLayout->addWidget(card);
  }
}

void DataUsageWidget::createAlertSection(QWidget* parent) {
  auto* card = new QWidget(parent);
  card->setObjectName("alertCard");
  card->setStyleSheet(R"(
    #alertCard {
      background-color: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 16px;
    }
  )");

  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setSpacing(8);
  cardLayout->setContentsMargins(16, 12, 16, 12);

  auto* alertTitle = new QLabel("USAGE ALERTS", card);
  alertTitle->setStyleSheet(R"(
    font-size: 12px; font-weight: 600; color: #8b949e;
    letter-spacing: 1.2px;
  )");
  cardLayout->addWidget(alertTitle);

  // Enable alerts
  alertEnabledCheck_ = new QCheckBox("Enable usage alerts", card);
  alertEnabledCheck_->setStyleSheet(R"(
    QCheckBox { color: #f0f6fc; font-size: 13px; spacing: 8px; }
    QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid rgba(255, 255, 255, 0.15); border-radius: 4px; background: #161b22; }
    QCheckBox::indicator:checked { background: #238636; border-color: #238636; }
  )");
  connect(alertEnabledCheck_, &QCheckBox::toggled, this, &DataUsageWidget::onAlertSettingsChanged);
  cardLayout->addWidget(alertEnabledCheck_);

  // Warning threshold
  auto* warningRow = new QHBoxLayout();
  warningRow->setSpacing(8);
  auto* warningLabel = new QLabel("Warning at:", card);
  warningLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
  warningRow->addWidget(warningLabel);

  warningThresholdSpin_ = new QSpinBox(card);
  warningThresholdSpin_->setRange(0, 99999);
  warningThresholdSpin_->setValue(0);
  warningThresholdSpin_->setFixedWidth(80);
  warningThresholdSpin_->setStyleSheet(R"(
    QSpinBox { background: #161b22; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 4px 8px; color: #f0f6fc; font-size: 12px; }
  )");
  connect(warningThresholdSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &DataUsageWidget::onAlertSettingsChanged);
  warningRow->addWidget(warningThresholdSpin_);

  warningUnitCombo_ = new QComboBox(card);
  warningUnitCombo_->addItem("MB");
  warningUnitCombo_->addItem("GB");
  warningUnitCombo_->setCurrentIndex(1);
  warningUnitCombo_->setFixedWidth(60);
  warningUnitCombo_->setStyleSheet(R"(
    QComboBox { background: #161b22; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 4px 8px; color: #f0f6fc; font-size: 12px; }
    QComboBox::drop-down { border: none; width: 16px; }
    QComboBox::down-arrow { image: none; border-left: 3px solid transparent; border-right: 3px solid transparent; border-top: 4px solid #8b949e; margin-right: 4px; }
  )");
  connect(warningUnitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DataUsageWidget::onAlertSettingsChanged);
  warningRow->addWidget(warningUnitCombo_);
  warningRow->addStretch();
  cardLayout->addLayout(warningRow);

  // Limit threshold
  auto* limitRow = new QHBoxLayout();
  limitRow->setSpacing(8);
  auto* limitLabel = new QLabel("Limit at:", card);
  limitLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
  limitRow->addWidget(limitLabel);

  limitThresholdSpin_ = new QSpinBox(card);
  limitThresholdSpin_->setRange(0, 99999);
  limitThresholdSpin_->setValue(0);
  limitThresholdSpin_->setFixedWidth(80);
  limitThresholdSpin_->setStyleSheet(R"(
    QSpinBox { background: #161b22; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 4px 8px; color: #f0f6fc; font-size: 12px; }
  )");
  connect(limitThresholdSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &DataUsageWidget::onAlertSettingsChanged);
  limitRow->addWidget(limitThresholdSpin_);

  limitUnitCombo_ = new QComboBox(card);
  limitUnitCombo_->addItem("MB");
  limitUnitCombo_->addItem("GB");
  limitUnitCombo_->setCurrentIndex(1);
  limitUnitCombo_->setFixedWidth(60);
  limitUnitCombo_->setStyleSheet(R"(
    QComboBox { background: #161b22; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 4px 8px; color: #f0f6fc; font-size: 12px; }
    QComboBox::drop-down { border: none; width: 16px; }
    QComboBox::down-arrow { image: none; border-left: 3px solid transparent; border-right: 3px solid transparent; border-top: 4px solid #8b949e; margin-right: 4px; }
  )");
  connect(limitUnitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DataUsageWidget::onAlertSettingsChanged);
  limitRow->addWidget(limitUnitCombo_);
  limitRow->addStretch();
  cardLayout->addLayout(limitRow);

  // Auto-disconnect checkbox
  autoDisconnectCheck_ = new QCheckBox("Auto-disconnect at limit", card);
  autoDisconnectCheck_->setStyleSheet(R"(
    QCheckBox { color: #f0f6fc; font-size: 13px; spacing: 8px; }
    QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid rgba(255, 255, 255, 0.15); border-radius: 4px; background: #161b22; }
    QCheckBox::indicator:checked { background: #f85149; border-color: #f85149; }
  )");
  connect(autoDisconnectCheck_, &QCheckBox::toggled, this, &DataUsageWidget::onAlertSettingsChanged);
  cardLayout->addWidget(autoDisconnectCheck_);

  // Load current alert settings
  const auto& alerts = tracker_->alertSettings();
  alertEnabledCheck_->setChecked(alerts.enabled);
  autoDisconnectCheck_->setChecked(alerts.autoDisconnectAtLimit);

  // Convert bytes to display units
  if (alerts.warningThresholdBytes >= 1073741824ULL) {
    warningThresholdSpin_->setValue(static_cast<int>(alerts.warningThresholdBytes / 1073741824ULL));
    warningUnitCombo_->setCurrentIndex(1);  // GB
  } else {
    warningThresholdSpin_->setValue(static_cast<int>(alerts.warningThresholdBytes / 1048576ULL));
    warningUnitCombo_->setCurrentIndex(0);  // MB
  }

  if (alerts.limitThresholdBytes >= 1073741824ULL) {
    limitThresholdSpin_->setValue(static_cast<int>(alerts.limitThresholdBytes / 1073741824ULL));
    limitUnitCombo_->setCurrentIndex(1);  // GB
  } else {
    limitThresholdSpin_->setValue(static_cast<int>(alerts.limitThresholdBytes / 1048576ULL));
    limitUnitCombo_->setCurrentIndex(0);  // MB
  }

  if (auto* parentLayout = parent->layout(); parentLayout != nullptr) {
    parentLayout->addWidget(card);
  }
}

void DataUsageWidget::refresh() {
  updateSummary();
  updateChart();
}

void DataUsageWidget::updateSummary() {
  auto today = tracker_->getTodayUsage();
  todayUploadLabel_->setText(formatBytes(today.txBytes));
  todayDownloadLabel_->setText(formatBytes(today.rxBytes));
  todayTotalLabel_->setText(formatBytes(today.totalBytes()));

  auto month = tracker_->getCurrentMonthUsage();
  monthUploadLabel_->setText(formatBytes(month.txBytes));
  monthDownloadLabel_->setText(formatBytes(month.rxBytes));
  monthTotalLabel_->setText(formatBytes(month.totalBytes()));
  monthSessionsLabel_->setText(QString::number(month.sessionCount));
  monthDurationLabel_->setText(formatDuration(month.totalDurationSec));
}

void DataUsageWidget::updateChart() {
  std::vector<UsageBarChart::BarData> chartData;

  int periodIndex = periodCombo_->currentIndex();
  QDate today = QDate::currentDate();

  if (periodIndex <= 1) {
    // Daily view: last 7 or 30 days
    int days = (periodIndex == 0) ? 7 : 30;
    QDate from = today.addDays(-(days - 1));
    auto records = tracker_->getDailyUsage(from, today);

    // Create entries for all days in range
    for (int i = 0; i < days; ++i) {
      QDate date = from.addDays(i);
      UsageBarChart::BarData bar;
      bar.label = date.toString("MM/dd");

      // Find matching record
      for (const auto& rec : records) {
        if (rec.date == date) {
          bar.txBytes = rec.txBytes;
          bar.rxBytes = rec.rxBytes;
          break;
        }
      }
      chartData.push_back(bar);
    }

    usageChart_->setTitle(periodIndex == 0 ? "Last 7 Days" : "Last 30 Days");
  } else {
    // Monthly view: last 6 or 12 months
    int months = (periodIndex == 2) ? 6 : 12;
    for (int i = months - 1; i >= 0; --i) {
      QDate monthDate = today.addMonths(-i);
      auto monthData = tracker_->getMonthlyUsage(monthDate.year(), monthDate.month());

      UsageBarChart::BarData bar;
      bar.label = monthDate.toString("MMM");
      bar.txBytes = monthData.txBytes;
      bar.rxBytes = monthData.rxBytes;
      chartData.push_back(bar);
    }

    usageChart_->setTitle(periodIndex == 2 ? "Last 6 Months" : "Last 12 Months");
  }

  usageChart_->setData(chartData);
}

void DataUsageWidget::onPeriodChanged(int /*index*/) {
  updateChart();
}

void DataUsageWidget::onExportClicked() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Export Usage Data", "veil_usage_data.csv",
      "CSV Files (*.csv);;JSON Files (*.json)");

  if (fileName.isEmpty()) return;

  QDate today = QDate::currentDate();
  QDate from = today.addDays(-UsageTracker::kMaxDaysRetained);
  auto records = tracker_->getDailyUsage(from, today);

  if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
    QString csv;
    csv += "Date,Upload (bytes),Download (bytes),Total (bytes),Sessions,Duration (s)\n";
    for (const auto& rec : records) {
      csv += QString("%1,%2,%3,%4,%5,%6\n")
                 .arg(rec.date.toString(Qt::ISODate))
                 .arg(rec.txBytes)
                 .arg(rec.rxBytes)
                 .arg(rec.totalBytes())
                 .arg(rec.sessionCount)
                 .arg(rec.totalDurationSec);
    }

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(csv.toUtf8());
      file.close();
    }
  } else {
    QJsonObject root;
    QJsonArray arr;
    for (const auto& rec : records) {
      arr.append(rec.toJson());
    }
    root["daily_usage"] = arr;
    root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Include monthly summaries
    QJsonArray monthlyArr;
    for (int i = 11; i >= 0; --i) {
      QDate monthDate = today.addMonths(-i);
      auto monthData = tracker_->getMonthlyUsage(monthDate.year(), monthDate.month());
      if (monthData.totalBytes() > 0) {
        QJsonObject monthObj;
        monthObj["month"] = monthDate.toString("yyyy-MM");
        monthObj["tx_bytes"] = static_cast<qint64>(monthData.txBytes);
        monthObj["rx_bytes"] = static_cast<qint64>(monthData.rxBytes);
        monthObj["sessions"] = static_cast<qint64>(monthData.sessionCount);
        monthObj["duration_sec"] = static_cast<qint64>(monthData.totalDurationSec);
        monthlyArr.append(monthObj);
      }
    }
    root["monthly_summary"] = monthlyArr;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
      file.close();
    }
  }
}

void DataUsageWidget::onClearHistoryClicked() {
  tracker_->clearHistory();
  refresh();
}

void DataUsageWidget::onAlertSettingsChanged() {
  UsageAlert alert;
  alert.enabled = alertEnabledCheck_->isChecked();
  alert.autoDisconnectAtLimit = autoDisconnectCheck_->isChecked();

  // Convert from display units to bytes
  uint64_t warningMultiplier = (warningUnitCombo_->currentIndex() == 1)
                                   ? 1073741824ULL  // GB
                                   : 1048576ULL;    // MB
  alert.warningThresholdBytes = static_cast<uint64_t>(warningThresholdSpin_->value()) * warningMultiplier;

  uint64_t limitMultiplier = (limitUnitCombo_->currentIndex() == 1)
                                 ? 1073741824ULL  // GB
                                 : 1048576ULL;    // MB
  alert.limitThresholdBytes = static_cast<uint64_t>(limitThresholdSpin_->value()) * limitMultiplier;

  tracker_->setAlertSettings(alert);
}

QString DataUsageWidget::formatBytes(uint64_t bytes) const {
  if (bytes >= 1073741824ULL) {
    return QString("%1 GB").arg(static_cast<double>(bytes) / 1073741824.0, 0, 'f', 1);
  } else if (bytes >= 1048576ULL) {
    return QString("%1 MB").arg(static_cast<double>(bytes) / 1048576.0, 0, 'f', 1);
  } else if (bytes >= 1024ULL) {
    return QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  return QString("%1 B").arg(bytes);
}

QString DataUsageWidget::formatDuration(uint64_t seconds) const {
  if (seconds < 60) {
    return QString("%1s").arg(seconds);
  } else if (seconds < 3600) {
    return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
  }
  uint64_t hours = seconds / 3600;
  uint64_t mins = (seconds % 3600) / 60;
  if (hours < 24) {
    return QString("%1h %2m").arg(hours).arg(mins);
  }
  uint64_t days = hours / 24;
  hours = hours % 24;
  return QString("%1d %2h").arg(days).arg(hours);
}

}  // namespace veil::gui
