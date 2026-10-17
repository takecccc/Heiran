#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <optional>

namespace heiran::domain {

struct ImageOrientation {
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
};

int normalizeQuarterTurns(int rotationQuarterTurns);

QSize transformedImageSize(const QSize &sourceSize, int rotationQuarterTurns);

QPoint mapSourceToTransformed(const QPoint &sourcePixel, const QSize &sourceSize,
                              const ImageOrientation &orientation);

QPoint mapTransformedToSource(const QPoint &transformedPixel, const QSize &sourceSize,
                              const ImageOrientation &orientation);

// 浮動小数の画像座標を画素位置へ落とす。
// 丸めは floor とし、右端と下端ちょうどは半開区間の考え方で範囲外として扱う。
// 範囲外なら値を持たない。
std::optional<QPoint> pixelAtImagePosition(const QPointF &imagePosition, const QSize &imageSize);

QRectF normalizeSelectionRect(const QPointF &startImagePosition, const QPointF &endImagePosition);

QRect clipSelectionRect(const QRectF &selectionRect, const QSize &imageSize);

}  // namespace heiran::domain
