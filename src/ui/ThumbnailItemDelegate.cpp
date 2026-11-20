#include "heiran/ui/ThumbnailItemDelegate.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace heiran::ui {

void ThumbnailItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
  QStyleOptionViewItem item(option);
  initStyleOption(&item, index);
  QStyledItemDelegate::paint(painter, item, index);

  const QRect bounds = item.rect;
  if (bounds.isEmpty())
    return;
  const bool favorite = index.data(ThumbnailFavoriteRole).toBool();
  const auto cells = index.data(ThumbnailCellsRole).toStringList().join(", ");
  if (!favorite && cells.isEmpty())
    return;

  const auto tokens = AppStyleTokens::current();
  auto background = tokens.panel;
  background.setAlpha(230);
  auto font = item.font;
  font.setWeight(QFont::DemiBold);
  const QFontMetrics metrics(font);
  constexpr int margin = 3;
  const int height = std::min(metrics.height() + 6, bounds.height() - 2 * margin);
  if (height <= 0 || bounds.width() <= 2 * margin)
    return;
  const int starWidth = favorite ? std::min(height, bounds.width() - 2 * margin) : 0;
  painter->save();
  painter->setClipRect(bounds, Qt::IntersectClip);
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setFont(font);
  painter->setPen(Qt::NoPen);
  painter->setBrush(background);
  if (favorite) {
    const QRect badge(bounds.right() - margin - starWidth + 1, bounds.top() + margin, starWidth,
                      height);
    painter->drawRoundedRect(badge, 3, 3);
    painter->setPen(tokens.accentText);
    painter->drawText(badge, Qt::AlignCenter, QStringLiteral("\u2605"));
  }
  const int maximumWidth = bounds.width() - starWidth - margin * (favorite ? 3 : 2);
  if (!cells.isEmpty() && maximumWidth > 0) {
    const int width = std::min(metrics.horizontalAdvance(cells) + 10, maximumWidth);
    const QRect badge(bounds.left() + margin, bounds.top() + margin, width, height);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badge, 3, 3);
    painter->setPen(tokens.text);
    painter->drawText(badge.adjusted(4, 0, -4, 0), Qt::AlignCenter,
                      metrics.elidedText(cells, Qt::ElideRight, std::max(0, width - 8)));
  }
  painter->restore();
}

}  // namespace heiran::ui
