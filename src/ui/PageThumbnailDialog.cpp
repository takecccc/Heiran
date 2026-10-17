#include "heiran/ui/PageThumbnailDialog.hpp"

#include "heiran/ui/ThumbnailCache.hpp"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QListWidget>
#include <QPixmap>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace heiran::ui {

namespace {

constexpr int kThumbnailWidth = 160;
constexpr int kThumbnailHeight = 120;

QSize thumbnailSize() {
  return {kThumbnailWidth, kThumbnailHeight};
}

}  // namespace

PageThumbnailDialog::PageThumbnailDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Pages"));
  resize(760, 560);

  auto *layout = new QVBoxLayout(this);
  listWidget_ = new QListWidget(this);
  listWidget_->setViewMode(QListView::IconMode);
  listWidget_->setIconSize(thumbnailSize());
  // 升目の高さはサムネイルにページ番号の行を足した分。
  listWidget_->setGridSize(QSize(kThumbnailWidth + 24, kThumbnailHeight + 44));
  listWidget_->setResizeMode(QListView::Adjust);
  listWidget_->setMovement(QListView::Static);
  listWidget_->setUniformItemSizes(true);
  listWidget_->setWordWrap(true);
  layout->addWidget(listWidget_, 1);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttonBox);

  // 1 回のクリックで飛ぶ。探して選ぶための窓なので、選んだ時点で用は済む。
  connect(listWidget_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    selectedPage_ = listWidget_->row(item);
    accept();
  });

  connect(&loaderWatcher_, &QFutureWatcher<QImage>::resultReadyAt, this,
          [this](int index) { applyThumbnail(index, loaderWatcher_.resultAt(index)); });
}

PageThumbnailDialog::~PageThumbnailDialog() {
  // 窓を閉じても読み込みは走り続ける。結果を受ける先が消えるため、待ってから壊す。
  loaderWatcher_.cancel();
  loaderWatcher_.waitForFinished();
}

void PageThumbnailDialog::setPages(const QStringList &pagePaths, int currentPage) {
  listWidget_->clear();
  selectedPage_ = -1;

  for (int index = 0; index < pagePaths.size(); ++index) {
    const QString &path = pagePaths.at(index);
    const QString caption = path.isEmpty()
                                ? QString("%1").arg(index + 1)
                                : QString("%1: %2").arg(index + 1).arg(QFileInfo(path).fileName());
    auto *item = new QListWidgetItem(caption, listWidget_);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    item->setToolTip(path.isEmpty() ? caption : path);
    // 読み込みが済むまで大きさを保つ。届くたびに升目が動くと目で追えない。
    QPixmap placeholder(thumbnailSize());
    placeholder.fill(Qt::transparent);
    item->setIcon(QIcon(placeholder));
  }

  if (currentPage >= 0 && currentPage < listWidget_->count()) {
    listWidget_->setCurrentRow(currentPage);
    listWidget_->scrollToItem(listWidget_->item(currentPage), QAbstractItemView::PositionAtCenter);
  }

  startLoading(pagePaths);
}

void PageThumbnailDialog::startLoading(const QStringList &pagePaths) {
  loaderWatcher_.cancel();
  loaderWatcher_.waitForFinished();
  if (pagePaths.isEmpty()) {
    return;
  }

  const QSize size = thumbnailSize();
  loaderWatcher_.setFuture(
      QtConcurrent::mapped(&loaderPool_, pagePaths, [size](const QString &path) {
        return path.isEmpty() ? QImage() : ThumbnailCache::standard().loadOrCreate(path, size);
      }));
}

void PageThumbnailDialog::applyThumbnail(int index, const QImage &thumbnail) {
  if (index < 0 || index >= listWidget_->count() || thumbnail.isNull()) {
    return;
  }

  listWidget_->item(index)->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
}

}  // namespace heiran::ui
