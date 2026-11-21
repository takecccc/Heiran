#include "heiran/ui/CellOverlayPainter.hpp"

#include <QImage>
#include <QPainter>
#include <QtTest>

namespace {

QImage renderOverlays(const heiran::ui::CellOverlayInput &input, QSize size = QSize(200, 160)) {
  QImage canvas(size, QImage::Format_RGB32);
  canvas.fill(Qt::black);
  QPainter painter(&canvas);
  heiran::ui::paintCellOverlays(&painter, input);
  painter.end();
  return canvas;
}

// 黒以外の画素を数える。何かが描かれたかを見るために使う。
int paintedPixelCount(const QImage &image) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y) != QColor(Qt::black)) {
        ++count;
      }
    }
  }
  return count;
}

// 指定した色に近い画素があるか。
bool containsColorNear(const QImage &image, const QColor &expected, int tolerance = 24) {
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor color = image.pixelColor(x, y);
      if (std::abs(color.red() - expected.red()) <= tolerance &&
          std::abs(color.green() - expected.green()) <= tolerance &&
          std::abs(color.blue() - expected.blue()) <= tolerance) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

class CellOverlayPainterTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 何も指定しなければ何も描かないこと。
  void drawsNothingForAnEmptyInput() {
    const QImage rendered = renderOverlays({});
    QCOMPARE(paintedPixelCount(rendered), 0);
  }

  void projectsTheHistogramMaskWithoutPaintingUnmatchedPixels() {
    QImage mask(2, 1, QImage::Format_MonoLSB);
    mask.setColorTable({qRgba(0, 0, 0, 0), qRgba(32, 200, 240, 128)});
    mask.fill(0);
    mask.setPixel(0, 0, 1);
    mask.setOffset(QPoint(5, 3));
    const auto output =
        renderOverlays({.scale = 3.0, .offsetX = 2.0, .offsetY = 1.0, .histogramHighlight = &mask});
    QCOMPARE(paintedPixelCount(output), 9);
    QCOMPARE(output.pixelColor(9, 6), QColor(16, 100, 120));
    QCOMPARE(output.pixelColor(12, 6), QColor(Qt::black));
  }

  // 倍率が 0 以下なら描かないこと。座標の写しが成り立たない。
  void drawsNothingForANonPositiveScale() {
    heiran::ui::CellOverlayInput input;
    input.scale = 0.0;
    input.sharedCursorPosition = QPointF(10.0, 10.0);
    QCOMPARE(paintedPixelCount(renderOverlays(input)), 0);
  }

  // 選択範囲を画像座標から表示座標へ写して描くこと。
  void drawsTheSelectionRect() {
    heiran::ui::CellOverlayInput input;
    input.scale = 2.0;
    input.canvasSize = QSize(200, 160);
    input.selectionRect = QRectF(10.0, 10.0, 20.0, 20.0);

    const QImage rendered = renderOverlays(input);
    QVERIFY(containsColorNear(rendered, QColor(255, 210, 64)));

    // 倍率 2 なので、画像座標 (10,10) は表示座標 (20,20) に来る。
    // 枠線の上を通る位置に色があること。
    bool foundOnEdge = false;
    for (int x = 18; x <= 22 && !foundOnEdge; ++x) {
      for (int y = 18; y <= 22 && !foundOnEdge; ++y) {
        const QColor color = rendered.pixelColor(x, y);
        foundOnEdge = color.red() > 100 && color.green() > 80 && color.blue() < 120;
      }
    }
    QVERIFY2(foundOnEdge, "the selection edge was not drawn at the projected position");
  }

  // ドラッグ中の仮の選択範囲は色を分けること。確定したものと区別が付く。
  void drawsThePreviewSelectionInADifferentColor() {
    heiran::ui::CellOverlayInput input;
    input.scale = 2.0;
    input.canvasSize = QSize(200, 160);
    input.previewSelectionRect = QRectF(10.0, 10.0, 20.0, 20.0);

    const QImage rendered = renderOverlays(input);
    QVERIFY(containsColorNear(rendered, QColor(120, 200, 255)));
    QVERIFY(!containsColorNear(rendered, QColor(255, 210, 64), 8));
  }

  // 共有カーソルは中心を空けた十字で描くこと。真ん中まで線を引くと、
  // 指している画素そのものが隠れて色を確かめられない。
  void leavesAGapAtTheCursorCentre() {
    heiran::ui::CellOverlayInput input;
    input.scale = 1.0;
    input.canvasSize = QSize(200, 160);
    input.sharedCursorPosition = QPointF(100.0, 80.0);

    const QImage rendered = renderOverlays(input);
    QVERIFY(containsColorNear(rendered, QColor(255, 235, 120)));

    // 等倍では画素 (100,80) の中心を指す。中心そのものは塗られない。
    QCOMPARE(rendered.pixelColor(100, 80), QColor(Qt::black));

    // 腕は中心から離れた場所にある。
    QVERIFY2(rendered.pixelColor(94, 80) != QColor(Qt::black),
             "the left arm of the crosshair is missing");
    QVERIFY2(rendered.pixelColor(100, 74) != QColor(Qt::black),
             "the upper arm of the crosshair is missing");
  }

  // 拡大すると十字も画素の大きさに合わせて広がること。ただし際限なくは広げない。
  void scalesTheCrosshairWithTheZoom() {
    const auto armSpanAt = [](double scale) {
      heiran::ui::CellOverlayInput input;
      input.scale = scale;
      input.canvasSize = QSize(400, 400);
      input.sharedCursorPosition = QPointF(4.5, 4.5);
      const QImage rendered = renderOverlays(input, QSize(400, 400));

      int leftmost = rendered.width();
      int rightmost = -1;
      for (int x = 0; x < rendered.width(); ++x) {
        for (int y = 0; y < rendered.height(); ++y) {
          if (rendered.pixelColor(x, y) != QColor(Qt::black)) {
            leftmost = std::min(leftmost, x);
            rightmost = std::max(rightmost, x);
          }
        }
      }
      return rightmost - leftmost;
    };

    const int spanAtOne = armSpanAt(1.0);
    const int spanAtTen = armSpanAt(10.0);
    QVERIFY2(spanAtTen > spanAtOne,
             qPrintable(QString("scale=1 span=%1 scale=10 span=%2").arg(spanAtOne).arg(spanAtTen)));

    // 腕の長さは 14 で頭打ちにしてある。左右で 28 に余白を足した程度に収まる。
    const int spanAtHundred = armSpanAt(100.0);
    QVERIFY2(spanAtHundred <= 32, qPrintable(QString("scale=100 span=%1").arg(spanAtHundred)));
  }

  // 拡大した選択範囲が枠の外へはみ出さないこと。
  void clipsTheSelectionToTheCanvas() {
    heiran::ui::CellOverlayInput input;
    input.scale = 4.0;
    input.canvasSize = QSize(60, 60);
    // 表示座標では (40,40) から 400x400 になり、枠を大きくはみ出す。
    input.selectionRect = QRectF(10.0, 10.0, 100.0, 100.0);

    const QImage rendered = renderOverlays(input, QSize(200, 160));
    // 枠の中には線がある。
    QVERIFY(paintedPixelCount(rendered.copy(QRect(0, 0, 60, 60))) > 0);
    // 枠の外には出ない。
    QCOMPARE(paintedPixelCount(rendered.copy(QRect(60, 0, 140, 160))), 0);
    QCOMPARE(paintedPixelCount(rendered.copy(QRect(0, 60, 200, 100))), 0);
  }

  // ラベルは矩形を渡したときだけ描くこと。
  void drawsTheLabelOnlyWhenARectIsGiven() {
    heiran::ui::CellOverlayInput input;
    input.scale = 1.0;
    input.canvasSize = QSize(200, 160);
    input.labelText = "cell";

    QCOMPARE(paintedPixelCount(renderOverlays(input)), 0);

    input.labelRect = QRect(8, 8, 120, 24);
    const QImage rendered = renderOverlays(input);
    QVERIFY(paintedPixelCount(rendered) > 0);
    // 下地と文字の両方が出ること。
    QVERIFY(containsColorNear(rendered, QColor(18, 20, 24), 20));
    QVERIFY(containsColorNear(rendered, QColor(245, 247, 250), 20));
  }

  // カーソルと選択範囲は、明るい画像の上でも見えること。色の線の下に、暗い縁を敷く。
  void outlinesTheCursorAndSelectionSoTheyShowOnBrightImages() {
    const auto darkPixelsOnWhite = [](const heiran::ui::CellOverlayInput &input) {
      QImage canvas(200, 160, QImage::Format_RGB32);
      canvas.fill(Qt::white);
      QPainter painter(&canvas);
      heiran::ui::paintCellOverlays(&painter, input);
      painter.end();
      int dark = 0;
      for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
          if (qGray(canvas.pixel(x, y)) < 80) {
            ++dark;
          }
        }
      }
      return dark;
    };

    heiran::ui::CellOverlayInput cursor;
    cursor.scale = 1.0;
    cursor.canvasSize = QSize(200, 160);
    cursor.sharedCursorPosition = QPointF(100.0, 80.0);
    QVERIFY2(darkPixelsOnWhite(cursor) > 20, "the cursor has no dark outline on a bright image");

    heiran::ui::CellOverlayInput selection;
    selection.scale = 1.0;
    selection.canvasSize = QSize(200, 160);
    selection.selectionRect = QRectF(20.0, 20.0, 60.0, 40.0);
    QVERIFY2(darkPixelsOnWhite(selection) > 100,
             "the selection has no dark outline on a bright image");

    // 色の線は、縁の上に残る。
    QImage canvas(200, 160, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    heiran::ui::paintCellOverlays(&painter, cursor);
    painter.end();
    QVERIFY(containsColorNear(canvas, QColor(255, 235, 120)));
  }

  // 「離れた色」のカーソルは、周りの画素の色から離れた色で描く。中間のグレーの上で、
  // 単純な反転のように溶け込まないこと。縁は付けない。
  void drawsTheContrastCursorAwayFromThePixelColor() {
    QImage canvas(200, 160, QImage::Format_RGB32);
    canvas.fill(qRgb(128, 128, 128));
    const QImage samples = canvas;
    QPainter painter(&canvas);
    heiran::ui::CellOverlayInput input;
    input.scale = 1.0;
    input.canvasSize = QSize(200, 160);
    input.sharedCursorPosition = QPointF(100.0, 80.0);
    input.cursorStyle = heiran::ui::CursorStyle::Contrast;
    input.sampleImage = &samples;
    heiran::ui::paintCellOverlays(&painter, input);
    painter.end();

    int farPixels = 0;
    int nearPixels = 0;
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        const QRgb pixel = canvas.pixel(x, y);
        if (pixel == qRgb(128, 128, 128)) {
          continue;
        }
        (heiran::ui::colorDifference(pixel, qRgb(128, 128, 128)) > 70.0 ? farPixels : nearPixels)++;
      }
    }
    QVERIFY2(farPixels >= 12, "the contrast cursor was not drawn");
    // 縁 (暗い半透明の線) は敷かない。
    QCOMPARE(nearPixels, 0);
  }

  // 画像が無ければ、固定の色になる。
  void fallsBackToTheFixedColorWithoutASampleImage() {
    heiran::ui::CellOverlayInput input;
    input.scale = 1.0;
    input.canvasSize = QSize(200, 160);
    input.sharedCursorPosition = QPointF(100.0, 80.0);
    input.cursorStyle = heiran::ui::CursorStyle::Contrast;
    QVERIFY(containsColorNear(renderOverlays(input), QColor(255, 235, 120)));
  }

  // 描いた後に painter の状態が戻っていること。呼び出し側が続けて描くため。
  void restoresThePainterState() {
    QImage canvas(200, 160, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    painter.setPen(QPen(QColor(10, 200, 30), 3));
    const QPen before = painter.pen();
    const QTransform transformBefore = painter.transform();

    heiran::ui::CellOverlayInput input;
    input.scale = 2.0;
    input.selectionRect = QRectF(10.0, 10.0, 20.0, 20.0);
    input.sharedCursorPosition = QPointF(30.0, 30.0);
    input.canvasSize = canvas.size();
    input.labelText = "cell";
    input.labelRect = QRect(8, 8, 120, 24);
    heiran::ui::paintCellOverlays(&painter, input);

    QCOMPARE(painter.pen().color(), before.color());
    QCOMPARE(painter.pen().widthF(), before.widthF());
    QCOMPARE(painter.transform(), transformBefore);
  }
};

QTEST_MAIN(CellOverlayPainterTests)

#include "CellOverlayPainterTests.moc"
