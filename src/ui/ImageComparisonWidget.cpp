#include "heiran/ui/ImageComparisonWidget.hpp"

#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/DiffRenderer.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPromise>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <utility>

namespace heiran::ui {
namespace {

using heiran::app::FileComparisonStatus;
using heiran::domain::DiffComparisonBasis;
using heiran::domain::DiffMode;
using heiran::domain::DiffSizeMatch;

constexpr int kDiffCell = 2;
enum Display { SideBySide, SideBySideHighlighted, Highlight, Blink };
enum HighlightMethod { Steady, Blinking };
enum Filter { AllFiles, Changes, Modified, LeftOnly, RightOnly };
constexpr int kThresholdPrecision = 1000;

bool isSideBySide(int display) {
  return display == SideBySide || display == SideBySideHighlighted;
}

template <typename SpinBox> void syncSlider(QSlider *slider, const SpinBox *spin, int precision) {
  const QSignalBlocker blocker(slider);
  slider->setRange(qRound(double(spin->minimum()) * precision),
                   qRound(double(spin->maximum()) * precision));
  slider->setSingleStep(qRound(double(spin->singleStep()) * precision));
  slider->setPageStep(qMax(slider->singleStep(), (slider->maximum() - slider->minimum()) / 10));
  slider->setValue(qRound(double(spin->value()) * precision));
}

template <typename SpinBox>
QSlider *addSliderField(QFormLayout *form, const QString &label, SpinBox *spin,
                        const char *objectName, int precision = 1) {
  auto *field = new QWidget(spin->parentWidget());
  auto *row = new QHBoxLayout(field);
  row->setContentsMargins(0, 0, 0, 0);
  auto *slider = new QSlider(Qt::Horizontal, field);
  slider->setObjectName(objectName);
  slider->setAccessibleName(label);
  spin->setAccessibleName(label);
  syncSlider(slider, spin, precision);
  const auto reflectSlider = [spin, precision](int value) {
    spin->setValue(static_cast<decltype(spin->value())>(double(value) / precision));
  };
  QObject::connect(slider, &QSlider::valueChanged, spin, reflectSlider);
  QObject::connect(spin, &SpinBox::valueChanged, slider, [slider, precision](auto value) {
    const QSignalBlocker blocker(slider);
    slider->setValue(qRound(double(value) * precision));
  });
  row->addWidget(slider, 1);
  row->addWidget(spin);
  field->setFocusProxy(slider);
  form->addRow(label, field);
  return slider;
}

QLabel *messageLabel(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setMinimumWidth(0);
  return label;
}

}  // namespace

ImageComparisonWidget::ImageComparisonWidget(QWidget *parent)
    : QWidget(parent), highlightTimer_(this) {
  setObjectName("imageComparison");
  setFocusPolicy(Qt::StrongFocus);
  setWindowTitle(tr("Image Difference - Heiran"));
  auto *root = new QVBoxLayout(this);
  auto *splitter = new QSplitter(this);
  splitter->setChildrenCollapsible(false);
  root->addWidget(splitter);
  auto *sidebarScroll = new QScrollArea(splitter);
  sidebarScroll->setFrameShape(QFrame::NoFrame);
  sidebarScroll->setWidgetResizable(true);
  auto *sidebar = new QWidget;
  sidebarScroll->setWidget(sidebar);
  auto *left = new QVBoxLayout(sidebar);
  left->setContentsMargins(0, 0, 8, 0);
  paths_ = messageLabel(sidebar);
  left->addWidget(paths_);

  folderControls_ = new QWidget(sidebar);
  auto *files = new QVBoxLayout(folderControls_);
  files->setContentsMargins(0, 0, 0, 0);
  filter_ = new QComboBox(folderControls_);
  filter_->setObjectName("folderComparisonFilter");
  filter_->addItems({tr("All files"), tr("Changes only"), tr("Modified files"), tr("Left only"),
                     tr("Right only")});
  filter_->setCurrentIndex(Changes);
  auto *reload = new QPushButton(themedIcon("refresh-cw"), tr("Refresh"), folderControls_);
  auto *filterRow = new QHBoxLayout;
  filterRow->addWidget(filter_, 1);
  filterRow->addWidget(reload);
  files->addLayout(filterRow);
  entries_ = new QTreeWidget(folderControls_);
  entries_->setObjectName("folderComparisonEntries");
  entries_->setHeaderLabels({tr("Relative path"), tr("File status")});
  entries_->setRootIsDecorated(false);
  entries_->setUniformRowHeights(true);
  entries_->header()->setStretchLastSection(false);
  entries_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  entries_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  files->addWidget(entries_, 1);
  folderStatus_ = messageLabel(folderControls_);
  files->addWidget(folderStatus_);
  previousFile_ = new QPushButton(themedIcon("arrow-left"), tr("Previous changed file"), sidebar);
  previousFile_->setObjectName("previousChangedFile");
  nextFile_ = new QPushButton(themedIcon("arrow-right"), tr("Next changed file"), sidebar);
  nextFile_->setObjectName("nextChangedFile");
  files->addWidget(previousFile_);
  files->addWidget(nextFile_);
  externalViewer_ =
      new QPushButton(themedIcon("external-link"), tr("Open in External Diff Viewer..."), sidebar);
  externalViewer_->setObjectName("openExternalDiffViewer");
  externalViewer_->setEnabled(false);
  auto *chooseViewer = new QToolButton(folderControls_);
  chooseViewer->setIcon(themedIcon("settings"));
  chooseViewer->setToolTip(tr("Choose an External Diff Viewer"));
  chooseViewer->setAccessibleName(chooseViewer->toolTip());
  auto *viewerRow = new QHBoxLayout;
  viewerRow->addWidget(externalViewer_, 1);
  viewerRow->addWidget(chooseViewer);
  files->addLayout(viewerRow);
  left->addWidget(folderControls_, 1);
  folderControls_->hide();

  auto *form = new QFormLayout;
  display_ = new QComboBox(sidebar);
  display_->setObjectName("imageComparisonDisplay");
  display_->addItem(themedIcon("panel-left"), tr("Side by side"));
  display_->addItem(themedIcon("scan-line"), tr("Side by side with highlights"));
  display_->addItem(themedIcon("contrast"), tr("Highlight differences"));
  display_->addItem(themedIcon("layers"), tr("Blink"));
  form->addRow(tr("Display"), display_);
  showDifferenceMap_ = new QCheckBox(tr("Show difference map"), sidebar);
  showDifferenceMap_->setIcon(themedIcon("layout-grid"));
  showDifferenceMap_->setObjectName("imageComparisonShowDifferenceMap");
  form->addRow(showDifferenceMap_);
  mapAppearance_ = new QWidget(sidebar);
  auto *mapForm = new QFormLayout(mapAppearance_);
  mapForm->setContentsMargins(0, 0, 0, 0);
  mapGain_ = new QComboBox(mapAppearance_);
  mapGain_->setObjectName("imageComparisonMapGain");
  for (const int gain : {1, 2, 4, 8, 16, 32}) {
    mapGain_->addItem(tr("%1×").arg(gain), gain);
  }
  mapGain_->setToolTip(tr("Brightens the map without changing which differences are detected."));
  mapForm->addRow(tr("Map brightness"), mapGain_);
  mapLegend_ = messageLabel(mapAppearance_);
  mapLegend_->setObjectName("imageComparisonMapLegend");
  mapForm->addRow(mapLegend_);
  form->addRow(mapAppearance_);
  calculation_ = new QComboBox(sidebar);
  calculation_->setObjectName("imageComparisonCalculation");
  calculation_->addItem(tr("RGB difference (maximum channel)"), static_cast<int>(DiffMode::RgbMax));
  calculation_->addItem(tr("Brightness"), static_cast<int>(DiffMode::Absolute));
  calculation_->addItem(tr("Color difference (ΔE00)"), static_cast<int>(DiffMode::DeltaE));
  calculation_->addItem(tr("Structural similarity (SSIM)"), static_cast<int>(DiffMode::Ssim));
  form->addRow(tr("Compare by"), calculation_);
  comparisonBasis_ = new QComboBox(sidebar);
  comparisonBasis_->setObjectName("imageComparisonBasis");
  comparisonBasis_->addItem(tr("Pixel values"), static_cast<int>(DiffComparisonBasis::PixelValues));
  comparisonBasis_->addItem(tr("Composite on white"),
                            static_cast<int>(DiffComparisonBasis::WhiteBackground));
  comparisonBasis_->addItem(tr("Composite on black"),
                            static_cast<int>(DiffComparisonBasis::BlackBackground));
  form->addRow(tr("Transparency comparison method"), comparisonBasis_);
  ignoreTransparentRgb_ = new QCheckBox(tr("Ignore fully transparent color"), sidebar);
  ignoreTransparentRgb_->setObjectName("imageComparisonIgnoreTransparentRgb");
  ignoreTransparentRgb_->setToolTip(
      tr("Ignore RGB differences where both images are fully transparent."));
  ignoreTransparentRgb_->setChecked(true);
  form->addRow(ignoreTransparentRgb_);
  const heiran::domain::DiffSettings defaultDiff;
  threshold_ = new QDoubleSpinBox(sidebar);
  threshold_->setObjectName("imageComparisonThreshold");
  threshold_->setDecimals(3);
  threshold_->setRange(0.0, 255.0);
  threshold_->setKeyboardTracking(false);
  threshold_->setValue(defaultDiff.threshold);
  auto *thresholdSlider = addSliderField(form, tr("Threshold"), threshold_,
                                         "imageComparisonThresholdSlider", kThresholdPrecision);
  auto *thresholdHint = messageLabel(sidebar);
  thresholdHint->setObjectName("imageComparisonThresholdHint");
  form->addRow(thresholdHint);
  blockSize_ = new QSpinBox(sidebar);
  blockSize_->setObjectName("imageComparisonBlockSize");
  blockSize_->setRange(1, 128);
  blockSize_->setSuffix(QStringLiteral(" px"));
  blockSize_->setKeyboardTracking(false);
  blockSize_->setValue(defaultDiff.highlightBlockSize);
  addSliderField(form, tr("Block size"), blockSize_, "imageComparisonBlockSizeSlider");
  highlightMethod_ = new QComboBox(sidebar);
  highlightMethod_->setObjectName("imageComparisonHighlightMethod");
  highlightMethod_->addItem(themedIcon("scan-line"), tr("Steady"));
  highlightMethod_->addItem(themedIcon("sun"), tr("Blink"));
  form->addRow(tr("Highlight method"), highlightMethod_);
  highlightTransparency_ = new QSpinBox(sidebar);
  highlightTransparency_->setObjectName("imageComparisonHighlightTransparency");
  highlightTransparency_->setRange(0, 100);
  highlightTransparency_->setSuffix(QStringLiteral("%"));
  highlightTransparency_->setSingleStep(5);
  highlightTransparency_->setKeyboardTracking(false);
  highlightTransparency_->setValue(100 - defaultDiff.highlightOpacity);
  addSliderField(form, tr("Highlight transparency"), highlightTransparency_,
                 "imageComparisonHighlightTransparencySlider");
  sizeMatch_ = new QComboBox(sidebar);
  sizeMatch_->addItems(
      {tr("Align top left"), tr("Resize to left"), tr("Resize to right"), tr("Resize to larger")});
  form->addRow(tr("Image size"), sizeMatch_);
  zoom_ = new QDoubleSpinBox(sidebar);
  zoom_->setObjectName("imageComparisonZoom");
  zoom_->setRange(0.01, 1000000.0);
  zoom_->setDecimals(2);
  zoom_->setSuffix(QStringLiteral("%"));
  zoom_->setSingleStep(10.0);
  zoom_->setKeyboardTracking(false);
  zoom_->setValue(100.0);
  form->addRow(tr("Zoom"), zoom_);
  left->addLayout(form);
  auto *navigation = new QHBoxLayout;
  previousDifference_ =
      new QPushButton(themedIcon("arrow-left"), tr("Previous difference"), sidebar);
  nextDifference_ = new QPushButton(themedIcon("arrow-right"), tr("Next difference"), sidebar);
  nextDifference_->setObjectName("nextComparisonDifference");
  const auto setShortcut = [](QPushButton *button, const QKeySequence &shortcut) {
    button->setShortcut(shortcut);
    button->setToolTip(button->text() + QStringLiteral(" (") +
                       shortcut.toString(QKeySequence::NativeText) + u')');
  };
  setShortcut(previousDifference_, QKeySequence(Qt::Key_F7));
  setShortcut(nextDifference_, QKeySequence(Qt::Key_F8));
  setShortcut(previousFile_, QKeySequence(Qt::CTRL | Qt::Key_F7));
  setShortcut(nextFile_, QKeySequence(Qt::CTRL | Qt::Key_F8));
  navigation->addWidget(previousDifference_);
  navigation->addWidget(nextDifference_);
  left->addLayout(navigation);
  differences_ = messageLabel(sidebar);
  differences_->setObjectName("imageComparisonDetectionStatus");
  pixelStatus_ = messageLabel(sidebar);
  pixelStatus_->setObjectName("imageComparisonPixelStatus");
  left->addWidget(differences_);
  left->addWidget(pixelStatus_);
  auto *footer = new QHBoxLayout;
  fit_ = new QPushButton(themedIcon("scan"), tr("Fit"), sidebar);
  auto *close = new QPushButton(themedIcon("x"), tr("End Comparison"), sidebar);
  close->setObjectName("endImageComparison");
  footer->addWidget(fit_);
  footer->addStretch();
  footer->addWidget(close);
  left->addLayout(footer);

  preview_ = new QStackedWidget(splitter);
  previewMessage_ = messageLabel(preview_);
  previewMessage_->setAlignment(Qt::AlignCenter);
  preview_->addWidget(previewMessage_);
  grid_ = new ViewerGridWidget(preview_);
  grid_->setComparisonPresentation();
  grid_->setAnimationPlaying(false);
  grid_->setFitOnPageChange(true);
  preview_->addWidget(grid_);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({330, 830});

  connect(close, &QPushButton::clicked, this, &ImageComparisonWidget::endRequested);
  connect(fit_, &QPushButton::clicked, this, [this]() {
    grid_->resetView();
    grid_->requestFitToCells();
  });
  connect(reload, &QPushButton::clicked, this, &ImageComparisonWidget::reloadFolders);
  connect(&folderTask_, &QFutureWatcherBase::finished, this,
          &ImageComparisonWidget::populateEntries);
  connect(filter_, &QComboBox::currentIndexChanged, this, &ImageComparisonWidget::updateFilter);
  connect(entries_, &QTreeWidget::currentItemChanged, this,
          &ImageComparisonWidget::showCurrentEntry);
  connect(previousFile_, &QPushButton::clicked, this, [this]() { stepFiles(-1); });
  connect(nextFile_, &QPushButton::clicked, this, [this]() { stepFiles(1); });
  connect(externalViewer_, &QPushButton::clicked, this, &ImageComparisonWidget::openExternalViewer);
  connect(chooseViewer, &QToolButton::clicked, this, [this]() { chooseExternalViewer(); });
  connect(display_, &QComboBox::currentIndexChanged, this,
          &ImageComparisonWidget::applyDisplayMode);
  connect(highlightMethod_, &QComboBox::currentIndexChanged, this,
          &ImageComparisonWidget::updateHighlights);
  connect(showDifferenceMap_, &QCheckBox::toggled, this, &ImageComparisonWidget::applyDisplayMode);
  connect(mapGain_, &QComboBox::currentIndexChanged, this, [this]() {
    grid_->setComparisonMapGain(mapGain_->currentData().toInt());
    updateMapLegend();
  });
  highlightTimer_.setObjectName("comparisonHighlightTimer");
  highlightTimer_.setInterval(500);
  connect(&highlightTimer_, &QTimer::timeout, this, [this]() {
    highlightPhaseVisible_ = !highlightPhaseVisible_;
    grid_->setComparisonHighlightsVisible(highlightPhaseVisible_);
  });
  const auto updateThreshold = [this, thresholdSlider, thresholdHint]() {
    const auto mode = static_cast<DiffMode>(calculation_->currentData().toInt());
    const QSignalBlocker blocker(threshold_);
    threshold_->setRange(0.0, mode == DiffMode::Ssim ? 1.0 : 255.0);
    threshold_->setSingleStep(mode == DiffMode::Ssim ? 0.01 : 1.0);
    threshold_->setValue(mode == DiffMode::Ssim ? 0.9 : mode == DiffMode::DeltaE ? 2.3 : 8.0);
    syncSlider(thresholdSlider, threshold_, kThresholdPrecision);
    QString explanation;
    switch (mode) {
      case DiffMode::Ssim:
        explanation = tr("Detects SSIM below this value (0–1).");
        break;
      case DiffMode::DeltaE:
        explanation = tr("Detects ΔE00 above this value.");
        break;
      case DiffMode::RgbMax:
        explanation = tr("Detects RGB differences above this value (0–255).");
        break;
      default:
        explanation = tr("Detects brightness differences above this value (0–255).");
        break;
    }
    thresholdHint->setText(explanation);
    threshold_->setAccessibleDescription(explanation);
    thresholdSlider->setAccessibleDescription(explanation);
    updateMapLegend();
    applyDiffSettings();
  };
  connect(calculation_, &QComboBox::currentIndexChanged, this, updateThreshold);
  updateThreshold();
  connect(sizeMatch_, &QComboBox::currentIndexChanged, this, [this]() {
    applyDiffSettings();
    if (display_->currentIndex() == Blink) {
      applyDisplayMode();
    }
  });
  connect(threshold_, &QDoubleSpinBox::valueChanged, this,
          &ImageComparisonWidget::applyDiffSettings);
  connect(blockSize_, &QSpinBox::valueChanged, this, &ImageComparisonWidget::applyDiffSettings);
  connect(highlightTransparency_, &QSpinBox::valueChanged, this,
          &ImageComparisonWidget::applyDiffSettings);
  connect(zoom_, &QDoubleSpinBox::valueChanged, this,
          [this](double percent) { grid_->setGlobalScale(percent / 100.0); });
  const auto refreshZoom = [this]() {
    if (zoomEditing_) {
      return;
    }
    const QSignalBlocker blocker(zoom_);
    zoom_->setValue(grid_->globalViewState().baseScale * 100.0);
  };
  connect(zoom_->findChild<QLineEdit *>(), &QLineEdit::textEdited, this,
          [this]() { zoomEditing_ = true; });
  connect(zoom_, &QDoubleSpinBox::editingFinished, this, [this, refreshZoom]() {
    zoomEditing_ = false;
    refreshZoom();
  });
  connect(grid_, &ViewerGridWidget::viewChanged, this, refreshZoom);
  connect(grid_, &ViewerGridWidget::statusChanged, this, refreshZoom);
  connect(comparisonBasis_, &QComboBox::currentIndexChanged, this, [this]() {
    TransparencyBackground background;
    const auto basis = comparisonBasis();
    if (basis != DiffComparisonBasis::PixelValues) {
      background.solidColor = basis == DiffComparisonBasis::WhiteBackground ? Qt::white : Qt::black;
    }
    grid_->setTransparencyBackground(background);
    applyDiffSettings();
    applyDisplayMode();
    updatePixelStatus();
  });
  connect(ignoreTransparentRgb_, &QCheckBox::toggled, this, [this]() {
    pixelTask_.cancel();
    pixelsStarted_ = false;
    pixelStatus_->clear();
    applyDiffSettings();
    checkPixels();
  });
  const auto jump = [this](int direction) {
    grid_->moveToDiffRegion(kDiffCell, direction);
    updateComparisonControls();
  };
  connect(previousDifference_, &QPushButton::clicked, this, [jump]() { jump(-1); });
  connect(nextDifference_, &QPushButton::clicked, this, [jump]() { jump(1); });
  connect(grid_, &ViewerGridWidget::cellDiffChanged, this,
          &ImageComparisonWidget::updateComparisonControls);
  connect(grid_, &ViewerGridWidget::imageContentChanged, this, &ImageComparisonWidget::checkPixels);
  connect(&pixelTask_, &QFutureWatcherBase::finished, this,
          &ImageComparisonWidget::updatePixelStatus);
  updateComparisonControls();
}

ImageComparisonWidget::~ImageComparisonWidget() {
  folderTask_.cancel();
  pixelTask_.cancel();
}

void ImageComparisonWidget::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
    event->accept();
    Q_EMIT endRequested();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ImageComparisonWidget::openImages(const QStringList &paths) {
  openPreparedImages(StartupPreparationController::prepareDiffImages(paths, QDir::currentPath()));
}

void ImageComparisonWidget::openPreparedImages(PreparedImageSession prepared,
                                               std::shared_ptr<DecodeScheduler> scheduler) {
  pixelTask_.cancel();
  pixelsStarted_ = false;
  blinkPending_ = false;
  pixelStatus_->clear();
  comparingImages_ = prepared.sourcePaths.size() == 2;
  if (leftRoot_.isEmpty()) {
    paths_->setText(prepared.sourcePaths.join('\n'));
  }
  preview_->setCurrentWidget(grid_);
  if (!comparingImages_) {
    grid_->setComparisonMapVisible(false);
  }
  grid_->setSession(prepared.session, 0, std::move(scheduler));
  if (!comparingImages_) {
    grid_->setSoloCell(prepared.session.cells[0].imageIndex >= 0 ? 0 : 1);
  } else {
    applyDiffSettings();
    applyDisplayMode();
  }
  grid_->requestFitToCells();
  updateComparisonControls();
  checkPixels();
}

void ImageComparisonWidget::openFolders(const QString &left, const QString &right) {
  leftRoot_ = left;
  rightRoot_ = right;
  setWindowTitle(tr("Folder Comparison - Heiran"));
  paths_->setText(tr("Left: %1\nRight: %2")
                      .arg(QDir::toNativeSeparators(left), QDir::toNativeSeparators(right)));
  folderControls_->show();
  reloadFolders();
}

void ImageComparisonWidget::reloadFolders() {
  folderTask_.cancel();
  {
    const QSignalBlocker blocker(entries_);
    entries_->clear();
    folderResult_ = {};
  }
  folderStatus_->setText(tr("Comparing files…"));
  filter_->setEnabled(false);
  previousFile_->setEnabled(false);
  nextFile_->setEnabled(false);
  externalViewer_->setEnabled(false);
  showMessage(tr("Comparing folders…"));
  folderTask_.setFuture(
      QtConcurrent::run([left = leftRoot_, right = rightRoot_](
                            QPromise<heiran::app::FolderComparisonResult> &promise) {
        auto result =
            heiran::app::compareFolders(left, right, [&]() { return promise.isCanceled(); });
        if (!promise.isCanceled()) {
          promise.addResult(std::move(result));
        }
      }));
}

QString ImageComparisonWidget::fileStatus(FileComparisonStatus status) const {
  switch (status) {
    case FileComparisonStatus::Identical:
      return tr("Identical");
    case FileComparisonStatus::Different:
      return tr("Modified");
    case FileComparisonStatus::LeftOnly:
      return tr("Left only");
    case FileComparisonStatus::RightOnly:
      return tr("Right only");
    case FileComparisonStatus::Unreadable:
      return tr("Unreadable");
  }
  return {};
}

void ImageComparisonWidget::populateEntries() {
  if (folderTask_.isCanceled() || folderTask_.future().resultCount() == 0) {
    return;
  }
  folderResult_ = folderTask_.result();
  filter_->setEnabled(true);
  for (qsizetype index = 0; index < folderResult_.entries.size(); ++index) {
    const auto &entry = folderResult_.entries.at(index);
    auto *item = new QTreeWidgetItem(entries_, {entry.relativePath, fileStatus(entry.status)});
    item->setData(0, Qt::UserRole, index);
    item->setToolTip(0, entry.relativePath);
  }
  updateFilter();
}

void ImageComparisonWidget::updateFilter() {
  const QSignalBlocker blocker(entries_);
  int visible = 0;
  int changed = 0;
  QTreeWidgetItem *first = nullptr;
  for (int i = 0; i < entries_->topLevelItemCount(); ++i) {
    auto *item = entries_->topLevelItem(i);
    const auto status = folderResult_.entries.at(i).status;
    const int filter = filter_->currentIndex();
    const bool show = filter == AllFiles ||
                      (filter == Changes && status != FileComparisonStatus::Identical) ||
                      (filter == Modified && status == FileComparisonStatus::Different) ||
                      (filter == LeftOnly && status == FileComparisonStatus::LeftOnly) ||
                      (filter == RightOnly && status == FileComparisonStatus::RightOnly);
    item->setHidden(!show);
    if (show) {
      first = first ? first : item;
      ++visible;
      changed += status != FileComparisonStatus::Identical;
    }
  }
  folderStatus_->setText(tr("%1 of %2 files").arg(visible).arg(folderResult_.entries.size()));
  if (!folderResult_.unreadableDirectories.isEmpty()) {
    folderStatus_->setText(folderStatus_->text() + '\n' + tr("Some folders could not be read."));
    folderStatus_->setToolTip(folderResult_.unreadableDirectories.join('\n'));
  } else {
    folderStatus_->setToolTip({});
  }
  previousFile_->setEnabled(changed > 0);
  nextFile_->setEnabled(changed > 0);
  if (!entries_->currentItem() || entries_->currentItem()->isHidden()) {
    entries_->setCurrentItem(first);
    showCurrentEntry();
  }
}

void ImageComparisonWidget::stepFiles(int direction) {
  const int count = entries_->topLevelItemCount();
  int index = entries_->indexOfTopLevelItem(entries_->currentItem());
  if (index < 0) {
    index = direction > 0 ? count - 1 : 0;
  }
  for (int i = 0; i < count; ++i) {
    index = (index + count + direction) % count;
    auto *item = entries_->topLevelItem(index);
    if (!item->isHidden() &&
        folderResult_.entries.at(index).status != FileComparisonStatus::Identical) {
      entries_->setCurrentItem(item);
      entries_->scrollToItem(item);
      return;
    }
  }
}

const heiran::app::FolderComparisonEntry *ImageComparisonWidget::currentEntry() const {
  const auto *item = entries_->currentItem();
  if (!item || item->isHidden()) {
    return nullptr;
  }
  return &folderResult_.entries.at(item->data(0, Qt::UserRole).toLongLong());
}

void ImageComparisonWidget::showCurrentEntry() {
  const auto *entry = currentEntry();
  externalViewer_->setEnabled(entry && !entry->image && !entry->leftPath.isEmpty() &&
                              !entry->rightPath.isEmpty());
  if (!entry) {
    showMessage(tr("No files match the filter."));
  } else if (!entry->image) {
    showMessage(tr("%1\n%2").arg(entry->relativePath, fileStatus(entry->status)));
  } else {
    openPreparedImages(StartupPreparationController::prepareDiffImages(
        {entry->leftPath, entry->rightPath}, QDir::currentPath(),
        {.left = tr("Left: %1").arg(entry->relativePath),
         .right = tr("Right: %1").arg(entry->relativePath)}));
  }
}

void ImageComparisonWidget::showMessage(const QString &message) {
  comparingImages_ = false;
  blinkPending_ = false;
  pixelTask_.cancel();
  pixelsStarted_ = false;
  pixelStatus_->clear();
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setComparisonMapVisible(false);
  grid_->setSession({});
  previewMessage_->setText(message);
  preview_->setCurrentWidget(previewMessage_);
  updateComparisonControls();
}

void ImageComparisonWidget::openExternalViewer() {
  const auto *entry = currentEntry();
  if (!entry || !externalViewer_->isEnabled()) {
    return;
  }
  const QStringList arguments{QDir::toNativeSeparators(entry->leftPath),
                              QDir::toNativeSeparators(entry->rightPath)};
  QSettings settings;
  QString program = settings.value("comparison/externalViewer").toString();
  if (!QFileInfo(program).isFile()) {
    program = chooseExternalViewer();
    if (program.isEmpty()) {
      return;
    }
  }
  if (!QProcess::startDetached(program, arguments)) {
    QMessageBox::warning(this, windowTitle(), tr("Could not start the external diff viewer."));
  }
}

QString ImageComparisonWidget::chooseExternalViewer() {
  QSettings settings;
  const QString key = "comparison/externalViewer";
  const QString program = QFileDialog::getOpenFileName(this, tr("Choose an External Diff Viewer"),
                                                       settings.value(key).toString(),
                                                       tr("Programs (*.exe);;All Files (*)"));
  if (!program.isEmpty()) {
    settings.setValue(key, program);
  }
  return program;
}

void ImageComparisonWidget::applyDiffSettings() {
  auto diff = grid_->cellDiff(kDiffCell);
  if (!diff) {
    return;
  }
  diff->settings.mode = static_cast<DiffMode>(calculation_->currentData().toInt());
  diff->settings.threshold = threshold_->value();
  diff->settings.comparisonBasis = comparisonBasis();
  diff->settings.ignoreFullyTransparentRgb = ignoreTransparentRgb_->isChecked();
  diff->settings.sizeMatch = static_cast<DiffSizeMatch>(sizeMatch_->currentIndex());
  diff->settings.highlightBlockSize = blockSize_->value();
  diff->settings.highlightOpacity = 100 - highlightTransparency_->value();
  grid_->setCellDiff(kDiffCell, diff);
}

void ImageComparisonWidget::applyDisplayMode() {
  if (!comparingImages_) {
    return;
  }
  const int mode = display_->currentIndex();
  blinkPending_ = false;
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setComparisonMapVisible(isSideBySide(mode) && showDifferenceMap_->isChecked());
  grid_->setSoloCell(isSideBySide(mode) ? -1 : kDiffCell);
  blinkPending_ = mode == Blink;
  updateComparisonControls();
}

void ImageComparisonWidget::updateMapLegend() {
  const auto kind = diffFieldKindOf(static_cast<DiffMode>(calculation_->currentData().toInt()));
  const QString white =
      locale().toString(differenceMapWhiteLevel(kind) / mapGain_->currentData().toInt(), 'g', 4);
  switch (kind) {
    case DiffFieldKind::Ssim:
      mapLegend_->setText(tr("Black: 0 · White: %1 (1 − SSIM)").arg(white));
      break;
    case DiffFieldKind::DeltaE:
      mapLegend_->setText(tr("Black: 0 · White: %1 ΔE00").arg(white));
      break;
    default:
      mapLegend_->setText(tr("Black: 0 · White: %1 (0–255 scale)").arg(white));
      break;
  }
}

void ImageComparisonWidget::updateHighlights() {
  const int mode = display_->currentIndex();
  const bool highlighting =
      comparingImages_ && (mode == Highlight || mode == SideBySideHighlighted);
  showDifferenceMap_->setEnabled(comparingImages_ && isSideBySide(mode));
  mapAppearance_->setVisible(showDifferenceMap_->isEnabled() && showDifferenceMap_->isChecked());
  highlightMethod_->setEnabled(highlighting);
  highlightTransparency_->parentWidget()->setEnabled(highlighting);

  const auto metrics = grid_->cellDiffMetrics(kDiffCell);
  if (highlighting && highlightTransparency_->value() < 100 &&
      highlightMethod_->currentIndex() == Blinking && metrics && metrics->exceededPixelCount > 0) {
    if (!highlightTimer_.isActive()) {
      highlightPhaseVisible_ = true;
      highlightTimer_.start();
    }
  } else {
    highlightTimer_.stop();
    highlightPhaseVisible_ = true;
  }
  grid_->setComparisonHighlightsVisible(highlighting && highlightPhaseVisible_);
}

void ImageComparisonWidget::updateComparisonControls() {
  const auto metrics = grid_->cellDiffMetrics(kDiffCell);
  const bool ready = comparingImages_ && metrics.has_value();
  const bool changes = ready && metrics->exceededPixelCount > 0;
  display_->setEnabled(ready);
  calculation_->setEnabled(comparingImages_);
  sizeMatch_->setEnabled(comparingImages_);
  blockSize_->parentWidget()->setEnabled(comparingImages_);
  threshold_->parentWidget()->setEnabled(comparingImages_);
  comparisonBasis_->setEnabled(comparingImages_);
  ignoreTransparentRgb_->setEnabled(comparingImages_ &&
                                    comparisonBasis() == DiffComparisonBasis::PixelValues);
  fit_->setEnabled(preview_->currentWidget() == grid_);
  zoom_->setEnabled(fit_->isEnabled());
  previousDifference_->setEnabled(changes);
  nextDifference_->setEnabled(changes);
  const auto [position, count] = grid_->diffRegionPosition(kDiffCell);
  if (!comparingImages_) {
    differences_->clear();
  } else if (!ready) {
    differences_->setText(grid_->cellDiffMessage(kDiffCell));
  } else if (position > 0) {
    differences_->setText(tr("Difference %1 of %2").arg(position).arg(count));
  } else if (changes) {
    differences_->setText(
        tr("Current settings: %1 pixels detected").arg(metrics->exceededPixelCount));
  } else {
    differences_->setText(tr("Current settings: no differences detected."));
  }
  if (ready && comparisonBasis() == DiffComparisonBasis::PixelValues &&
      grid_->diffHasAlphaChanges(kDiffCell)) {
    differences_->setText(differences_->text() + '\n' + tr("Opacity differs."));
  }
  if (ready && std::exchange(blinkPending_, false)) {
    grid_->setDiffBlinking(kDiffCell, true);
  }
  updateHighlights();
}

DiffComparisonBasis ImageComparisonWidget::comparisonBasis() const {
  return static_cast<DiffComparisonBasis>(comparisonBasis_->currentData().toInt());
}

void ImageComparisonWidget::updatePixelStatus() {
  if (!pixelsStarted_ || pixelTask_.isCanceled() || pixelTask_.future().resultCount() == 0) {
    return;
  }
  const QString result = pixelTask_.result();
  pixelStatus_->setText(tr("Source images:\n%1").arg(result));
}

void ImageComparisonWidget::checkPixels() {
  if (pixelsStarted_ || !comparingImages_) {
    return;
  }
  const auto *left = grid_->comparisonSource(0);
  const auto *right = grid_->comparisonSource(1);
  if (!left || !right) {
    return;
  }
  pixelsStarted_ = true;
  const bool multiple = left->subImageCount > 1 || right->subImageCount > 1;
  const QString equal = multiple ? tr("First images: pixels match.") : tr("Pixels match.");
  const QString different = multiple ? tr("First images: pixels differ.") : tr("Pixels differ.");
  const QString profile = tr("Color profiles differ.");
  const QString dimensions = left->size() != right->size()
                                 ? tr("Image sizes differ: %1 × %2 / %3 × %4")
                                       .arg(left->size().width())
                                       .arg(left->size().height())
                                       .arg(right->size().width())
                                       .arg(right->size().height())
                                 : QString();
  const bool ignoreTransparentRgb = ignoreTransparentRgb_->isChecked();
  pixelTask_.setFuture(
      QtConcurrent::run([left = *left, right = *right, equal, different, profile, dimensions,
                         ignoreTransparentRgb](QPromise<QString> &promise) {
        const bool same = heiran::app::equalDecodedPixels(left, right, ignoreTransparentRgb,
                                                          [&]() { return promise.isCanceled(); });
        if (!promise.isCanceled()) {
          QString result = same ? equal : different;
          if (left.colorSpace != right.colorSpace) {
            result += '\n' + profile;
          }
          if (!dimensions.isEmpty()) {
            result += '\n' + dimensions;
          }
          promise.addResult(result);
        }
      }));
}

}  // namespace heiran::ui
