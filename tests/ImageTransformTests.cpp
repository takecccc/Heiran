#include "heiran/domain/ImageTransform.hpp"

#include <QImage>
#include <QSet>
#include <QTransform>
#include <QtTest>

using heiran::domain::ImageOrientation;

namespace {

QImage makeSourceImage(int width, int height) {
  QImage image(width, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.setPixel(x, y, qRgb(x * 16 + 1, y * 16 + 1, 0));
    }
  }
  return image;
}

QImage applyDisplayTransform(const QImage &source, const ImageOrientation &orientation) {
  QImage result = source;
  const int quarterTurns = heiran::domain::normalizeQuarterTurns(orientation.rotationQuarterTurns);
  if (quarterTurns != 0) {
    QTransform transform;
    transform.rotate(90.0 * quarterTurns);
    result = result.transformed(transform);
  }
  if (orientation.flipX || orientation.flipY) {
    result = result.mirrored(orientation.flipX, orientation.flipY);
  }
  return result;
}

}  // namespace

class ImageTransformTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void normalizesQuarterTurns() {
    QCOMPARE(heiran::domain::normalizeQuarterTurns(0), 0);
    QCOMPARE(heiran::domain::normalizeQuarterTurns(4), 0);
    QCOMPARE(heiran::domain::normalizeQuarterTurns(5), 1);
    QCOMPARE(heiran::domain::normalizeQuarterTurns(-1), 3);
    QCOMPARE(heiran::domain::normalizeQuarterTurns(-4), 0);
  }

  void keepsSizeForEvenQuarterTurns() {
    const QSize sourceSize(4, 3);
    QCOMPARE(heiran::domain::transformedImageSize(sourceSize, 0), QSize(4, 3));
    QCOMPARE(heiran::domain::transformedImageSize(sourceSize, 2), QSize(4, 3));
  }

  void swapsSizeForOddQuarterTurns() {
    const QSize sourceSize(4, 3);
    QCOMPARE(heiran::domain::transformedImageSize(sourceSize, 1), QSize(3, 4));
    QCOMPARE(heiran::domain::transformedImageSize(sourceSize, 3), QSize(3, 4));
  }

  void mapsIdentityTransform() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation;
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 0}, sourceSize, orientation), QPoint(0, 0));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 2}, sourceSize, orientation), QPoint(3, 2));
  }

  void mapsQuarterTurnClockwise() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.rotationQuarterTurns = 1};
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 0}, sourceSize, orientation), QPoint(2, 0));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 0}, sourceSize, orientation), QPoint(2, 3));
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 2}, sourceSize, orientation), QPoint(0, 0));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 2}, sourceSize, orientation), QPoint(0, 3));
  }

  void mapsHalfTurn() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.rotationQuarterTurns = 2};
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 0}, sourceSize, orientation), QPoint(3, 2));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 2}, sourceSize, orientation), QPoint(0, 0));
  }

  void mapsThreeQuarterTurn() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.rotationQuarterTurns = 3};
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 0}, sourceSize, orientation), QPoint(0, 3));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 2}, sourceSize, orientation), QPoint(2, 0));
  }

  void mapsHorizontalFlip() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.flipX = true};
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 1}, sourceSize, orientation), QPoint(3, 1));
    QCOMPARE(heiran::domain::mapSourceToTransformed({3, 1}, sourceSize, orientation), QPoint(0, 1));
  }

  void mapsVerticalFlip() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.flipY = true};
    QCOMPARE(heiran::domain::mapSourceToTransformed({1, 0}, sourceSize, orientation), QPoint(1, 2));
    QCOMPARE(heiran::domain::mapSourceToTransformed({1, 2}, sourceSize, orientation), QPoint(1, 0));
  }

  void appliesFlipAfterRotation() {
    const QSize sourceSize(4, 3);
    const ImageOrientation rotateThenFlip{.rotationQuarterTurns = 1, .flipX = true};

    // 90 度回転で (0, 0) は (2, 0) へ移り、幅 3 の回転後画像で左右反転して (0, 0) になる。
    QCOMPARE(heiran::domain::mapSourceToTransformed({0, 0}, sourceSize, rotateThenFlip),
             QPoint(0, 0));

    // 先に反転してから回転すると (2, 3) になり、結果が異なることを確認する。
    const QPoint flippedFirst{sourceSize.width() - 1, 0};
    const ImageOrientation rotateOnly{.rotationQuarterTurns = 1};
    QCOMPARE(heiran::domain::mapSourceToTransformed(flippedFirst, sourceSize, rotateOnly),
             QPoint(2, 3));
  }

  void roundTripsEveryOrientation() {
    const QSize sourceSize(4, 3);
    for (int quarterTurns = 0; quarterTurns < 4; ++quarterTurns) {
      for (const bool flipX : {false, true}) {
        for (const bool flipY : {false, true}) {
          const ImageOrientation orientation{
              .rotationQuarterTurns = quarterTurns, .flipX = flipX, .flipY = flipY};
          for (int y = 0; y < sourceSize.height(); ++y) {
            for (int x = 0; x < sourceSize.width(); ++x) {
              const QPoint sourcePixel(x, y);
              const QPoint transformedPixel =
                  heiran::domain::mapSourceToTransformed(sourcePixel, sourceSize, orientation);
              QCOMPARE(
                  heiran::domain::mapTransformedToSource(transformedPixel, sourceSize, orientation),
                  sourcePixel);
            }
          }
        }
      }
    }
  }

  void matchesQImageTransformResult() {
    const QImage source = makeSourceImage(4, 3);
    const QSize sourceSize = source.size();

    for (int quarterTurns = 0; quarterTurns < 4; ++quarterTurns) {
      for (const bool flipX : {false, true}) {
        for (const bool flipY : {false, true}) {
          const ImageOrientation orientation{
              .rotationQuarterTurns = quarterTurns, .flipX = flipX, .flipY = flipY};
          const QImage transformed = applyDisplayTransform(source, orientation);
          QCOMPARE(transformed.size(),
                   heiran::domain::transformedImageSize(sourceSize, quarterTurns));

          for (int y = 0; y < sourceSize.height(); ++y) {
            for (int x = 0; x < sourceSize.width(); ++x) {
              const QPoint transformedPixel =
                  heiran::domain::mapSourceToTransformed({x, y}, sourceSize, orientation);
              QCOMPARE(transformed.pixel(transformedPixel), source.pixel(x, y));
            }
          }
        }
      }
    }
  }

  void normalizesSelectionRectFromEitherDragDirection() {
    const QRectF forward =
        heiran::domain::normalizeSelectionRect(QPointF(1.0, 2.0), QPointF(5.0, 7.0));
    const QRectF backward =
        heiran::domain::normalizeSelectionRect(QPointF(5.0, 7.0), QPointF(1.0, 2.0));
    QCOMPARE(forward, backward);
    QCOMPARE(forward.left(), 1.0);
    QCOMPARE(forward.top(), 2.0);
    QCOMPARE(forward.right(), 5.0);
    QCOMPARE(forward.bottom(), 7.0);
  }

  void clipsSelectionRectToHalfOpenPixelRange() {
    const QSize imageSize(4, 3);
    const QRect clipped =
        heiran::domain::clipSelectionRect(QRectF(QPointF(1.2, 0.8), QPointF(3.4, 2.1)), imageSize);
    QCOMPARE(clipped, QRect(1, 0, 3, 3));
  }

  void clipsSelectionRectToImageBounds() {
    const QSize imageSize(4, 3);
    const QRect clipped = heiran::domain::clipSelectionRect(
        QRectF(QPointF(-5.0, -5.0), QPointF(99.0, 99.0)), imageSize);
    QCOMPARE(clipped, QRect(0, 0, 4, 3));
  }

  void selectsSingleBoundaryPixel() {
    const QSize imageSize(4, 3);
    const QRect clipped =
        heiran::domain::clipSelectionRect(QRectF(QPointF(3.0, 2.0), QPointF(4.0, 3.0)), imageSize);
    QCOMPARE(clipped, QRect(3, 2, 1, 1));
  }

  void returnsEmptyRectForEmptyImage() {
    QCOMPARE(heiran::domain::clipSelectionRect(QRectF(QPointF(0.0, 0.0), QPointF(1.0, 1.0)),
                                               QSize(0, 0)),
             QRect());
  }

  // docs/テスト観点.md 画素値 Case 13: 画素値表示の基本取得
  void resolvesPixelInsideImage() {
    const QSize imageSize(4, 3);

    QCOMPARE(heiran::domain::pixelAtImagePosition(QPointF(0.0, 0.0), imageSize).value(),
             QPoint(0, 0));
    QCOMPARE(heiran::domain::pixelAtImagePosition(QPointF(1.9, 2.1), imageSize).value(),
             QPoint(1, 2));
  }

  // docs/テスト観点.md 画素値 Case 14: 右端と下端の境界
  void treatsRightAndBottomEdgeAsOutOfBounds() {
    const QSize imageSize(4, 3);

    // 最後の画素の内側は取得できる。
    QCOMPARE(heiran::domain::pixelAtImagePosition(QPointF(3.999, 2.999), imageSize).value(),
             QPoint(3, 2));
    // 右端と下端ちょうどは半開区間で範囲外とする。
    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(4.0, 1.0), imageSize).has_value());
    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(1.0, 3.0), imageSize).has_value());
  }

  void treatsNegativePositionAsOutOfBounds() {
    const QSize imageSize(4, 3);

    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(-0.1, 1.0), imageSize).has_value());
    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(1.0, -0.1), imageSize).has_value());
  }

  void hasNoPixelForEmptyImage() {
    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(0.0, 0.0), QSize()).has_value());
    QVERIFY(!heiran::domain::pixelAtImagePosition(QPointF(0.0, 0.0), QSize(0, 5)).has_value());
  }

  // docs/テスト観点.md 画素値 Case 19: 回転と反転込みの逆変換取得
  void resolvesSourcePixelThroughRotationAndFlip() {
    const QSize sourceSize(4, 3);
    const ImageOrientation orientation{.rotationQuarterTurns = 1, .flipX = true};
    const QSize displaySize = heiran::domain::transformedImageSize(sourceSize, 1);

    // 表示上の左上を指したとき、元画像のどの画素かを求める。
    const auto displayPixel = heiran::domain::pixelAtImagePosition(QPointF(0.2, 0.7), displaySize);
    QVERIFY(displayPixel.has_value());

    const QPoint sourcePixel =
        heiran::domain::mapTransformedToSource(*displayPixel, sourceSize, orientation);
    // 往復させると同じ表示画素へ戻る。
    QCOMPARE(heiran::domain::mapSourceToTransformed(sourcePixel, sourceSize, orientation),
             *displayPixel);
    QVERIFY(sourcePixel.x() >= 0 && sourcePixel.x() < sourceSize.width());
    QVERIFY(sourcePixel.y() >= 0 && sourcePixel.y() < sourceSize.height());
  }

  void coversEveryDisplayPixelForEveryOrientation() {
    const QSize sourceSize(5, 3);

    for (int quarterTurns = 0; quarterTurns < 4; ++quarterTurns) {
      for (const bool flipX : {false, true}) {
        for (const bool flipY : {false, true}) {
          const ImageOrientation orientation{
              .rotationQuarterTurns = quarterTurns, .flipX = flipX, .flipY = flipY};
          const QSize displaySize = heiran::domain::transformedImageSize(sourceSize, quarterTurns);

          QSet<QPair<int, int>> visitedSourcePixels;
          for (int y = 0; y < displaySize.height(); ++y) {
            for (int x = 0; x < displaySize.width(); ++x) {
              const auto displayPixel =
                  heiran::domain::pixelAtImagePosition(QPointF(x + 0.5, y + 0.5), displaySize);
              QVERIFY(displayPixel.has_value());
              const QPoint sourcePixel =
                  heiran::domain::mapTransformedToSource(*displayPixel, sourceSize, orientation);
              visitedSourcePixels.insert({sourcePixel.x(), sourcePixel.y()});
            }
          }

          // 表示画素と元画素が 1 対 1 に対応する。
          QCOMPARE(visitedSourcePixels.size(), sourceSize.width() * sourceSize.height());
        }
      }
    }
  }
};

QTEST_MAIN(ImageTransformTests)

#include "ImageTransformTests.moc"
