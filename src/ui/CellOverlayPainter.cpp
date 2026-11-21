#include "heiran/ui/CellOverlayPainter.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

constexpr QColor kSelectionColor(255, 210, 64);
constexpr QColor kPreviewSelectionColor(120, 200, 255);
constexpr QColor kCursorColor(255, 235, 120);
// 線の下に敷く暗い縁。明るい画像でも暗い画像でも、色の付いた細い線が背景に溶けないようにする。
constexpr QColor kOutlineColor(0, 0, 0, 190);
constexpr double kOutlineExtraWidth = 2.0;
constexpr QColor kLabelBackgroundColor(18, 20, 24, 180);
constexpr QColor kLabelTextColor(245, 247, 250);

// 選択範囲は画像座標のまま描く。線の太さは倍率で割り戻して、拡大しても
// 太くならないようにする。
void drawHistogramHighlight(QPainter *painter, const CellOverlayInput &input) {
  if (!input.histogramHighlight || input.histogramHighlight->isNull()) {
    return;
  }
  const auto &mask = *input.histogramHighlight;
  const QRectF destination((mask.offset().x() - input.offsetX) * input.scale,
                           (mask.offset().y() - input.offsetY) * input.scale,
                           mask.width() * input.scale, mask.height() * input.scale);
  painter->save();
  painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter->drawImage(destination, mask, mask.rect());
  painter->restore();
}

void drawSelections(QPainter *painter, const CellOverlayInput &input) {
  if (!input.selectionRect && !input.previewSelectionRect) {
    return;
  }

  painter->save();
  // 拡大した選択範囲が枠の外へはみ出さないようにする。倍率を掛ける前に置くため、
  // 範囲は論理座標のままでよい。`QPainter::viewport()` はデバイス座標を返すので
  // 使わない。devicePixelRatio が 1 でない画面でずれる。
  if (!input.canvasSize.isEmpty()) {
    painter->setClipRect(QRect(QPoint(0, 0), input.canvasSize));
  }
  painter->scale(input.scale, input.scale);
  painter->translate(-input.offsetX, -input.offsetY);
  painter->setBrush(Qt::NoBrush);

  const auto drawOne = [painter, &input](const QRectF &rect, const QColor &color) {
    // 暗い縁を先に敷いてから、色の線を重ねる。
    painter->setPen(QPen(kOutlineColor, (1.5 + kOutlineExtraWidth) / input.scale));
    painter->drawRect(rect);
    painter->setPen(QPen(color, 1.5 / input.scale));
    painter->drawRect(rect);
  };
  if (input.selectionRect) {
    drawOne(*input.selectionRect, kSelectionColor);
  }
  if (input.previewSelectionRect) {
    drawOne(*input.previewSelectionRect, kPreviewSelectionColor);
  }
  painter->restore();
}

// 中心を空けた十字で指す。真ん中まで線を引くと、指している画素そのものが
// 隠れて色を確かめられない。
//
// 線は、暗い縁の上に色の線を重ねて描く。色だけでは、同じような色の画像の上で見えなくなる。
void drawCrosshair(QPainter *painter, const QPointF &center, double armLength, double gapRadius,
                   const std::optional<QColor> &lineColor = std::nullopt) {
  const auto drawArms = [&]() {
    painter->drawLine(QPointF(center.x() - armLength, center.y()),
                      QPointF(center.x() - gapRadius, center.y()));
    painter->drawLine(QPointF(center.x() + gapRadius, center.y()),
                      QPointF(center.x() + armLength, center.y()));
    painter->drawLine(QPointF(center.x(), center.y() - armLength),
                      QPointF(center.x(), center.y() - gapRadius));
    painter->drawLine(QPointF(center.x(), center.y() + gapRadius),
                      QPointF(center.x(), center.y() + armLength));
  };
  if (!lineColor) {
    painter->setPen(QPen(kOutlineColor, 1 + kOutlineExtraWidth));
    drawArms();
  }
  painter->setPen(QPen(lineColor ? *lineColor : kCursorColor, 1));
  drawArms();
}

// 十字の腕の下にある、表示している色を集める。画像の外は飛ばす。
QVector<QRgb> collectCursorSamples(const CellOverlayInput &input, const QPointF &center,
                                   double armLength, double gapRadius) {
  QVector<QRgb> samples;
  if (input.sampleImage == nullptr || input.sampleImage->isNull()) {
    return samples;
  }
  const QImage &image = *input.sampleImage;
  const auto sampleAt = [&](double canvasX, double canvasY) {
    const int x = static_cast<int>(std::floor(input.offsetX + canvasX / input.scale));
    const int y = static_cast<int>(std::floor(input.offsetY + canvasY / input.scale));
    if (x >= 0 && y >= 0 && x < image.width() && y < image.height()) {
      samples.push_back(image.pixel(x, y));
    }
  };
  sampleAt(center.x(), center.y());
  for (double distance = gapRadius; distance <= armLength; distance += 1.0) {
    sampleAt(center.x() - distance, center.y());
    sampleAt(center.x() + distance, center.y());
    sampleAt(center.x(), center.y() - distance);
    sampleAt(center.x(), center.y() + distance);
  }
  return samples;
}

// `Contrast` のときの線の色。`Outline` なら空 (固定の色と縁で描く)。
std::optional<QColor> cursorLineColor(const CellOverlayInput &input, const QPointF &center,
                                      double armLength, double gapRadius) {
  if (input.cursorStyle != CursorStyle::Contrast) {
    return std::nullopt;
  }
  return chooseContrastColor(collectCursorSamples(input, center, armLength, gapRadius));
}

void drawSharedCursor(QPainter *painter, const CellOverlayInput &input) {
  if (!input.sharedCursorPosition) {
    return;
  }

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, false);

  if (input.scale >= 1.0) {
    // 等倍以上では画素の中心を指す。画素の枠に合わせると、どの画素を見ているかが
    // はっきりする。
    const int pixelX = static_cast<int>(std::floor(input.sharedCursorPosition->x()));
    const int pixelY = static_cast<int>(std::floor(input.sharedCursorPosition->y()));
    const QRectF pixelRect((pixelX - input.offsetX) * input.scale,
                           (pixelY - input.offsetY) * input.scale, input.scale, input.scale);
    // 腕と隙間は画素の大きさに合わせるが、極端にならないよう幅を切る。
    const double armLength = std::max(6.0, std::min(14.0, input.scale * 0.75));
    const double gapRadius = std::max(3.0, std::min(8.0, input.scale * 0.25));
    drawCrosshair(painter, pixelRect.center(), armLength, gapRadius,
                  cursorLineColor(input, pixelRect.center(), armLength, gapRadius));
  } else {
    // 縮小時は画素より十字のほうが大きくなるため、位置だけを指す。
    constexpr double kArmLength = 8.0;
    constexpr double kGapRadius = 3.0;
    const QPointF center((input.sharedCursorPosition->x() - input.offsetX) * input.scale,
                         (input.sharedCursorPosition->y() - input.offsetY) * input.scale);
    drawCrosshair(painter, center, kArmLength, kGapRadius,
                  cursorLineColor(input, center, kArmLength, kGapRadius));
  }
  painter->restore();
}

void drawLabel(QPainter *painter, const CellOverlayInput &input) {
  if (input.labelRect.isEmpty()) {
    return;
  }

  painter->save();
  const QString elidedText = painter->fontMetrics().elidedText(
      input.labelText, Qt::ElideRight, std::max(0, input.canvasSize.width() - 32));
  painter->setPen(Qt::NoPen);
  painter->setBrush(kLabelBackgroundColor);
  painter->drawRoundedRect(input.labelRect, 4, 4);
  painter->setPen(kLabelTextColor);
  painter->drawText(input.labelRect.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    elidedText);
  painter->restore();
}

}  // namespace

void paintCellOverlays(QPainter *painter, const CellOverlayInput &input) {
  if (painter == nullptr || input.scale <= 0.0) {
    return;
  }

  drawHistogramHighlight(painter, input);
  drawSelections(painter, input);
  drawSharedCursor(painter, input);
  drawLabel(painter, input);
}

}  // namespace heiran::ui
