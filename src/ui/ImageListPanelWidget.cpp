#include "heiran/ui/ImageListPanelWidget.hpp"

#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/ImageListDragViews.hpp"
#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/ThumbnailCache.hpp"
#include "heiran/ui/ThumbnailItemDelegate.hpp"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>
#include <algorithm>
#include <array>

namespace heiran::ui {

namespace {
constexpr std::array kThumbnailWidths{64, 160, 224, 320};
constexpr int kThumbnailWidthStep = 16;
}  // namespace

ImageListPanelWidget::ImageListPanelWidget(QWidget *parent)
    : QWidget(parent), list_(new ImageFileListWidget(this)), count_(new QLabel(this)),
      empty_(new QLabel(tr("No images to display."), this)),
      excludedGroup_(new QGroupBox(tr("Displayed outside filter"), this)),
      excludedFiles_(new QLabel(excludedGroup_)), favoritesOnly_(new QToolButton(this)),
      thumbnailsAction_(new QAction(tr("Thumbnails"), this)),
      detailsAction_(new QAction(tr("Details"), this)), thumbnailSizeControls_(new QWidget(this)),
      thumbnailSizeButtons_(new QButtonGroup(this)),
      thumbnailSizeLabel_(new QLabel(thumbnailSizeControls_)), views_(new QStackedWidget(this)),
      details_(new ImageFileDetailsWidget(this)),
      compare_(new QPushButton(tr("Compare Selected"), this)),
      finishComparison_(new QPushButton(tr("End Comparison"), this)),
      addFavoritesAction_(new QAction(tr("Add Favorites"), this)),
      removeFavoritesAction_(new QAction(tr("Remove Favorites"), this)) {
  setObjectName("imageListPanelWidget");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  auto *modeLayout = new QHBoxLayout;
  auto *modes = new QActionGroup(this);
  modes->setExclusive(true);
  modes->setObjectName("imageListViewMode");
  for (auto *action : {thumbnailsAction_, detailsAction_}) {
    action->setCheckable(true);
    modes->addAction(action);
    auto *button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setObjectName(action == thumbnailsAction_ ? "imageListThumbnailsButton"
                                                      : "imageListDetailsButton");
    modeLayout->addWidget(button);
  }
  modeLayout->addStretch();
  thumbnailsAction_->setChecked(true);
  auto *sizeLayout = new QHBoxLayout(thumbnailSizeControls_);
  sizeLayout->setContentsMargins(0, 0, 0, 0);
  sizeLayout->setSpacing(2);
  sizeLayout->addWidget(new QLabel(tr("Size:"), thumbnailSizeControls_));
  const std::array sizeLabels{tr("Small"), tr("Medium"), tr("Large"), tr("Extra Large")};
  for (size_t index = 0; index < kThumbnailWidths.size(); ++index) {
    const int width = kThumbnailWidths.at(index);
    auto *button = new QToolButton(thumbnailSizeControls_);
    button->setObjectName(QString("thumbnailWidth%1").arg(width));
    button->setProperty("thumbnailPreset", true);
    button->setCheckable(true);
    button->setText(sizeLabels.at(index));
    button->setToolTip(tr("Maximum width: %1 px. Change size with Ctrl+mouse wheel.").arg(width));
    thumbnailSizeButtons_->addButton(button, width);
    sizeLayout->addWidget(button);
  }
  thumbnailSizeButtons_->button(thumbnailWidth_)->setChecked(true);
  sizeLayout->addStretch();
  sizeLayout->addWidget(thumbnailSizeLabel_);
  list_->viewport()->installEventFilter(this);
  favoritesOnly_->setObjectName("imageListFavoritesOnly");
  favoritesOnly_->setText(tr("Favorites only"));
  favoritesOnly_->setToolTip(tr("Show only favorites in the list and when changing pages."));
  favoritesOnly_->setIcon(themedIcon("filter"));
  favoritesOnly_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  favoritesOnly_->setCheckable(true);
  layout->addLayout(modeLayout);
  layout->addWidget(thumbnailSizeControls_);
  auto *filterLayout = new QHBoxLayout;
  auto *filterLabel = new QLabel(tr("Filter:"), this);
  filterLabel->setBuddy(favoritesOnly_);
  filterLayout->addWidget(filterLabel);
  filterLayout->addWidget(favoritesOnly_);
  filterLayout->addStretch();
  layout->addLayout(filterLayout);
  auto *excludedLayout = new QVBoxLayout(excludedGroup_);
  excludedFiles_->setObjectName("excludedDisplayedFiles");
  excludedFiles_->setTextFormat(Qt::PlainText);
  excludedFiles_->setWordWrap(true);
  excludedFiles_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  excludedLayout->addWidget(excludedFiles_);
  excludedGroup_->hide();
  layout->addWidget(excludedGroup_);
  auto *comparisonLayout = new QHBoxLayout;
  count_->setWordWrap(true);
  comparisonLayout->addWidget(count_, 1);
  comparisonLayout->addWidget(compare_);
  layout->addLayout(comparisonLayout);
  addFavoritesAction_->setObjectName("addSelectedFavoritesAction");
  removeFavoritesAction_->setObjectName("removeSelectedFavoritesAction");
  addFavoritesAction_->setToolTip(tr("Add selected images to favorites."));
  removeFavoritesAction_->setToolTip(tr("Remove selected images from favorites."));
  imageDiffAction_ = new QAction(themedIcon("layers"), tr("Compare Image Differences"), this);
  imageDiffAction_->setEnabled(false);
  for (auto *view :
       {static_cast<QAbstractItemView *>(list_), static_cast<QAbstractItemView *>(details_)}) {
    view->setContextMenuPolicy(Qt::ActionsContextMenu);
    view->addActions({addFavoritesAction_, removeFavoritesAction_, imageDiffAction_});
  }
  connect(imageDiffAction_, &QAction::triggered, this,
          [this]() { Q_EMIT imageDiffRequested(selectedFilePaths()); });
  connect(addFavoritesAction_, &QAction::triggered, this,
          [this]() { Q_EMIT favoritesRequested(selectedFilePaths(), true); });
  connect(removeFavoritesAction_, &QAction::triggered, this,
          [this]() { Q_EMIT favoritesRequested(selectedFilePaths(), false); });
  compare_->setObjectName("compareSelectedImages");
  compare_->setIcon(themedIcon("layout-grid"));
  compare_->setToolTip(tr("Select 2 to 16 images to view side by side. Ctrl-click or Shift-click "
                          "to select multiple images."));
  connect(compare_, &QPushButton::clicked, this,
          [this]() { Q_EMIT compareRequested(selectedFilePaths()); });
  layout->addWidget(finishComparison_);
  finishComparison_->setObjectName("finishComparison");
  finishComparison_->setIcon(themedIcon("arrow-left"));
  finishComparison_->setToolTip(tr("Return to the layout and page before comparison."));
  finishComparison_->hide();
  connect(finishComparison_, &QPushButton::clicked, this,
          &ImageListPanelWidget::finishComparisonRequested);
  layout->addWidget(views_, 1);
  layout->addWidget(empty_, 1);
  views_->addWidget(list_);
  views_->addWidget(details_);
  details_->setObjectName("imageDetailsList");
  details_->setAccessibleName(tr("Image details"));
  details_->setHeaderLabels({QStringLiteral("\u2605"), tr("File name"), tr("Width (px)"),
                             tr("Height (px)"), tr("File size"), tr("Cell")});
  details_->headerItem()->setToolTip(0, tr("Favorite"));
  details_->headerItem()->setToolTip(5, tr("Displayed in"));
  details_->setRootIsDecorated(false);
  details_->setUniformRowHeights(true);
  details_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  details_->setSelectionBehavior(QAbstractItemView::SelectRows);
  details_->setDragEnabled(true);
  details_->setDragDropMode(QAbstractItemView::DragOnly);
  details_->setDefaultDropAction(Qt::CopyAction);
  details_->setColumnWidth(0, 32);
  details_->setColumnWidth(1, 160);
  details_->setColumnWidth(2, 80);
  details_->setColumnWidth(3, 80);
  details_->setColumnWidth(4, 120);
  details_->setColumnWidth(5, 56);
  details_->header()->moveSection(5, 1);
  details_->header()->setStretchLastSection(false);
  detailsPool_.setMaxThreadCount(1);
  connect(details_, &QTreeWidget::itemSelectionChanged, this, [this]() { syncSelection(true); });
  connect(favoritesOnly_, &QToolButton::toggled, this,
          &ImageListPanelWidget::favoritesOnlyRequested);
  connect(modes, &QActionGroup::triggered, this, [this]() {
    if (views_->currentIndex() != (detailsVisible() ? 1 : 0))
      updateViewMode();
  });
  connect(thumbnailSizeButtons_, &QButtonGroup::idClicked, this, [this](int width) {
    thumbnailWheelRemainder_ = 0;
    setThumbnailWidth(width);
  });
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  list_->setObjectName("thumbnailList");
  list_->setAccessibleName(tr("Image List"));
  list_->setViewMode(QListView::IconMode);
  updateThumbnailGeometry();
  list_->setItemDelegate(new ThumbnailItemDelegate(list_));
  list_->setResizeMode(QListView::Adjust);
  list_->setMovement(QListView::Static);
  list_->setUniformItemSizes(true);
  list_->setWordWrap(true);
  list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list_->setDragEnabled(true);
  list_->setDragDropMode(QAbstractItemView::DragOnly);
  list_->setDefaultDropAction(Qt::CopyAction);
  connect(list_, &QListWidget::itemSelectionChanged, this, [this]() { syncSelection(false); });
  setPages({}, -1);
  applyActionIcons(this);
}

ImageListPanelWidget::~ImageListPanelWidget() {
  cancelLoading();
  loaderPool_.waitForDone();
  detailsPool_.waitForDone();
}

void ImageListPanelWidget::cancelLoading() {
  ++generation_;
  if (watcher_) {
    watcher_->cancel();
    watcher_ = nullptr;
  }
  if (detailsWatcher_) {
    detailsWatcher_->cancel();
    detailsWatcher_ = nullptr;
  }
}

void ImageListPanelWidget::setFavoritesOnly(bool enabled) {
  const QSignalBlocker blocker(favoritesOnly_);
  favoritesOnly_->setChecked(enabled);
}

void ImageListPanelWidget::setComparisonActive(bool active) {
  finishComparison_->setVisible(active);
}

bool ImageListPanelWidget::detailsVisible() const {
  return detailsAction_->isChecked();
}

void ImageListPanelWidget::setDetailsVisible(bool details) {
  if (detailsVisible() == details)
    return;
  (details ? detailsAction_ : thumbnailsAction_)->setChecked(true);
  updateViewMode();
}

void ImageListPanelWidget::updateViewMode() {
  thumbnailWheelRemainder_ = 0;
  cancelLoading();
  views_->setCurrentIndex(detailsVisible() ? 1 : 0);
  thumbnailSizeControls_->setVisible(!detailsVisible());
  if (detailsVisible()) {
    startDetailsLoading();
  } else {
    startLoading();
  }
  Q_EMIT detailsVisibleChanged(detailsVisible());
  updateSelectionActions();
}

QSize ImageListPanelWidget::thumbnailSize() const {
  return {thumbnailWidth_, thumbnailWidth_ * 3 / 4};
}

void ImageListPanelWidget::updateThumbnailGeometry() {
  const auto size = thumbnailSize();
  list_->setIconSize(size);
  list_->setGridSize(size + QSize(24, 44));
  for (int row = 0; row < list_->count(); ++row)
    list_->item(row)->setSizeHint(size + QSize(24, 40));
  thumbnailSizeLabel_->setText(tr("%1 px").arg(thumbnailWidth_));
}

void ImageListPanelWidget::setThumbnailWidth(int width) {
  width = std::clamp(width, kThumbnailWidths.front(), kThumbnailWidths.back());
  width = (width + (kThumbnailWidthStep - 1) / 2) / kThumbnailWidthStep * kThumbnailWidthStep;
  if (width == thumbnailWidth_)
    return;
  thumbnailWidth_ = width;
  if (auto *button = thumbnailSizeButtons_->button(width)) {
    button->setChecked(true);
  } else if (auto *checked = thumbnailSizeButtons_->checkedButton()) {
    thumbnailSizeButtons_->setExclusive(false);
    checked->setChecked(false);
    thumbnailSizeButtons_->setExclusive(true);
  }
  updateThumbnailGeometry();
  if (!detailsVisible()) {
    cancelLoading();
    startLoading();
  }
  Q_EMIT thumbnailWidthChanged(width);
}

bool ImageListPanelWidget::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() != QEvent::Wheel || watched != list_->viewport() || detailsVisible())
    return QWidget::eventFilter(watched, event);
  auto *wheel = static_cast<QWheelEvent *>(event);
  if (wheel->modifiers() != Qt::ControlModifier) {
    thumbnailWheelRemainder_ = 0;
    return QWidget::eventFilter(watched, event);
  }

  const int delta = wheel->angleDelta().y();
  if (wheel->phase() == Qt::ScrollBegin || (delta > 0 && thumbnailWheelRemainder_ < 0) ||
      (delta < 0 && thumbnailWheelRemainder_ > 0)) {
    thumbnailWheelRemainder_ = 0;
  }
  const auto angle = static_cast<qint64>(delta) + thumbnailWheelRemainder_;
  const auto steps = angle / 120;
  thumbnailWheelRemainder_ = wheel->phase() == Qt::ScrollEnd ? 0 : static_cast<int>(angle % 120);
  if (steps != 0) {
    const auto width = std::clamp<qint64>(thumbnailWidth_ + steps * kThumbnailWidthStep,
                                          kThumbnailWidths.front(), kThumbnailWidths.back());
    setThumbnailWidth(static_cast<int>(width));
  }
  wheel->accept();
  return true;
}

void ImageListPanelWidget::setPages(const QStringList &pagePaths, int currentPage) {
  if (paths_ == pagePaths && list_->count() == pagePaths.size() && !pagePaths.isEmpty()) {
    setCurrentPage(currentPage);
    if (detailsVisible()) {
      if (!detailsWatcher_) {
        startDetailsLoading();
      }
    } else if (!watcher_) {
      startLoading();
    }
    return;
  }
  cancelLoading();
  paths_ = pagePaths;
  const QSignalBlocker blocker(list_);
  const QSignalBlocker detailsBlocker(details_);
  list_->clear();
  details_->clear();
  count_->setText(tr("%1 items").arg(paths_.size()));
  views_->setVisible(!paths_.isEmpty());
  empty_->setVisible(paths_.isEmpty());
  for (int index = 0; index < paths_.size(); ++index) {
    const QString &path = paths_.at(index);
    const QString caption = path.isEmpty()
                                ? QString::number(index + 1)
                                : QString("%1: %2").arg(index + 1).arg(QFileInfo(path).fileName());
    auto *item = new QListWidgetItem(caption, list_);
    item->setSizeHint(thumbnailSize() + QSize(24, 40));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    item->setData(Qt::UserRole, caption);
    item->setData(Qt::UserRole + 1, index);
    item->setData(Qt::UserRole + 2, path);
    item->setToolTip(path.isEmpty() ? caption : path);
    QPixmap placeholder(thumbnailSize());
    placeholder.fill(Qt::transparent);
    item->setIcon(QIcon(placeholder));
    auto *detail = new QTreeWidgetItem(
        details_, {QStringLiteral("\u2014"), QFileInfo(path).fileName(), "...", "...", "...", ""});
    detail->setToolTip(1, path);
    detail->setData(0, Qt::UserRole + 1, index);
    detail->setData(0, Qt::UserRole + 2, path);
    detail->setTextAlignment(0, Qt::AlignCenter);
    for (int column = 2; column < 5; ++column) {
      detail->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  setCurrentPage(currentPage);
  updateSelectionActions();
  if (detailsVisible()) {
    startDetailsLoading();
  } else {
    startLoading();
  }
}

void ImageListPanelWidget::setCurrentPage(int page) {
  const QSignalBlocker blocker(list_);
  const QSignalBlocker detailsBlocker(details_);
  const bool keepSelection = selectedRows(detailsVisible()).size() > 1;
  const auto flags =
      keepSelection ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect;
  auto *detail =
      page >= 0 && page < details_->topLevelItemCount() ? details_->topLevelItem(page) : nullptr;
  if (details_->currentItem() != detail) {
    details_->setCurrentItem(detail, 0, flags);
    if (detail) {
      details_->scrollToItem(detail, QAbstractItemView::EnsureVisible);
    }
  }
  if (list_->currentRow() != page) {
    list_->setCurrentRow(page >= 0 && page < list_->count() ? page : -1, flags);
    if (auto *item = list_->currentItem()) {
      list_->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
  }
  updateSelectionActions();
}

QVector<int> ImageListPanelWidget::selectedRows(bool details) const {
  QVector<int> rows;
  if (details) {
    for (auto *item : details_->selectedItems()) {
      rows.push_back(details_->indexOfTopLevelItem(item));
    }
  } else {
    for (const auto *item : list_->selectedItems()) {
      rows.push_back(list_->row(item));
    }
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

QStringList ImageListPanelWidget::selectedFilePaths() const {
  QStringList paths;
  QSet<QString> seen;
  for (int row : selectedRows(detailsVisible())) {
    const auto path = paths_.value(row);
    const auto key = heiran::infra::FavoriteStore::pathKey(path);
    if (QFileInfo(path).isFile() && !seen.contains(key)) {
      paths.push_back(path);
      seen.insert(key);
    }
  }
  return paths;
}

void ImageListPanelWidget::syncSelection(bool fromDetails) {
  const auto rows = selectedRows(fromDetails);
  const QSignalBlocker listBlocker(list_);
  const QSignalBlocker detailsBlocker(details_);
  if (fromDetails) {
    list_->clearSelection();
    for (int row : rows) {
      list_->item(row)->setSelected(true);
    }
  } else {
    details_->clearSelection();
    for (int row : rows) {
      details_->topLevelItem(row)->setSelected(true);
    }
  }
  updateSelectionActions();
  if (rows.size() == 1) {
    Q_EMIT pageSelected(rows.first());
  }
}

void ImageListPanelWidget::updateSelectionActions() {
  const int count = selectedRows(detailsVisible()).size();
  count_->setText(tr("%1 items, %2 selected").arg(paths_.size()).arg(count));
  compare_->setEnabled(count >= 2 && count <= SessionLayoutController::kMaxCellCount);
  imageDiffAction_->setEnabled(count == 2);
  addFavoritesAction_->setEnabled(count > 0);
  removeFavoritesAction_->setEnabled(count > 0);
}

void ImageListPanelWidget::startDetailsLoading() {
  if (paths_.isEmpty()) {
    return;
  }
  auto *watcher = new QFutureWatcher<heiran::infra::ImageFileDetails>(this);
  detailsWatcher_ = watcher;
  const auto generation = generation_;
  connect(
      watcher, &QFutureWatcher<heiran::infra::ImageFileDetails>::resultReadyAt, this,
      [this, watcher, generation](int index) {
        if (generation != generation_ || index < 0 || index >= details_->topLevelItemCount()) {
          return;
        }
        const auto result = watcher->resultAt(index);
        auto *item = details_->topLevelItem(index);
        const QString unknown = QStringLiteral("\u2014");
        item->setText(2, result.dimensions.isEmpty() ? unknown
                                                     : QString::number(result.dimensions.width()));
        item->setText(3, result.dimensions.isEmpty() ? unknown
                                                     : QString::number(result.dimensions.height()));
        item->setText(4, result.fileSize < 0 ? unknown
                                             : QLocale().formattedDataSize(result.fileSize, 1));
        if (result.fileSize >= 0) {
          item->setToolTip(4, tr("%1 bytes").arg(QLocale().toString(result.fileSize)));
        }
        if (!result.error.isEmpty()) {
          item->setToolTip(2, result.error);
          item->setToolTip(3, result.error);
        }
      });
  connect(watcher, &QFutureWatcher<heiran::infra::ImageFileDetails>::finished, watcher,
          &QObject::deleteLater);
  watcher->setFuture(
      QtConcurrent::mapped(&detailsPool_, paths_, heiran::infra::readImageFileDetails));
}

void ImageListPanelWidget::setFavoriteStates(const QVector<bool> &states) {
  for (int index = 0; index < list_->count(); ++index) {
    const bool favorite = states.value(index, false);
    auto *item = list_->item(index);
    item->setData(ThumbnailFavoriteRole, favorite);
    updateItemDescription(index);
    const auto description = favorite ? tr("Favorite") : tr("Not a favorite");
    auto *detail = details_->topLevelItem(index);
    detail->setText(0, favorite ? QStringLiteral("\u2605") : QStringLiteral("\u2014"));
    detail->setToolTip(0, description);
    detail->setData(0, Qt::AccessibleDescriptionRole, description);
  }
}

void ImageListPanelWidget::setDisplayedFiles(const QStringList &cellPaths) {
  QHash<QString, QStringList> cellsByPath;
  for (int cell = 0; cell < cellPaths.size(); ++cell) {
    if (!cellPaths.at(cell).isEmpty()) {
      cellsByPath[heiran::infra::FavoriteStore::pathKey(cellPaths.at(cell))].push_back(
          QString::number(cell + 1));
    }
  }
  for (int index = 0; index < paths_.size(); ++index) {
    const auto cells = cellsByPath.value(heiran::infra::FavoriteStore::pathKey(paths_.at(index)));
    const auto text =
        cells.isEmpty() ? QString() : tr("Displayed in cell %1").arg(cells.join(", "));
    auto *item = list_->item(index);
    item->setData(ThumbnailCellsRole, cells);
    updateItemDescription(index);
    details_->topLevelItem(index)->setText(5, cells.join(", "));
    details_->topLevelItem(index)->setToolTip(5, text);
  }
}

void ImageListPanelWidget::setExcludedDisplayedFiles(const QStringList &cellPaths) {
  QStringList labels, descriptions;
  for (int cell = 0; cell < cellPaths.size(); ++cell) {
    const auto &path = cellPaths.at(cell);
    if (path.isEmpty())
      continue;
    labels.push_back(tr("Cell %1: %2").arg(cell + 1).arg(QFileInfo(path).fileName()));
    descriptions.push_back(tr("Cell %1: %2").arg(cell + 1).arg(path));
  }
  excludedFiles_->setText(labels.join("\n"));
  excludedFiles_->setToolTip(descriptions.join("\n"));
  excludedGroup_->setVisible(!labels.isEmpty());
}

void ImageListPanelWidget::updateItemDescription(int index) {
  auto *item = list_->item(index);
  const auto caption = item->data(Qt::UserRole).toString();
  const bool favorite = item->data(ThumbnailFavoriteRole).toBool();
  const auto cells = item->data(ThumbnailCellsRole).toStringList();
  QStringList descriptions{favorite ? tr("Favorite") : tr("Not a favorite")};
  if (!cells.isEmpty())
    descriptions.push_back(tr("Displayed in cell %1").arg(cells.join(", ")));
  const auto description = descriptions.join("\n");
  item->setData(Qt::AccessibleDescriptionRole, description);
  const auto path = paths_.value(index);
  item->setToolTip((path.isEmpty() ? caption : path) + "\n" + description);
}

void ImageListPanelWidget::startLoading() {
  if (paths_.isEmpty()) {
    return;
  }
  auto *watcher = new QFutureWatcher<QImage>(this);
  watcher_ = watcher;
  const auto generation = generation_;
  connect(watcher, &QFutureWatcher<QImage>::resultReadyAt, this,
          [this, watcher, generation](int index) {
            if (generation != generation_ || index < 0 || index >= list_->count()) {
              return;
            }
            const auto image = watcher->resultAt(index);
            if (!image.isNull()) {
              list_->item(index)->setIcon(QIcon(QPixmap::fromImage(image)));
            }
          });
  connect(watcher, &QFutureWatcher<QImage>::finished, watcher, &QObject::deleteLater);
  watcher->setFuture(
      QtConcurrent::mapped(&loaderPool_, paths_, [size = thumbnailSize()](const QString &path) {
        return path.isEmpty() ? QImage() : ThumbnailCache::standard().loadOrCreate(path, size);
      }));
}

}  // namespace heiran::ui
