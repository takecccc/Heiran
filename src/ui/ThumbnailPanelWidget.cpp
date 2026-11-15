#include "heiran/ui/ThumbnailPanelWidget.hpp"

#include "heiran/ui/ThumbnailCache.hpp"

#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace heiran::ui {

namespace {
constexpr QSize kThumbnailSize(160, 120);
}

ThumbnailPanelWidget::ThumbnailPanelWidget(QWidget *parent)
    : QWidget(parent), list_(new QListWidget(this)), count_(new QLabel(this)),
      empty_(new QLabel(tr("No images to display."), this)) {
  setObjectName("thumbnailPanelWidget");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->addWidget(count_);
  layout->addWidget(list_, 1);
  layout->addWidget(empty_, 1);
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  list_->setObjectName("thumbnailList");
  list_->setAccessibleName(tr("Thumbnails"));
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

ThumbnailPanelWidget::~ThumbnailPanelWidget() {
  cancelLoading();
  loaderPool_.waitForDone();
}

void ThumbnailPanelWidget::cancelLoading() {
  ++generation_;
  if (watcher_) {
    watcher_->cancel();
    watcher_ = nullptr;
  }
}

void ThumbnailPanelWidget::setPages(const QStringList &pagePaths, int currentPage) {
  if (paths_ == pagePaths && list_->count() == pagePaths.size() && !pagePaths.isEmpty()) {
    setCurrentPage(currentPage);
    if (!watcher_) {
      startLoading();
    }
    return;
  }
  cancelLoading();
  paths_ = pagePaths;
  const QSignalBlocker blocker(list_);
  list_->clear();
  count_->setText(tr("%1 items").arg(paths_.size()));
  list_->setVisible(!paths_.isEmpty());
  empty_->setVisible(paths_.isEmpty());
  for (int index = 0; index < paths_.size(); ++index) {
    const QString &path = paths_.at(index);
    const QString caption = path.isEmpty()
                                ? QString::number(index + 1)
                                : QString("%1: %2").arg(index + 1).arg(QFileInfo(path).fileName());
    auto *item = new QListWidgetItem(caption, list_);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    item->setToolTip(path.isEmpty() ? caption : path);
    QPixmap placeholder(kThumbnailSize);
    placeholder.fill(Qt::transparent);
    item->setIcon(QIcon(placeholder));
  }
  setCurrentPage(currentPage);
  startLoading();
}

void ThumbnailPanelWidget::setCurrentPage(int page) {
  const QSignalBlocker blocker(list_);
  if (list_->currentRow() == page) {
    return;
  }
  list_->setCurrentRow(page >= 0 && page < list_->count() ? page : -1);
  if (auto *item = list_->currentItem()) {
    list_->scrollToItem(item, QAbstractItemView::EnsureVisible);
  }
}

void ThumbnailPanelWidget::startLoading() {
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
