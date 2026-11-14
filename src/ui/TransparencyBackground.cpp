#include "heiran/ui/TransparencyBackground.hpp"

#include <QPainter>
#include <QPixmap>
#include <cmath>

namespace heiran::ui {

QColor TransparencyBackground::colorAt(const QPoint &position) const {
  if (pattern == TransparencyPattern::Solid) {
    return solidColor;
  }
  const int x = static_cast<int>(std::floor(double(position.x()) / kTransparencyCheckerSize));
  const int y = static_cast<int>(std::floor(double(position.y()) / kTransparencyCheckerSize));
  return (x + y) % 2 == 0 ? checkerColor1 : checkerColor2;
}

QBrush transparencyBackgroundBrush(const TransparencyBackground &background) {
  if (background.pattern == TransparencyPattern::Solid) {
    return QBrush(background.solidColor);
  }
  QPixmap tile(kTransparencyCheckerSize * 2, kTransparencyCheckerSize * 2);
  tile.fill(background.checkerColor1);
  QPainter painter(&tile);
  painter.fillRect(kTransparencyCheckerSize, 0, kTransparencyCheckerSize, kTransparencyCheckerSize,
                   background.checkerColor2);
  painter.fillRect(0, kTransparencyCheckerSize, kTransparencyCheckerSize, kTransparencyCheckerSize,
                   background.checkerColor2);
  painter.end();
  return QBrush(tile);
}

}  // namespace heiran::ui
