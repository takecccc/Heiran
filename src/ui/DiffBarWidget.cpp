#include "heiran/ui/DiffBarWidget.hpp"
#include "heiran/ui/ActionIcons.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

constexpr int kBarFontPoints = 9;

// 閾値の既定は、モードで尺度が違う: 輝度は 8 (0-255)、色差は 2.3 (ほぼ気付く差。ΔE の単位)、SSIM は
// 0.9 (これを下回った画素を数える)。
double defaultThresholdFor(const QString &mode) {
  if (mode == QStringLiteral("delta-e")) {
    return 2.3;
  }
  if (mode == QStringLiteral("ssim")) {
    return 0.9;
  }
  return 8.0;
}

QString metricsText(const heiran::domain::DiffMetrics &metrics) {
  if (metrics.ssim) {
    const double below =
        metrics.pixelCount > 0
            ? 100.0 * static_cast<double>(metrics.exceededPixelCount) / metrics.pixelCount
            : 0.0;
    return QCoreApplication::translate("DiffBarWidget", "SSIM mean %1  min %2  below %3%")
        .arg(metrics.ssimMean, 0, 'f', 4)
        .arg(metrics.ssimMin, 0, 'f', 3)
        .arg(below, 0, 'f', 1);
  }
  if (metrics.wipe) {
    return QCoreApplication::translate("DiffBarWidget", "Drag the line to move the wipe");
  }
  if (metrics.colorDifference) {
    const double over =
        metrics.pixelCount > 0
            ? 100.0 * static_cast<double>(metrics.exceededPixelCount) / metrics.pixelCount
            : 0.0;
    return QCoreApplication::translate("DiffBarWidget", "ΔE00 max %1  mean %2  over %3%")
        .arg(metrics.maxAbsDiff, 0, 'f', 2)
        .arg(metrics.meanAbsDiff, 0, 'f', 2)
        .arg(over, 0, 'f', 1);
  }
  const QString psnr =
      metrics.psnrIsInfinite
          ? QStringLiteral("inf")
          : QCoreApplication::translate("DiffBarWidget", "%1 dB").arg(metrics.psnr, 0, 'f', 1);
  const double ratio =
      metrics.pixelCount > 0
          ? 100.0 * static_cast<double>(metrics.exceededPixelCount) / metrics.pixelCount
          : 0.0;
  return QCoreApplication::translate("DiffBarWidget", "max %1  mean %2  RMSE %3  PSNR %4  over %5%")
      .arg(metrics.maxAbsDiff, 0, 'f', 1)
      .arg(metrics.meanAbsDiff, 0, 'f', 2)
      .arg(metrics.rmse, 0, 'f', 2)
      .arg(psnr)
      .arg(ratio, 0, 'f', 1);
}

// 幅に収まらない文字を、省略記号で切るラベル。全文は、ツールチップで読める。
// 幅の希望は持たない (狭いセルを、帯が押し広げない)。
class ElidedLabel : public QLabel {
public:
  explicit ElidedLabel(QWidget *parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
  }

  void setFullText(const QString &text) {
    fullText_ = text;
    setToolTip(text);
    update();
  }

  const QString &fullText() const { return fullText_; }

  QSize sizeHint() const override { return QSize(0, fontMetrics().height() + 2); }
  QSize minimumSizeHint() const override { return QSize(0, fontMetrics().height() + 2); }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.setFont(font());
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(fullText_, Qt::ElideRight, width()));
  }

private:
  QString fullText_;
};

}  // namespace

DiffBarWidget::DiffBarWidget(QWidget *parent) : QWidget(parent) {
  setObjectName("diffBar");
  // 背景は QSS で塗る。幅の希望は無視させる (狭いセルを帯が押し広げない)。
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  // 帯には、求め方と、指標だけを置く。そのほかの設定は、歯車のボタンで開く一覧へ置く
  // (狭いセルでも欠けず、選択肢の名前を略さずに読める)。比べているセルは、セルの題が示す。
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 3, 6, 3);
  root->setSpacing(2);

  auto *controls = new QHBoxLayout;
  controls->setSpacing(4);
  auto *caption = new QLabel(tr("Diff"), this);
  modeCombo_ = new QComboBox(this);
  modeCombo_->addItems({"absolute", "signed", "binary", "delta-e", "ssim", "wipe", "rgb-max"});
  modeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  modeCombo_->setToolTip(tr(
      "How the difference is computed. delta-e is the CIEDE2000 colour difference of the whole RGB "
      "colour (it takes longer to compute); ssim is the structural similarity of the luma (1 means "
      "identical; the picture shows 1 - SSIM); wipe joins the two images at a line you can drag; "
      "rgb-max uses the largest of the three RGB channel differences; "
      "the others use luma."));
  settingsButton_ = new QToolButton(this);
  settingsButton_->setText(QStringLiteral("⚙"));
  settingsButton_->setToolTip(tr("Diff settings"));
  settingsButton_->setAutoRaise(true);
  closeButton_ = new QToolButton(this);
  closeButton_->setText(QStringLiteral("×"));
  closeButton_->setToolTip(tr("Stop showing the difference"));
  closeButton_->setAutoRaise(true);
  controls->addWidget(caption);
  controls->addWidget(modeCombo_);
  controls->addStretch(1);
  controls->addWidget(settingsButton_);
  controls->addWidget(closeButton_);
  root->addLayout(controls);

  auto *inspection = new QHBoxLayout;
  inspection->setSpacing(4);
  const auto addButton = [&](const char *name, const QString &icon, const QString &tooltip) {
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setIcon(themedIcon(icon));
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setAutoRaise(true);
    button->setEnabled(false);
    inspection->addWidget(button);
    return button;
  };
  previousRegion_ = addButton("previousDiffRegion", "arrow-left", tr("Previous difference"));
  nextRegion_ = addButton("nextDiffRegion", "arrow-right", tr("Next difference"));
  regionLabel_ = new ElidedLabel(this);
  inspection->addWidget(regionLabel_, 1);
  blinkButton_ = addButton("blinkDiffImages", "layers", tr("Alternate the base and target images"));
  blinkButton_->setText(tr("Blink"));
  blinkButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  blinkButton_->setCheckable(true);
  root->addLayout(inspection);
  connect(previousRegion_, &QToolButton::clicked, this,
          [this]() { Q_EMIT regionStepRequested(-1); });
  connect(nextRegion_, &QToolButton::clicked, this, [this]() { Q_EMIT regionStepRequested(1); });
  connect(blinkButton_, &QToolButton::toggled, this, &DiffBarWidget::blinkRequested);

  resultLabel_ = new ElidedLabel(this);
  resultLabel_->setObjectName("diffBarResult");
  root->addWidget(resultLabel_);

  // 設定の一覧。外を押すと閉じる。
  settingsPopup_ = new QFrame(this, Qt::Popup);
  settingsPopup_->setObjectName("diffSettingsPopup");
  settingsPopup_->setAttribute(Qt::WA_StyledBackground, true);
  auto *form = new QFormLayout(settingsPopup_);
  form->setContentsMargins(10, 8, 10, 8);
  form->setSpacing(6);
  baseCombo_ = new QComboBox(settingsPopup_);
  targetCombo_ = new QComboBox(settingsPopup_);
  viewCombo_ = new QComboBox(settingsPopup_);
  gainSpinBox_ = new QDoubleSpinBox(settingsPopup_);
  thresholdSpinBox_ = new QDoubleSpinBox(settingsPopup_);
  autoRangeCheckBox_ = new QCheckBox(tr("Auto range"), settingsPopup_);
  sizeMatchCombo_ = new QComboBox(settingsPopup_);
  wipeDirectionCombo_ = new QComboBox(settingsPopup_);
  wipePositionSpinBox_ = new QDoubleSpinBox(settingsPopup_);

  baseCombo_->setToolTip(tr("Base cell"));
  targetCombo_->setToolTip(tr("Compared cell"));
  viewCombo_->addItems({"grayscale", "signed-color", "overlay"});
  viewCombo_->setToolTip(
      tr("How the difference is drawn. signed-color paints positive red and negative blue; overlay "
         "lays the difference on the base image."));
  gainSpinBox_->setRange(0.1, 32.0);
  gainSpinBox_->setSingleStep(0.1);
  gainSpinBox_->setToolTip(tr("Multiplies the difference before it is drawn"));
  thresholdSpinBox_->setRange(0.0, 255.0);
  thresholdSpinBox_->setDecimals(1);
  thresholdSpinBox_->setToolTip(
      tr("Threshold (0-255 luma; delta-e: in ΔE units; ssim: pixels with a similarity "
         "below it are counted)"));
  autoRangeCheckBox_->setToolTip(tr("Stretch the largest difference to 255"));
  // 並びは、`WipeOrientation` の順。基準は、左 / 上。
  wipeDirectionCombo_->addItems({tr("Left | Right"), tr("Top | Bottom")});
  wipeDirectionCombo_->setToolTip(
      tr("Where the line cuts. The base image is on the left (or top)."));
  wipePositionSpinBox_->setRange(0.0, 100.0);
  wipePositionSpinBox_->setDecimals(1);
  wipePositionSpinBox_->setSingleStep(5.0);
  wipePositionSpinBox_->setSuffix(QStringLiteral(" %"));
  wipePositionSpinBox_->setToolTip(tr("Position of the line (you can also drag it)"));
  // 並びは、`DiffSizeMatch` の順。
  sizeMatchCombo_->addItems(
      {tr("Overlap only"), tr("Resize to base"), tr("Resize to target"), tr("Resize to larger")});
  sizeMatchCombo_->setToolTip(
      tr("When the sizes differ. Overlap only compares the part both images cover, aligned at the "
         "top left. The others resize one image to the size of the base, the target, or the larger "
         "one."));

  form->addRow(tr("Base"), baseCombo_);
  form->addRow(tr("Compared"), targetCombo_);
  form->addRow(tr("View"), viewCombo_);
  form->addRow(tr("Gain"), gainSpinBox_);
  form->addRow(tr("Threshold"), thresholdSpinBox_);
  form->addRow(QString(), autoRangeCheckBox_);
  form->addRow(tr("Cut"), wipeDirectionCombo_);
  form->addRow(tr("Position"), wipePositionSpinBox_);
  form->addRow(tr("Size"), sizeMatchCombo_);
  settingsForm_ = form;

  // 画像の邪魔にならないよう、小さく控えめにする。
  QFont smallFont = font();
  smallFont.setPointSize(kBarFontPoints);
  setFont(smallFont);
  settingsPopup_->setFont(smallFont);
  setStyleSheet(
      "#diffBar { background: rgba(33, 35, 39, 225); border-top: 1px solid #34373d; }"
      "#diffBar QLabel { color: #969ba4; }"
      "#diffBar QComboBox { min-height: 18px; }"
      "#diffSettingsPopup { background: #212327; border: 1px solid #34373d; }"
      "#diffSettingsPopup QLabel, #diffSettingsPopup QCheckBox { color: #c4c8d0; }"
      "#diffSettingsPopup QComboBox, #diffSettingsPopup QDoubleSpinBox { min-height: 18px; }");

  const auto changed = [this]() { emitChange(); };
  connect(baseCombo_, &QComboBox::currentIndexChanged, this, changed);
  connect(targetCombo_, &QComboBox::currentIndexChanged, this, changed);
  connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
    // 既定の閾値は、輝度 (8) と ΔE (2.3 は、ほぼ気付く差)
    // で尺度が違う。既定のままなら、尺度に合わせる。
    updateRowVisibility();
    const QString mode = modeCombo_->currentText();
    // 閾値が、直前のモードの既定のままなら、新しいモードの既定へ替える (尺度が違うため)。
    if (!updating_ && thresholdSpinBox_->value() == defaultThresholdFor(previousMode_)) {
      thresholdSpinBox_->setValue(defaultThresholdFor(mode));
    }
    // SSIM の閾値は、0-1 の値のため、桁を増やす。
    // 桁を減らすと、値が丸まる (0.75 が 0.8 になる)。丸まらないときだけ、減らす。
    const double scaled = thresholdSpinBox_->value() * 10.0;
    const bool roundsCleanly = std::abs(scaled - std::round(scaled)) < 1e-9;
    thresholdSpinBox_->setDecimals(mode == QStringLiteral("ssim") || !roundsCleanly ? 3 : 1);
    thresholdSpinBox_->setSingleStep(mode == QStringLiteral("ssim") ? 0.01 : 1.0);
    previousMode_ = mode;
    emitChange();
  });
  connect(viewCombo_, &QComboBox::currentIndexChanged, this, changed);
  connect(sizeMatchCombo_, &QComboBox::currentIndexChanged, this, changed);
  connect(wipeDirectionCombo_, &QComboBox::currentIndexChanged, this, changed);
  connect(wipePositionSpinBox_, &QDoubleSpinBox::valueChanged, this, changed);
  connect(gainSpinBox_, &QDoubleSpinBox::valueChanged, this, changed);
  connect(thresholdSpinBox_, &QDoubleSpinBox::valueChanged, this, changed);
  connect(autoRangeCheckBox_, &QCheckBox::toggled, this, changed);
  connect(settingsButton_, &QToolButton::clicked, this, &DiffBarWidget::showSettings);
  connect(closeButton_, &QToolButton::clicked, this, &DiffBarWidget::closeRequested);
}

void DiffBarWidget::updateRowVisibility() {
  const bool wipe = modeCombo_->currentText() == QStringLiteral("wipe");
  settingsForm_->setRowVisible(viewCombo_, !wipe);
  settingsForm_->setRowVisible(gainSpinBox_, !wipe);
  settingsForm_->setRowVisible(thresholdSpinBox_, !wipe);
  settingsForm_->setRowVisible(autoRangeCheckBox_, !wipe);
  settingsForm_->setRowVisible(wipeDirectionCombo_, wipe);
  settingsForm_->setRowVisible(wipePositionSpinBox_, wipe);
}

void DiffBarWidget::showSettings() {
  updateRowVisibility();
  settingsPopup_->adjustSize();
  // 帯の上へ、右端をそろえて出す (帯は画像の面の下端にあるため、下へは出せない)。
  QPoint position = settingsButton_->mapToGlobal(
      QPoint(settingsButton_->width() - settingsPopup_->width(), -settingsPopup_->height()));
  if (const QScreen *screen = settingsButton_->screen()) {
    const QRect area = screen->availableGeometry();
    position.setX(std::clamp(position.x(), area.left(),
                             std::max(area.left(), area.right() - settingsPopup_->width())));
    position.setY(std::clamp(position.y(), area.top(),
                             std::max(area.top(), area.bottom() - settingsPopup_->height())));
  }
  settingsPopup_->move(position);
  settingsPopup_->show();
}

void DiffBarWidget::setDiff(const heiran::domain::CellDiff &diff, const QStringList &cellLabels,
                            int selfCell, bool ownsImage) {
  updating_ = true;
  ownsImage_ = ownsImage;
  // 同じ並びなら、選択肢を作り直さない (操作中の入力を壊さないため)。
  const auto fill = [&](QComboBox *combo, int current) {
    QStringList items;
    QList<int> cells;
    for (int cell = 0; cell < cellLabels.size(); ++cell) {
      if (cell == selfCell) {
        continue;
      }
      items << cellLabels.at(cell);
      cells << cell;
    }
    bool same = combo->count() == items.size();
    for (int row = 0; same && row < items.size(); ++row) {
      same = combo->itemText(row) == items.at(row) && combo->itemData(row).toInt() == cells.at(row);
    }
    if (!same) {
      combo->clear();
      for (int row = 0; row < items.size(); ++row) {
        combo->addItem(items.at(row), cells.at(row));
      }
    }
    combo->setCurrentIndex(combo->findData(current));
  };
  fill(baseCombo_, diff.baseCell);
  fill(targetCombo_, diff.targetCell);
  // 比べる側を選べるのは、自身の画像を持たないセルだけ。
  settingsForm_->setRowVisible(targetCombo_, !ownsImage);

  modeCombo_->setCurrentText(heiran::domain::toDiffModeString(diff.settings.mode));
  previousMode_ = modeCombo_->currentText();
  thresholdSpinBox_->setDecimals(previousMode_ == QStringLiteral("ssim") ? 3 : 1);
  viewCombo_->setCurrentText(heiran::domain::toDiffViewModeString(diff.settings.view));
  sizeMatchCombo_->setCurrentIndex(static_cast<int>(diff.settings.sizeMatch));
  wipeDirectionCombo_->setCurrentIndex(static_cast<int>(diff.settings.wipeOrientation));
  wipePositionSpinBox_->setValue(diff.settings.wipePosition * 100.0);
  updateRowVisibility();
  gainSpinBox_->setValue(diff.settings.gain);
  thresholdSpinBox_->setValue(diff.settings.threshold);
  autoRangeCheckBox_->setChecked(diff.settings.autoRange);
  updating_ = false;
}

void DiffBarWidget::setResult(const std::optional<heiran::domain::DiffMetrics> &metrics,
                              const QString &message) {
  auto *label = static_cast<ElidedLabel *>(resultLabel_);
  label->setFullText(metrics ? metricsText(*metrics) : message);
  const bool hasChanges = metrics && !metrics->wipe && metrics->exceededPixelCount > 0;
  previousRegion_->setEnabled(hasChanges);
  nextRegion_->setEnabled(hasChanges);
  blinkButton_->setEnabled(metrics.has_value());
}

void DiffBarWidget::setRegionProgress(qsizetype index, qsizetype count) {
  static_cast<ElidedLabel *>(regionLabel_)
      ->setFullText(index >= 0 ? tr("Difference %1 of %2").arg(index + 1).arg(count) : QString());
}

void DiffBarWidget::setBlinking(bool enabled) {
  const QSignalBlocker blocker(blinkButton_);
  blinkButton_->setChecked(enabled);
}

heiran::domain::CellDiff DiffBarWidget::currentDiff() const {
  heiran::domain::CellDiff diff;
  const QVariant base = baseCombo_->currentData();
  const QVariant target = targetCombo_->currentData();
  diff.baseCell = base.isValid() ? base.toInt() : -1;
  diff.targetCell = ownsImage_ || !target.isValid() ? -1 : target.toInt();
  diff.settings.mode = heiran::domain::diffModeFromString(modeCombo_->currentText());
  diff.settings.view = heiran::domain::diffViewModeFromString(viewCombo_->currentText());
  diff.settings.gain = gainSpinBox_->value();
  diff.settings.threshold = thresholdSpinBox_->value();
  diff.settings.autoRange = autoRangeCheckBox_->isChecked();
  diff.settings.sizeMatch =
      static_cast<heiran::domain::DiffSizeMatch>(std::max(0, sizeMatchCombo_->currentIndex()));
  diff.settings.wipeOrientation = static_cast<heiran::domain::WipeOrientation>(
      std::max(0, wipeDirectionCombo_->currentIndex()));
  diff.settings.wipePosition = wipePositionSpinBox_->value() / 100.0;
  return diff;
}

QString DiffBarWidget::resultText() const {
  return static_cast<ElidedLabel *>(resultLabel_)->fullText();
}

bool DiffBarWidget::isInteracting() const {
  // 設定の一覧を開いている間は、マウスがセルの外へ出ても、隠さない。
  if (settingsPopup_->isVisible() || QApplication::activePopupWidget() != nullptr) {
    return true;
  }
  const QWidget *focus = QApplication::focusWidget();
  return focus != nullptr && (isAncestorOf(focus) || settingsPopup_->isAncestorOf(focus));
}

void DiffBarWidget::emitChange() {
  if (updating_) {
    return;
  }
  const auto diff = currentDiff();
  if (diff.baseCell < 0) {
    return;
  }
  Q_EMIT diffChanged(diff);
}

}  // namespace heiran::ui
