#include "heiran/ui/ImageListPanelWidget.hpp"

#include "heiran/ui/ThumbnailCache.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace heiran::ui {

namespace {
constexpr QSize kThumbnailSize(160, 120);
}

ImageListPanelWidget::ImageListPanelWidget(QWidget *parent)
    : QWidget(parent), list_(new QListWidget(this)), count_(new QLabel(this)),
      empty_(new QLabel(tr("No images to display."), this)),
      favoritesOnly_(new QCheckBox(tr("Favorites only"), this)), viewMode_(new QComboBox(this)),
      views_(new QStackedWidget(this)), details_(new QTreeWidget(this)) {
  setObjectName("imageListPanelWidget");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  auto *modeLayout = new QHBoxLayout;
  auto *modeLabel = new QLabel(tr("View:"), this);
  modeLabel->setBuddy(viewMode_);
  modeLayout->addWidget(modeLabel);
  modeLayout->addWidget(viewMode_, 1);
  viewMode_->setObjectName("imageListViewMode");
  viewMode_->setAccessibleName(tr("View mode"));
  viewMode_->addItems({tr("Thumbnails"), tr("Details")});
  favoritesOnly_->setObjectName("imageListFavoritesOnly");
  layout->addLayout(modeLayout);
  layout->addWidget(favoritesOnly_);
  layout->addWidget(count_);
  layout->addWidget(views_, 1);
  layout->addWidget(empty_, 1);
  views_->addWidget(list_);
  views_->addWidget(details_);
  details_->setObjectName("imageDetailsList");
  details_->setAccessibleName(tr("Image details"));
  details_->setHeaderLabels({QStringLiteral("\u2605"), tr("File name"), tr("Width (px)"),
                             tr("Height (px)"), tr("File size")});
  details_->headerItem()->setToolTip(0, tr("Favorite"));
  details_->setRootIsDecorated(false);
  details_->setUniformRowHeights(true);
  details_->setSelectionMode(QAbstractItemView::SingleSelection);
  details_->setColumnWidth(0, 32);
  details_->setColumnWidth(1, 160);
  details_->setColumnWidth(2, 80);
  details_->setColumnWidth(3, 80);
  details_->setColumnWidth(4, 120);
  details_->header()->setStretchLastSection(false);
  detailsPool_.setMaxThreadCount(1);
  connect(details_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
    if (item) {
      Q_EMIT pageSelected(details_->indexOfTopLevelItem(item));
    }
  });
  connect(favoritesOnly_, &QCheckBox::toggled, this, &ImageListPanelWidget::favoritesOnlyRequested);
  connect(viewMode_, &QComboBox::currentIndexChanged, this, [this]() { updateViewMode(); });
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  list_->setObjectName("thumbnailList");
  list_->setAccessibleName(tr("Image List"));
  list_->setViewMode(QListView::IconMode);
  list_->setIconSize(kThumbnailSize);
  list_->setGridSize(QSize(184, 164));
  list_->setResizeMode(QListView::Adjust);
  list_->setMovement(QListView::Static);
  list_->setUniformItemSizes(true);
  list_->setWordWrap(true);
  connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0) {
      Q_EMIT pageSelected(row);
    }
  });
  setPages({}, -1);
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
  return viewMode_->currentIndex() == 1;
}

void ImageListPanelWidget::setDetailsVisible(bool details) {
  viewMode_->setCurrentIndex(details ? 1 : 0);
}

void ImageListPanelWidget::updateViewMode() {
  cancelLoading();
  views_->setCurrentIndex(detailsVisible() ? 1 : 0);
  if (detailsVisible()) {
    startDetailsLoading();
  } else {
    startLoading();
  }
  Q_EMIT detailsVisibleChanged(detailsVisible());
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
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    item->setData(Qt::UserRole, caption);
    item->setToolTip(path.isEmpty() ? caption : path);
    QPixmap placeholder(kThumbnailSize);
    placeholder.fill(Qt::transparent);
    item->setIcon(QIcon(placeholder));
    auto *detail = new QTreeWidgetItem(
        details_, {QStringLiteral("\u2014"), QFileInfo(path).fileName(), "...", "...", "..."});
    detail->setToolTip(1, path);
    detail->setTextAlignment(0, Qt::AlignCenter);
    for (int column = 2; column < 5; ++column) {
      detail->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  setCurrentPage(currentPage);
  if (detailsVisible()) {
    startDetailsLoading();
  } else {
    startLoading();
  }
}

void ImageListPanelWidget::setCurrentPage(int page) {
  const QSignalBlocker blocker(list_);
  const QSignalBlocker detailsBlocker(details_);
  auto *detail =
      page >= 0 && page < details_->topLevelItemCount() ? details_->topLevelItem(page) : nullptr;
  if (details_->currentItem() != detail) {
    details_->setCurrentItem(detail);
    if (detail) {
      details_->scrollToItem(detail, QAbstractItemView::EnsureVisible);
    }
  }
  if (list_->currentRow() != page) {
    list_->setCurrentRow(page >= 0 && page < list_->count() ? page : -1);
    if (auto *item = list_->currentItem()) {
      list_->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
  }
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
    const auto caption = item->data(Qt::UserRole).toString();
    item->setText((favorite ? QStringLiteral("\u2605 ") : QString()) + caption);
    const auto description = favorite ? tr("Favorite") : tr("Not a favorite");
    item->setData(Qt::AccessibleDescriptionRole, description);
    auto *detail = details_->topLevelItem(index);
    detail->setText(0, favorite ? QStringLiteral("\u2605") : QStringLiteral("\u2014"));
    detail->setToolTip(0, description);
    detail->setData(0, Qt::AccessibleDescriptionRole, description);
  }
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
  watcher->setFuture(QtConcurrent::mapped(&loaderPool_, paths_, [](const QString &path) {
    return path.isEmpty() ? QImage()
                          : ThumbnailCache::standard().loadOrCreate(path, kThumbnailSize);
  }));
}

}  // namespace heiran::ui
