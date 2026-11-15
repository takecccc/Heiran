#pragma once

#include <QListWidget>
#include <QTreeWidget>

namespace heiran::ui {

class ImageFileListWidget final : public QListWidget {
public:
  using QListWidget::QListWidget;

protected:
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
  Qt::DropActions supportedDropActions() const override;
};

class ImageFileDetailsWidget final : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

protected:
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
  Qt::DropActions supportedDropActions() const override;
};

}  // namespace heiran::ui
