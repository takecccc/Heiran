#include "heiran/ui/DiffWindow.hpp"

#include "heiran/domain/DiffMetrics.hpp"
#include "heiran/ui/DiffRenderer.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace heiran::ui {

namespace {

QString imageLabelForSelector(const AnalysisImage &image, int cellIndex) {
  return QCoreApplication::translate("DiffWindow", "Cell %1 - %2")
      .arg(cellIndex + 1)
      .arg(image.label);
}

}  // namespace

DiffWindow::DiffWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("DiffWindow", "Diff"), parent) {
  resize(960, 720);

  auto *rootLayout = new QVBoxLayout(this);
  auto *controlsLayout = new QHBoxLayout;

  auto *selectorForm = new QFormLayout;
  baseCellCombo_ = new QComboBox(this);
  targetCellCombo_ = new QComboBox(this);
  modeCombo_ = new QComboBox(this);
  viewCombo_ = new QComboBox(this);
  gainSpinBox_ = new QDoubleSpinBox(this);
  thresholdSpinBox_ = new QDoubleSpinBox(this);
  autoRangeCheckBox_ = new QCheckBox(QCoreApplication::translate("DiffWindow", "Auto Range"), this);

  modeCombo_->addItems({"absolute", "signed", "binary", "delta-e", "ssim", "rgb-max"});
  viewCombo_->addItems({"grayscale", "signed-color", "overlay"});
  gainSpinBox_->setRange(0.1, 32.0);
  gainSpinBox_->setValue(1.0);
  gainSpinBox_->setSingleStep(0.1);
  thresholdSpinBox_->setRange(0.0, 255.0);
  thresholdSpinBox_->setValue(8.0);
  thresholdSpinBox_->setSingleStep(1.0);
  autoRangeCheckBox_->setChecked(true);

  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "Base"), baseCellCombo_);
  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "Target"), targetCellCombo_);
  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "Mode"), modeCombo_);
  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "View"), viewCombo_);
  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "Gain"), gainSpinBox_);
  selectorForm->addRow(QCoreApplication::translate("DiffWindow", "Threshold"), thresholdSpinBox_);
  selectorForm->addRow(QString(), autoRangeCheckBox_);
  controlsLayout->addLayout(selectorForm);
  controlsLayout->addStretch(1);
  rootLayout->addLayout(controlsLayout);

  previewLabel_ = new QLabel(this);
  previewLabel_->setMinimumSize(480, 360);
  previewLabel_->setAlignment(Qt::AlignCenter);
  previewLabel_->setText(QCoreApplication::translate("DiffWindow", "No comparable images"));
  rootLayout->addWidget(previewLabel_, 1);

  statsLabel_ = new QLabel(this);
  statsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rootLayout->addWidget(statsLabel_);

  statusLabel_ = new QLabel(this);
  rootLayout->addWidget(statusLabel_);

  const auto refreshHandler = [this]() { refreshDiffImage(); };
  connect(baseCellCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, refreshHandler);
  connect(targetCellCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, refreshHandler);
  // 閾値の尺度が、SSIM (0-1 の類似度) だけ違う。既定のままなら、尺度に合わせる。
  connect(modeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
    const bool ssim = index == 4;
    QSignalBlocker blocker(thresholdSpinBox_);
    if (ssim && thresholdSpinBox_->value() == 8.0) {
      thresholdSpinBox_->setDecimals(3);
      thresholdSpinBox_->setValue(0.9);
    } else if (!ssim && thresholdSpinBox_->value() == 0.9) {
      thresholdSpinBox_->setDecimals(1);
      thresholdSpinBox_->setValue(8.0);
    } else {
      thresholdSpinBox_->setDecimals(ssim ? 3 : 1);
    }
  });
  connect(modeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, refreshHandler);
  connect(viewCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, refreshHandler);
  connect(gainSpinBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshHandler);
  connect(thresholdSpinBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          refreshHandler);
  connect(autoRangeCheckBox_, &QCheckBox::checkStateChanged, this, refreshHandler);
}

void DiffWindow::analysisImagesChanged() {
  rebuildCellSelectors();
  refreshDiffImage();
}

void DiffWindow::rebuildCellSelectors() {
  const int imageCount = static_cast<int>(analysisImages().size());
  const int lastIndex = std::max(0, imageCount - 1);
  const int baseIndex = std::clamp(baseCellCombo_->currentIndex(), 0, lastIndex);
  const int targetIndex = std::clamp(targetCellCombo_->currentIndex(), 0, lastIndex);

  baseCellCombo_->blockSignals(true);
  targetCellCombo_->blockSignals(true);
  baseCellCombo_->clear();
  targetCellCombo_->clear();

  for (int cellIndex = 0; cellIndex < imageCount; ++cellIndex) {
    const QString label = imageLabelForSelector(analysisImages().at(cellIndex), cellIndex);
    baseCellCombo_->addItem(label);
    targetCellCombo_->addItem(label);
  }

  if (imageCount > 0) {
    baseCellCombo_->setCurrentIndex(std::min(baseIndex, imageCount - 1));
    if (imageCount >= 2) {
      targetCellCombo_->setCurrentIndex(
          targetIndex == baseCellCombo_->currentIndex()
              ? std::min(baseCellCombo_->currentIndex() + 1, imageCount - 1)
              : std::min(targetIndex, imageCount - 1));
    } else {
      targetCellCombo_->setCurrentIndex(0);
    }
  }

  baseCellCombo_->blockSignals(false);
  targetCellCombo_->blockSignals(false);
}

void DiffWindow::refreshDiffImage() {
  if (analysisImages().size() < 2 || baseCellCombo_->currentIndex() < 0 ||
      targetCellCombo_->currentIndex() < 0) {
    previewLabel_->setPixmap(QPixmap());
    previewLabel_->setText(QCoreApplication::translate("DiffWindow", "No comparable images"));
    statsLabel_->setText(QString());
    setStatusMessage(QCoreApplication::translate("DiffWindow", "Select at least two images."));
    return;
  }

  const int baseIndex = baseCellCombo_->currentIndex();
  const int targetIndex = targetCellCombo_->currentIndex();
  if (baseIndex == targetIndex) {
    previewLabel_->setPixmap(QPixmap());
    previewLabel_->setText(QCoreApplication::translate("DiffWindow", "Choose different cells"));
    statsLabel_->setText(QString());
    setStatusMessage(
        QCoreApplication::translate("DiffWindow", "Base and target must be different."));
    return;
  }

  const auto &baseAnalysisImage = analysisImages().at(baseIndex);
  const auto &targetAnalysisImage = analysisImages().at(targetIndex);
  QImage baseImage = baseAnalysisImage.image;
  QImage targetImage = targetAnalysisImage.image;
  // 切り出した場合、走査座標は切り出し後の原点基準になる。
  // 解析用バッファは表示座標で引くため、原点を足し戻す。
  QPoint baseOrigin(0, 0);
  QPoint targetOrigin(0, 0);
  if (baseImage.isNull() || targetImage.isNull()) {
    previewLabel_->setPixmap(QPixmap());
    previewLabel_->setText(QCoreApplication::translate("DiffWindow", "No comparable images"));
    statsLabel_->setText(QString());
    setStatusMessage(
        QCoreApplication::translate("DiffWindow", "Base or target image is not available."));
    return;
  }

  if (baseAnalysisImage.selectionRect && targetAnalysisImage.selectionRect &&
      !baseAnalysisImage.selectionRect->isEmpty() &&
      !targetAnalysisImage.selectionRect->isEmpty()) {
    const QRect baseSelectionRect = *baseAnalysisImage.selectionRect;
    const QRect targetSelectionRect = *targetAnalysisImage.selectionRect;
    if (baseSelectionRect.size() != targetSelectionRect.size()) {
      previewLabel_->setPixmap(QPixmap());
      previewLabel_->setText(QCoreApplication::translate("DiffWindow", "Selection sizes differ"));
      statsLabel_->setText(QCoreApplication::translate(
                               "DiffWindow", "Base selection: %1x%2  Target selection: %3x%4")
                               .arg(baseSelectionRect.width())
                               .arg(baseSelectionRect.height())
                               .arg(targetSelectionRect.width())
                               .arg(targetSelectionRect.height()));
      setStatusMessage(QCoreApplication::translate(
          "DiffWindow", "Diff over selections requires same-size projected selections."));
      return;
    }

    baseImage = baseImage.copy(baseSelectionRect);
    targetImage = targetImage.copy(targetSelectionRect);
    baseOrigin = baseSelectionRect.topLeft();
    targetOrigin = targetSelectionRect.topLeft();
  }

  if (baseImage.size() != targetImage.size()) {
    previewLabel_->setPixmap(QPixmap());
    previewLabel_->setText(QCoreApplication::translate("DiffWindow", "Image sizes differ"));
    statsLabel_->setText(QCoreApplication::translate("DiffWindow", "Base: %1x%2  Target: %3x%4")
                             .arg(baseImage.width())
                             .arg(baseImage.height())
                             .arg(targetImage.width())
                             .arg(targetImage.height()));
    setStatusMessage(
        QCoreApplication::translate("DiffWindow", "MVP supports diff only for same-size images."));
    return;
  }

  heiran::domain::DiffSettings settings;
  settings.mode = diffModeFromIndex(modeCombo_->currentIndex());
  settings.view = diffViewModeFromIndex(viewCombo_->currentIndex());
  settings.gain = gainSpinBox_->value();
  settings.threshold = thresholdSpinBox_->value();
  settings.autoRange = autoRangeCheckBox_->isChecked();

  const DiffOutput diff = renderDiff(
      {.image = baseImage, .sampler = baseAnalysisImage.sampler, .origin = baseOrigin},
      {.image = targetImage, .sampler = targetAnalysisImage.sampler, .origin = targetOrigin},
      settings);
  if (!diff.ok) {
    previewLabel_->setPixmap(QPixmap());
    previewLabel_->setText(QCoreApplication::translate("DiffWindow", "No comparable images"));
    statsLabel_->setText(QString());
    setStatusMessage(diff.error);
    return;
  }
  const QImage &diffImage = diff.image;
  const heiran::domain::DiffMetrics &metrics = diff.metrics;
  const double maxAbsDiff = metrics.maxAbsDiff;

  previewLabel_->setText(QString());
  previewLabel_->setPixmap(QPixmap::fromImage(diffImage).scaled(
      previewLabel_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));

  const std::int64_t totalPixels = metrics.pixelCount;
  const double thresholdRatio =
      totalPixels > 0 ? (100.0 * static_cast<double>(metrics.exceededPixelCount) / totalPixels)
                      : 0.0;
  // 輝度 (0-255 の尺度) での指標。差が無いときの PSNR は無限大。
  const QString psnrText =
      metrics.psnrIsInfinite
          ? QString("inf")
          : QCoreApplication::translate("DiffWindow", "%1 dB").arg(metrics.psnr, 0, 'f', 2);

  if (metrics.ssim) {
    statsLabel_->setText(QCoreApplication::translate(
                             "DiffWindow", "base=%1  target=%2\nSSIM (luma): mean=%3  min=%4\n"
                                           "pixels below threshold=%5 / %6 (%7%)")
                             .arg(analysisImages().at(baseIndex).label)
                             .arg(analysisImages().at(targetIndex).label)
                             .arg(metrics.ssimMean, 0, 'f', 4)
                             .arg(metrics.ssimMin, 0, 'f', 3)
                             .arg(metrics.exceededPixelCount)
                             .arg(totalPixels)
                             .arg(thresholdRatio, 0, 'f', 2));
    setStatusMessage(QCoreApplication::translate("DiffWindow", "Diff image updated."));
    return;
  }
  if (metrics.colorDifference) {
    statsLabel_->setText(
        QCoreApplication::translate("DiffWindow",
                                    "base=%1  target=%2\nCIEDE2000: max=%3  mean=%4\n"
                                    "different pixel count=%5 / %6  threshold exceeded ratio=%7%")
            .arg(analysisImages().at(baseIndex).label)
            .arg(analysisImages().at(targetIndex).label)
            .arg(maxAbsDiff, 0, 'f', 2)
            .arg(metrics.meanAbsDiff, 0, 'f', 2)
            .arg(metrics.exceededPixelCount)
            .arg(totalPixels)
            .arg(thresholdRatio, 0, 'f', 2));
    setStatusMessage(QCoreApplication::translate("DiffWindow", "Diff image updated."));
    return;
  }
  statsLabel_->setText(QCoreApplication::translate(
                           "DiffWindow",
                           "base=%1  target=%2\nmax absolute diff=%3  mean absolute diff=%4  "
                           "mean signed diff=%5\nRMSE=%6  PSNR=%7 (luma)\n"
                           "different pixel count=%8 / %9  threshold exceeded ratio=%10%")
                           .arg(analysisImages().at(baseIndex).label)
                           .arg(analysisImages().at(targetIndex).label)
                           .arg(maxAbsDiff, 0, 'f', 2)
                           .arg(metrics.meanAbsDiff, 0, 'f', 2)
                           .arg(metrics.meanSignedDiff, 0, 'f', 2)
                           .arg(metrics.rmse, 0, 'f', 2)
                           .arg(psnrText)
                           .arg(metrics.exceededPixelCount)
                           .arg(totalPixels)
                           .arg(thresholdRatio, 0, 'f', 2));
  setStatusMessage(QCoreApplication::translate("DiffWindow", "Diff image updated."));
}

void DiffWindow::setStatusMessage(const QString &message) {
  statusLabel_->setText(message);
}

heiran::domain::DiffMode DiffWindow::diffModeFromIndex(int index) {
  switch (index) {
    case 1:
      return heiran::domain::DiffMode::Signed;
    case 2:
      return heiran::domain::DiffMode::Binary;
    case 3:
      return heiran::domain::DiffMode::DeltaE;
    case 4:
      return heiran::domain::DiffMode::Ssim;
    case 5:
      return heiran::domain::DiffMode::RgbMax;
    default:
      return heiran::domain::DiffMode::Absolute;
  }
}

heiran::domain::DiffViewMode DiffWindow::diffViewModeFromIndex(int index) {
  switch (index) {
    case 1:
      return heiran::domain::DiffViewMode::SignedColor;
    case 2:
      return heiran::domain::DiffViewMode::Overlay;
    default:
      return heiran::domain::DiffViewMode::Grayscale;
  }
}

}  // namespace heiran::ui
