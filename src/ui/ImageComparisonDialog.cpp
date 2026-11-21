#include "heiran/ui/ImageComparisonDialog.hpp"

#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPromise>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace heiran::ui {
namespace {

using heiran::app::FileComparisonStatus;
using heiran::domain::DiffMode;
using heiran::domain::DiffSizeMatch;

constexpr int kDiffCell = 2;
enum Display { SideBySide, Highlight, Blink };
enum Filter { AllFiles, Changes, Modified, LeftOnly, RightOnly };

QLabel *messageLabel(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setMinimumWidth(0);
  return label;
}

}  // namespace

ImageComparisonDialog::ImageComparisonDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Image Difference - Heiran"));
  setWindowFlag(Qt::WindowMinMaxButtonsHint);
  resize(1160, 700);
  auto *root = new QVBoxLayout(this);
  auto *splitter = new QSplitter(this);
  splitter->setChildrenCollapsible(false);
  root->addWidget(splitter);
  auto *sidebar = new QWidget(splitter);
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
  auto *reload = new QPushButton(tr("Refresh"), folderControls_);
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
  externalViewer_ = new QPushButton(tr("Open in External Diff Viewer..."), sidebar);
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
  display_->addItems({tr("Side by side"), tr("Highlight differences"), tr("Blink")});
  form->addRow(tr("Display"), display_);
  calculation_ = new QComboBox(sidebar);
  calculation_->addItem(tr("Brightness"), static_cast<int>(DiffMode::Absolute));
  calculation_->addItem(tr("Color difference (ΔE00)"), static_cast<int>(DiffMode::DeltaE));
  calculation_->addItem(tr("Structural similarity (SSIM)"), static_cast<int>(DiffMode::Ssim));
  form->addRow(tr("Compare by"), calculation_);
  threshold_ = new QDoubleSpinBox(sidebar);
  threshold_->setObjectName("imageComparisonThreshold");
  threshold_->setDecimals(3);
  threshold_->setRange(0.0, 255.0);
  threshold_->setValue(8.0);
  form->addRow(tr("Threshold"), threshold_);
  sizeMatch_ = new QComboBox(sidebar);
  sizeMatch_->addItems(
      {tr("Align top left"), tr("Resize to left"), tr("Resize to right"), tr("Resize to larger")});
  form->addRow(tr("Image size"), sizeMatch_);
  left->addLayout(form);
  auto *navigation = new QHBoxLayout;
  previousDifference_ =
      new QPushButton(themedIcon("arrow-left"), tr("Previous difference"), sidebar);
  nextDifference_ = new QPushButton(themedIcon("arrow-right"), tr("Next difference"), sidebar);
  navigation->addWidget(previousDifference_);
  navigation->addWidget(nextDifference_);
  left->addLayout(navigation);
  differences_ = messageLabel(sidebar);
  pixelStatus_ = messageLabel(sidebar);
  pixelStatus_->setObjectName("imageComparisonPixelStatus");
  left->addWidget(differences_);
  left->addWidget(pixelStatus_);
  auto *footer = new QHBoxLayout;
  fit_ = new QPushButton(tr("Fit"), sidebar);
  auto *close = new QPushButton(tr("End Comparison"), sidebar);
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

  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(fit_, &QPushButton::clicked, this, [this]() {
    grid_->resetView();
    grid_->requestFitToCells();
  });
  connect(reload, &QPushButton::clicked, this, &ImageComparisonDialog::reloadFolders);
  connect(&folderTask_, &QFutureWatcherBase::finished, this,
          &ImageComparisonDialog::populateEntries);
  connect(filter_, &QComboBox::currentIndexChanged, this, &ImageComparisonDialog::updateFilter);
  connect(entries_, &QTreeWidget::currentItemChanged, this,
          &ImageComparisonDialog::showCurrentEntry);
  connect(previousFile_, &QPushButton::clicked, this, [this]() { stepFiles(-1); });
  connect(nextFile_, &QPushButton::clicked, this, [this]() { stepFiles(1); });
  connect(externalViewer_, &QPushButton::clicked, this, &ImageComparisonDialog::openExternalViewer);
  connect(chooseViewer, &QToolButton::clicked, this, [this]() { chooseExternalViewer(); });
  connect(display_, &QComboBox::currentIndexChanged, this,
          &ImageComparisonDialog::applyDisplayMode);
  connect(calculation_, &QComboBox::currentIndexChanged, this, [this]() {
    const auto mode = static_cast<DiffMode>(calculation_->currentData().toInt());
    const QSignalBlocker blocker(threshold_);
    threshold_->setRange(0.0, mode == DiffMode::Ssim ? 1.0 : 255.0);
    threshold_->setSingleStep(mode == DiffMode::Ssim ? 0.01 : 1.0);
    threshold_->setValue(mode == DiffMode::Ssim ? 0.9 : mode == DiffMode::DeltaE ? 2.3 : 8.0);
    applyDiffSettings();
  });
  connect(sizeMatch_, &QComboBox::currentIndexChanged, this, [this]() {
    if (display_->currentIndex() == Blink) {
      display_->setCurrentIndex(Highlight);
    }
    applyDiffSettings();
  });
  connect(threshold_, &QDoubleSpinBox::valueChanged, this,
          &ImageComparisonDialog::applyDiffSettings);
  const auto jump = [this](int direction) {
    grid_->moveToDiffRegion(kDiffCell, direction);
    updateComparisonControls();
  };
  connect(previousDifference_, &QPushButton::clicked, this, [jump]() { jump(-1); });
  connect(nextDifference_, &QPushButton::clicked, this, [jump]() { jump(1); });
  connect(grid_, &ViewerGridWidget::cellDiffChanged, this,
          &ImageComparisonDialog::updateComparisonControls);
  connect(grid_, &ViewerGridWidget::imageContentChanged, this, &ImageComparisonDialog::checkPixels);
  connect(&pixelTask_, &QFutureWatcherBase::finished, this, [this]() {
    if (!pixelTask_.isCanceled() && pixelTask_.future().resultCount() > 0) {
      pixelStatus_->setText(pixelTask_.result());
    }
  });
  updateComparisonControls();
}

ImageComparisonDialog::~ImageComparisonDialog() {
  folderTask_.cancel();
  pixelTask_.cancel();
}

void ImageComparisonDialog::openImages(const QStringList &paths) {
  openPreparedImages(StartupPreparationController::prepareDiffImages(paths, QDir::currentPath()));
}

void ImageComparisonDialog::openPreparedImages(PreparedImageSession prepared,
                                               std::shared_ptr<DecodeScheduler> scheduler) {
  pixelTask_.cancel();
  pixelsStarted_ = false;
  pixelStatus_->clear();
  comparingImages_ = prepared.sourcePaths.size() == 2;
  if (leftRoot_.isEmpty()) {
    paths_->setText(prepared.sourcePaths.join('\n'));
  }
  if (display_->currentIndex() == Blink) {
    const QSignalBlocker blocker(display_);
    display_->setCurrentIndex(SideBySide);
  }
  preview_->setCurrentWidget(grid_);
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

void ImageComparisonDialog::openFolders(const QString &left, const QString &right) {
  leftRoot_ = left;
  rightRoot_ = right;
  setWindowTitle(tr("Folder Comparison - Heiran"));
  paths_->setText(tr("Left: %1\nRight: %2")
                      .arg(QDir::toNativeSeparators(left), QDir::toNativeSeparators(right)));
  folderControls_->show();
  reloadFolders();
}

void ImageComparisonDialog::reloadFolders() {
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

QString ImageComparisonDialog::fileStatus(FileComparisonStatus status) const {
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

void ImageComparisonDialog::populateEntries() {
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

void ImageComparisonDialog::updateFilter() {
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

void ImageComparisonDialog::stepFiles(int direction) {
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

const heiran::app::FolderComparisonEntry *ImageComparisonDialog::currentEntry() const {
  const auto *item = entries_->currentItem();
  if (!item || item->isHidden()) {
    return nullptr;
  }
  return &folderResult_.entries.at(item->data(0, Qt::UserRole).toLongLong());
}

void ImageComparisonDialog::showCurrentEntry() {
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

void ImageComparisonDialog::showMessage(const QString &message) {
  comparingImages_ = false;
  pixelTask_.cancel();
  pixelsStarted_ = false;
  pixelStatus_->clear();
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setSession({});
  previewMessage_->setText(message);
  preview_->setCurrentWidget(previewMessage_);
  updateComparisonControls();
}

void ImageComparisonDialog::openExternalViewer() {
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

QString ImageComparisonDialog::chooseExternalViewer() {
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

void ImageComparisonDialog::applyDiffSettings() {
  auto diff = grid_->cellDiff(kDiffCell);
  if (!diff) {
    return;
  }
  diff->settings.mode = static_cast<DiffMode>(calculation_->currentData().toInt());
  diff->settings.threshold = threshold_->value();
  diff->settings.sizeMatch = static_cast<DiffSizeMatch>(sizeMatch_->currentIndex());
  grid_->setCellDiff(kDiffCell, diff);
}

void ImageComparisonDialog::applyDisplayMode() {
  if (!comparingImages_) {
    return;
  }
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setSoloCell(display_->currentIndex() == SideBySide ? -1 : kDiffCell);
  if (display_->currentIndex() == Blink) {
    grid_->setDiffBlinking(kDiffCell, true);
  }
}

void ImageComparisonDialog::updateComparisonControls() {
  const auto metrics = grid_->cellDiffMetrics(kDiffCell);
  const bool ready = comparingImages_ && metrics.has_value();
  const bool changes = ready && metrics->exceededPixelCount > 0;
  display_->setEnabled(ready);
  calculation_->setEnabled(comparingImages_);
  sizeMatch_->setEnabled(comparingImages_);
  threshold_->setEnabled(comparingImages_);
  fit_->setEnabled(preview_->currentWidget() == grid_);
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
    differences_->setText(tr("%1 changed pixels").arg(metrics->exceededPixelCount));
  } else {
    differences_->setText(tr("No changed pixels."));
  }
  if (ready && grid_->diffHasAlphaChanges(kDiffCell)) {
    differences_->setText(differences_->text() + '\n' + tr("Opacity differs."));
  }
}

void ImageComparisonDialog::checkPixels() {
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
  pixelTask_.setFuture(QtConcurrent::run([left = *left, right = *right, equal, different, profile,
                                          dimensions](QPromise<QString> &promise) {
    const bool same =
        heiran::app::equalDecodedPixels(left, right, [&]() { return promise.isCanceled(); });
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
