#include "heiran/ui/ImagePrinter.hpp"

#include <QPageLayout>
#include <QPainter>
#include <QPrinter>
#include <algorithm>

namespace heiran::ui {

QRect ImagePrinter::targetRect(const QSize &imageSize, const QRect &pageRect) {
  if (imageSize.isEmpty() || pageRect.isEmpty()) {
    return {};
  }

  const double scale = std::min(static_cast<double>(pageRect.width()) / imageSize.width(),
                                static_cast<double>(pageRect.height()) / imageSize.height());
  const int width = std::max(1, static_cast<int>(imageSize.width() * scale));
  const int height = std::max(1, static_cast<int>(imageSize.height() * scale));
  return QRect(pageRect.left() + (pageRect.width() - width) / 2,
               pageRect.top() + (pageRect.height() - height) / 2, width, height);
}

bool ImagePrinter::print(QPrinter *printer, const QImage &image) {
  if (printer == nullptr || image.isNull()) {
    return false;
  }

  // 横長の画像は横向きの用紙へ置く。縦向きのままだと、用紙の幅で大きさが決まって
  // 小さく印刷される。
  printer->setPageOrientation(image.width() > image.height() ? QPageLayout::Landscape
                                                             : QPageLayout::Portrait);

  QPainter painter;
  if (!painter.begin(printer)) {
    return false;
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const QRect pageRect = printer->pageLayout().paintRectPixels(printer->resolution());
  painter.drawImage(targetRect(image.size(), QRect(QPoint(0, 0), pageRect.size())), image);
  return painter.end();
}

}  // namespace heiran::ui
