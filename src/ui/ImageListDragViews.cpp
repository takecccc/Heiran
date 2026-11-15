#include "heiran/ui/ImageListDragViews.hpp"

#include "heiran/infra/FavoriteStore.hpp"

#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace heiran::ui {

namespace {
QVariant itemData(const QListWidgetItem *item, int role) {
  return item->data(role);
}
QVariant itemData(const QTreeWidgetItem *item, int role) {
  return item->data(0, role);
}

template <class Item> QMimeData *fileMimeData(QList<Item *> items) {
  std::sort(items.begin(), items.end(), [](const Item *a, const Item *b) {
    return itemData(a, Qt::UserRole + 1).toInt() < itemData(b, Qt::UserRole + 1).toInt();
  });
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  QSet<QString> seen;
  for (const auto *item : items) {
    const auto path = itemData(item, Qt::UserRole + 2).toString();
    const QFileInfo file(path);
    const auto key = heiran::infra::FavoriteStore::pathKey(path);
    if (file.isFile() && !seen.contains(key)) {
      urls.push_back(QUrl::fromLocalFile(file.absoluteFilePath()));
      seen.insert(key);
    }
  }
  mime->setUrls(urls);
  return mime;
}

}  // namespace

void ImageFileListWidget::mousePressEvent(QMouseEvent *event) {
  const auto *item = itemAt(event->position().toPoint());
  if (event->button() == Qt::RightButton && (item == nullptr || item->isSelected())) {
    event->accept();
    return;
  }
  QListWidget::mousePressEvent(event);
}

QStringList ImageFileListWidget::mimeTypes() const {
  return {"text/uri-list"};
}
QMimeData *ImageFileListWidget::mimeData(const QList<QListWidgetItem *> &items) const {
  return fileMimeData(items);
}
Qt::DropActions ImageFileListWidget::supportedDropActions() const {
  return Qt::CopyAction;
}

void ImageFileDetailsWidget::mousePressEvent(QMouseEvent *event) {
  const auto *item = itemAt(event->position().toPoint());
  if (event->button() == Qt::RightButton && (item == nullptr || item->isSelected())) {
    event->accept();
    return;
  }
  QTreeWidget::mousePressEvent(event);
}

QStringList ImageFileDetailsWidget::mimeTypes() const {
  return {"text/uri-list"};
}
QMimeData *ImageFileDetailsWidget::mimeData(const QList<QTreeWidgetItem *> &items) const {
  return fileMimeData(items);
}
Qt::DropActions ImageFileDetailsWidget::supportedDropActions() const {
  return Qt::CopyAction;
}

}  // namespace heiran::ui
