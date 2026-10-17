#include "heiran/domain/ImageTransform.hpp"

#include <algorithm>
#include <cmath>

namespace heiran::domain {

namespace {

QPoint applyFlip(const QPoint &pixel, const QSize &size, bool flipX, bool flipY) {
  const int x = flipX ? size.width() - 1 - pixel.x() : pixel.x();
  const int y = flipY ? size.height() - 1 - pixel.y() : pixel.y();
  return {x, y};
}

QPoint applyRotation(const QPoint &pixel, const QSize &size, int quarterTurns) {
  switch (quarterTurns) {
    case 1:
      return {size.height() - 1 - pixel.y(), pixel.x()};
    case 2:
      return {size.width() - 1 - pixel.x(), size.height() - 1 - pixel.y()};
    case 3:
      return {pixel.y(), size.width() - 1 - pixel.x()};
    case 0:
    default:
      return pixel;
  }
}

QPoint reverseRotation(const QPoint &pixel, const QSize &size, int quarterTurns) {
  switch (quarterTurns) {
    case 1:
      return {pixel.y(), size.height() - 1 - pixel.x()};
    case 2:
      return {size.width() - 1 - pixel.x(), size.height() - 1 - pixel.y()};
    case 3:
      return {size.width() - 1 - pixel.y(), pixel.x()};
    case 0:
    default:
      return pixel;
  }
}

}  // namespace

int normalizeQuarterTurns(int rotationQuarterTurns) {
  return ((rotationQuarterTurns % 4) + 4) % 4;
}

QSize transformedImageSize(const QSize &sourceSize, int rotationQuarterTurns) {
  if (!sourceSize.isValid()) {
    return {};
  }

  const int quarterTurns = normalizeQuarterTurns(rotationQuarterTurns);
  if (quarterTurns == 1 || quarterTurns == 3) {
    return {sourceSize.height(), sourceSize.width()};
  }
  return sourceSize;
}

QPoint mapSourceToTransformed(const QPoint &sourcePixel, const QSize &sourceSize,
                              const ImageOrientation &orientation) {
  const int quarterTurns = normalizeQuarterTurns(orientation.rotationQuarterTurns);
  const QPoint rotated = applyRotation(sourcePixel, sourceSize, quarterTurns);
  const QSize rotatedSize = transformedImageSize(sourceSize, quarterTurns);
  return applyFlip(rotated, rotatedSize, orientation.flipX, orientation.flipY);
}

QPoint mapTransformedToSource(const QPoint &transformedPixel, const QSize &sourceSize,
                              const ImageOrientation &orientation) {
  const int quarterTurns = normalizeQuarterTurns(orientation.rotationQuarterTurns);
  const QSize rotatedSize = transformedImageSize(sourceSize, quarterTurns);
  const QPoint rotated =
      applyFlip(transformedPixel, rotatedSize, orientation.flipX, orientation.flipY);
  return reverseRotation(rotated, sourceSize, quarterTurns);
}

std::optional<QPoint> pixelAtImagePosition(const QPointF &imagePosition, const QSize &imageSize) {
  if (imageSize.isEmpty()) {
    return std::nullopt;
  }

  const int x = static_cast<int>(std::floor(imagePosition.x()));
  const int y = static_cast<int>(std::floor(imagePosition.y()));
  if (x < 0 || y < 0 || x >= imageSize.width() || y >= imageSize.height()) {
    return std::nullopt;
  }
  return QPoint(x, y);
}

QRectF normalizeSelectionRect(const QPointF &startImagePosition, const QPointF &endImagePosition) {
  const double left = std::min(startImagePosition.x(), endImagePosition.x());
  const double top = std::min(startImagePosition.y(), endImagePosition.y());
  const double right = std::max(startImagePosition.x(), endImagePosition.x());
  const double bottom = std::max(startImagePosition.y(), endImagePosition.y());
  return {QPointF(left, top), QPointF(right, bottom)};
}

QRect clipSelectionRect(const QRectF &selectionRect, const QSize &imageSize) {
  if (imageSize.isEmpty()) {
    return {};
  }

  const int x0 =
      std::clamp(static_cast<int>(std::floor(selectionRect.left())), 0, imageSize.width());
  const int y0 =
      std::clamp(static_cast<int>(std::floor(selectionRect.top())), 0, imageSize.height());
  const int x1 =
      std::clamp(static_cast<int>(std::ceil(selectionRect.right())), 0, imageSize.width());
  const int y1 =
      std::clamp(static_cast<int>(std::ceil(selectionRect.bottom())), 0, imageSize.height());
  return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

}  // namespace heiran::domain
