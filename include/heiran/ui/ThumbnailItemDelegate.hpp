#pragma once

#include <QStyledItemDelegate>

namespace heiran::ui {

enum ThumbnailItemRole {
  ThumbnailFavoriteRole = Qt::UserRole + 3,
  ThumbnailCellsRole = Qt::UserRole + 4
};

class ThumbnailItemDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
};

}  // namespace heiran::ui
