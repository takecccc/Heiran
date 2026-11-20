#include "heiran/ui/ImageListPanelWidget.hpp"

#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/ImageListDragViews.hpp"
#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/ThumbnailCache.hpp"
#include "heiran/ui/ThumbnailItemDelegate.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
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
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace heiran::ui {

ImageListPanelWidget::ImageListPanelWidget(QWidget *parent)
    : QWidget(parent), list_(new ImageFileListWidget(this)), count_(new QLabel(this)),
      empty_(new QLabel(tr("No images to display."), this)),
      excludedGroup_(new QGroupBox(tr("Displayed outside filter"), this)),
      excludedFiles_(new QLabel(excludedGroup_)),
      favoritesOnly_(new QCheckBox(tr("Favorites only"), this)),
      thumbnailsAction_(new QAction(tr("Thumbnails"), this)),
      detailsAction_(new QAction(tr("Details"), this)), thumbnailSizeControls_(new QWidget(this)),
      thumbnailSizeSlider_(new QSlider(Qt::Horizontal, thumbnailSizeControls_)),
      thumbnailSizeLabel_(new QLabel(thumbnailSizeControls_)), views_(new QStackedWidget(this)),
      details_(new ImageFileDetailsWidget(this)),
      compare_(new QPushButton(tr("Compare Selected"), this)),
      addFavorites_(new QPushButton(tr("Add Favorites"), this)),
      removeFavorites_(new QPushButton(tr("Remove Favorites"), this)),
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
  auto *sizeLabel = new QLabel(tr("Size:"), thumbnailSizeControls_);
  sizeLabel->setBuddy(thumbnailSizeSlider_);
  thumbnailSizeSlider_->setObjectName("thumbnailSizeSlider");
  thumbnailSizeSlider_->setAccessibleName(tr("Thumbnail size"));
  thumbnailSizeSlider_->setToolTip(tr("Maximum thumbnail width in pixels."));
  thumbnailSizeSlider_->setRange(64, 320);
  thumbnailSizeSlider_->setSingleStep(16);
  thumbnailSizeSlider_->setPageStep(64);
  thumbnailSizeSlider_->setTracking(false);
  thumbnailSizeSlider_->setValue(thumbnailWidth_);
  sizeLayout->addWidget(sizeLabel);
  sizeLayout->addWidget(thumbnailSizeSlider_, 1);
  sizeLayout->addWidget(thumbnailSizeLabel_);
  favoritesOnly_->setObjectName("imageListFavoritesOnly");
  layout->addLayout(modeLayout);
  layout->addWidget(thumbnailSizeControls_);
  layout->addWidget(favoritesOnly_);
  auto *excludedLayout = new QVBoxLayout(excludedGroup_);
  excludedFiles_->setObjectName("excludedDisplayedFiles");
  excludedFiles_->setTextFormat(Qt::PlainText);
  excludedFiles_->setWordWrap(true);
  excludedFiles_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  excludedLayout->addWidget(excludedFiles_);
  excludedGroup_->hide();
  layout->addWidget(excludedGroup_);
  layout->addWidget(count_);
  auto *favoriteActions = new QHBoxLayout;
  favoriteActions->addWidget(addFavorites_);
  favoriteActions->addWidget(removeFavorites_);
  layout->addLayout(favoriteActions);
  addFavorites_->setObjectName("addSelectedFavorites");
  removeFavorites_->setObjectName("removeSelectedFavorites");
  addFavorites_->setToolTip(tr("Add selected images to favorites."));
  removeFavorites_->setToolTip(tr("Remove selected images from favorites."));
  addFavoritesAction_->setObjectName("addSelectedFavoritesAction");
  removeFavoritesAction_->setObjectName("removeSelectedFavoritesAction");
  for (auto *view :
       {static_cast<QAbstractItemView *>(list_), static_cast<QAbstractItemView *>(details_)}) {
    view->setContextMenuPolicy(Qt::ActionsContextMenu);
    view->addActions({addFavoritesAction_, removeFavoritesAction_});
  }
  connect(addFavorites_, &QPushButton::clicked, addFavoritesAction_, &QAction::trigger);
  connect(removeFavorites_, &QPushButton::clicked, removeFavoritesAction_, &QAction::trigger);
  connect(addFavoritesAction_, &QAction::triggered, this,
          [this]() { Q_EMIT favoritesRequested(selectedFilePaths(), true); });
  connect(removeFavoritesAction_, &QAction::triggered, this,
          [this]() { Q_EMIT favoritesRequested(selectedFilePaths(), false); });
  layout->addWidget(compare_);
  compare_->setObjectName("compareSelectedImages");
  compare_->setToolTip(tr("Select 2 to 16 images to view side by side. Ctrl-click or Shift-click "
                          "to select multiple images."));
  connect(compare_, &QPushButton::clicked, this,
          [this]() { Q_EMIT compareRequested(selectedFilePaths()); });
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
  connect(favoritesOnly_, &QCheckBox::toggled, this, &ImageListPanelWidget::favoritesOnlyRequested);
  connect(modes, &QActionGroup::triggered, this, [this]() {
    if (views_->currentIndex() != (detailsVisible() ? 1 : 0))
      updateViewMode();
  });
  connect(thumbnailSizeSlider_, &QSlider::valueChanged, this,
          &ImageListPanelWidget::setThumbnailWidth);
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
  width = std::clamp(width, 64, 320);
  if (width == thumbnailWidth_)
    return;
  thumbnailWidth_ = width;
  const QSignalBlocker blocker(thumbnailSizeSlider_);
  thumbnailSizeSlider_->setValue(width);
  updateThumbnailGeometry();
  if (!detailsVisible()) {
    cancelLoading();
    startLoading();
  }
  Q_EMIT thumbnailWidthChanged(width);
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
  addFavorites_->setEnabled(count > 0);
  removeFavorites_->setEnabled(count > 0);
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
