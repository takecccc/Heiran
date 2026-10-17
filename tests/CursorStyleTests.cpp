#include "heiran/ui/CursorStyle.hpp"

#include <QtTest>

using heiran::ui::chooseContrastColor;
using heiran::ui::colorDifference;

namespace {

QVector<QRgb> solid(QRgb color, int count = 20) {
  return QVector<QRgb>(count, color);
}

int saturationOf(const QColor &color) {
  return color.toHsv().saturation();
}

}  // namespace

class CursorStyleTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void roundTripsThroughStrings() {
    for (const auto style : {heiran::ui::CursorStyle::Outline, heiran::ui::CursorStyle::Contrast}) {
      QCOMPARE(heiran::ui::cursorStyleFromString(heiran::ui::cursorStyleToString(style)), style);
    }
    QCOMPARE(heiran::ui::cursorStyleFromString("unknown"), heiran::ui::CursorStyle::Outline);
  }

  // 単純な反転では、中間のグレーは、ほぼ同じ色になる。彩度のある色を選んで、離れて見せる。
  void picksAChromaticColorOnMidGray() {
    for (const int level : {96, 118, 128, 140, 160}) {
      const QRgb gray = qRgb(level, level, level);
      const QColor chosen = chooseContrastColor(solid(gray));
      QVERIFY2(saturationOf(chosen) > 150,
               qPrintable(QString("level %1 -> %2").arg(level).arg(chosen.name())));
      QVERIFY2(colorDifference(gray, chosen.rgb()) > 70.0,
               qPrintable(QString("level %1 -> %2 (ΔE %3)")
                              .arg(level)
                              .arg(chosen.name())
                              .arg(colorDifference(gray, chosen.rgb()))));
      // 単純な反転 (255 - 値) より離れている。
      const QRgb inverted = qRgb(255 - level, 255 - level, 255 - level);
      QVERIFY(colorDifference(gray, chosen.rgb()) > colorDifference(gray, inverted) + 20.0);
    }
  }

  // 黒には明るい色、白には暗い色や濃い色。元の色と同じ色は選ばない。
  void picksADistantColorOnBlackAndWhite() {
    const QColor onBlack = chooseContrastColor(solid(qRgb(0, 0, 0)));
    QVERIFY(colorDifference(qRgb(0, 0, 0), onBlack.rgb()) > 80.0);
    const QColor onWhite = chooseContrastColor(solid(qRgb(255, 255, 255)));
    QVERIFY(colorDifference(qRgb(255, 255, 255), onWhite.rgb()) > 80.0);
  }

  // 純色の画像では、その色の近くを避ける。
  void avoidsTheColorOfThePixels() {
    for (const QRgb color : {qRgb(255, 0, 0), qRgb(0, 255, 0), qRgb(0, 0, 255), qRgb(255, 255, 0),
                             qRgb(0, 255, 255), qRgb(255, 0, 255)}) {
      const QColor chosen = chooseContrastColor(solid(color));
      QVERIFY2(colorDifference(color, chosen.rgb()) > 60.0,
               qPrintable(QString("%1 -> %2").arg(QColor(color).name(), chosen.name())));
    }
  }

  // 暗い画素と明るい画素が混ざるとき (黒と白の半分ずつ) は、どちらからも離れた色を選ぶ。
  // 黒にも白にも近い色 (灰色) や、片方と同じ色は選ばない。
  void keepsAwayFromEveryPixelInAMixedNeighbourhood() {
    QVector<QRgb> samples = solid(qRgb(0, 0, 0), 10);
    samples += solid(qRgb(255, 255, 255), 10);
    const QColor chosen = chooseContrastColor(samples);
    QVERIFY2(colorDifference(qRgb(0, 0, 0), chosen.rgb()) > 40.0 &&
                 colorDifference(qRgb(255, 255, 255), chosen.rgb()) > 40.0,
             qPrintable(chosen.name()));
    QVERIFY(saturationOf(chosen) > 150);
  }

  // 空なら、既定の色。透明度は無視する。
  void handlesEmptyInputAndIgnoresAlpha() {
    QVERIFY(chooseContrastColor({}).isValid());
    QCOMPARE(chooseContrastColor(solid(qRgba(128, 128, 128, 255))),
             chooseContrastColor(solid(qRgba(128, 128, 128, 20))));
  }

  void measuresColorDifferenceSensibly() {
    QCOMPARE(colorDifference(qRgb(10, 20, 30), qRgb(10, 20, 30)), 0.0);
    QVERIFY(colorDifference(qRgb(0, 0, 0), qRgb(255, 255, 255)) > 99.0);
    QVERIFY(colorDifference(qRgb(128, 128, 128), qRgb(130, 128, 128)) < 3.0);
  }
};

QTEST_MAIN(CursorStyleTests)

#include "CursorStyleTests.moc"
