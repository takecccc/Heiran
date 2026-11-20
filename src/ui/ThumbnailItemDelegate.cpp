#include "heiran/ui/ThumbnailItemDelegate.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace heiran::ui {

void ThumbnailItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
  QStyleOptionViewItem item(option);
  initStyleOption(&item, index);
  QStyledItemDelegate::paint(painter, item, index);

  const auto *style = item.widget ? item.widget->style() : QApplication::style();
  const QRect thumbnail =
      style->subElementRect(QStyle::SE_ItemViewItemDecoration, &item, item.widget)
          .intersected(item.rect);
  if (thumbnail.isEmpty())
    return;
  const bool favorite = index.data(ThumbnailFavoriteRole).toBool();
  const auto cells = index.data(ThumbnailCellsRole).toStringList().join(", ");
  if (!favorite && cells.isEmpty())
    return;

  const auto tokens = AppStyleTokens::forDarkScheme(isDarkPalette());
  auto background = tokens.panel;
  background.setAlpha(230);
  auto font = item.font;
  font.setWeight(QFont::DemiBold);
  const QFontMetrics metrics(font);
  const int height = std::min(metrics.height() + 6, thumbnail.height());
  constexpr int margin = 3;
  const int starWidth = favorite ? std::min(height, thumbnail.width()) : 0;
  painter->save();
  painter->setClipRect(thumbnail, Qt::IntersectClip);
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setFont(font);
  painter->setPen(Qt::NoPen);
  painter->setBrush(background);
  if (favorite) {
    const QRect badge(thumbnail.right() - margin - starWidth + 1, thumbnail.top() + margin,
                      starWidth, height);
    painter->drawRoundedRect(badge, 3, 3);
    painter->setPen(tokens.accentText);
    painter->drawText(badge, Qt::AlignCenter, QStringLiteral("\u2605"));
  }
  const int maximumWidth = thumbnail.width() - starWidth - margin * (favorite ? 3 : 2);
  if (!cells.isEmpty() && maximumWidth > 0) {
    const int width = std::min(metrics.horizontalAdvance(cells) + 10, maximumWidth);
    const QRect badge(thumbnail.left() + margin, thumbnail.top() + margin, width, height);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badge, 3, 3);
    painter->setPen(tokens.text);
    painter->drawText(badge.adjusted(4, 0, -4, 0), Qt::AlignCenter,
                      metrics.elidedText(cells, Qt::ElideRight, std::max(0, width - 8)));
  }
  painter->restore();
}

}  // namespace heiran::ui
