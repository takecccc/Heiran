// ヒストグラムの横軸の範囲と、画面位置との対応 (HistogramAxis)。

#include "heiran/ui/HistogramAxis.hpp"

#include <QtTest>

using namespace heiran::ui;

class HistogramAxisTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void startsAsTheFullRange() {
    const HistogramRange range;
    QVERIFY(range.isFull());
    QCOMPARE(range.span(), 1.0);
  }

  // 範囲は [0, 1] に収まり、最小の幅を下回らない。
  void clampsTheRange() {
    const auto outside = clampHistogramRange({-0.5, 2.0});
    QCOMPARE(outside.low, 0.0);
    QCOMPARE(outside.high, 1.0);

    const auto collapsed = clampHistogramRange({0.5, 0.5});
    QVERIFY(collapsed.span() >= kMinHistogramSpan - 1e-12);

    // 幅を保ったまま、はみ出した分だけずらす。
    const auto shifted = clampHistogramRange({0.9, 1.3});
    QVERIFY(qAbs(shifted.high - 1.0) < 1e-12);
    QVERIFY(qAbs(shifted.span() - 0.4) < 1e-12);
  }

  // 拡大しても、位置の下にあるものが同じ画面位置に残ること。
  void keepsThePointUnderTheCursorWhenZooming() {
    const HistogramRange range{0.2, 0.8};
    const double anchor = 0.35;
    constexpr double kLeft = 100.0;
    constexpr double kWidth = 600.0;
    const double before = histogramXForFraction(anchor, kLeft, kWidth, range);

    const auto zoomed = zoomHistogramRange(range, anchor, 0.5);
    QVERIFY(qAbs(zoomed.span() - 0.3) < 1e-12);
    const double after = histogramXForFraction(anchor, kLeft, kWidth, zoomed);
    QVERIFY(qAbs(before - after) < 1e-9);
  }

  void zoomsOutUpToTheFullRange() {
    HistogramRange range{0.4, 0.5};
    range = zoomHistogramRange(range, 0.45, 100.0);
    QVERIFY(range.isFull());
  }

  void doesNotZoomBelowTheMinimumSpan() {
    HistogramRange range{0.0, 1.0};
    for (int step = 0; step < 40; ++step) {
      range = zoomHistogramRange(range, 0.5, 0.5);
    }
    QVERIFY(qAbs(range.span() - kMinHistogramSpan) < 1e-12);
  }

  // x と軸上の位置は、行き来で元に戻る。
  void mapsBetweenXAndFraction() {
    const HistogramRange range{0.25, 0.75};
    constexpr double kLeft = 44.0;
    constexpr double kWidth = 800.0;
    QCOMPARE(histogramFractionForX(kLeft, kLeft, kWidth, range), 0.25);
    QCOMPARE(histogramFractionForX(kLeft + kWidth, kLeft, kWidth, range), 0.75);
    QVERIFY(qAbs(histogramFractionForX(kLeft + kWidth / 2, kLeft, kWidth, range) - 0.5) < 1e-12);

    const double x = histogramXForFraction(0.6, kLeft, kWidth, range);
    QVERIFY(qAbs(histogramFractionForX(x, kLeft, kWidth, range) - 0.6) < 1e-12);
  }

  // ビンと位置の対応。256 個のビンは 0.0 - 1.0 に並び、両端が 0 と 255。
  void mapsBetweenBinsAndFractions() {
    QCOMPARE(histogramBinForFraction(0.0, 256), 0);
    QCOMPARE(histogramBinForFraction(1.0, 256), 255);
    QCOMPARE(histogramBinForFraction(0.5, 256), 128);
    // 範囲外は端へ丸める。
    QCOMPARE(histogramBinForFraction(-1.0, 256), 0);
    QCOMPARE(histogramBinForFraction(2.0, 256), 255);

    QCOMPARE(histogramFractionForBin(0, 256), 0.0);
    QCOMPARE(histogramFractionForBin(255, 256), 1.0);
    for (const int bin : {0, 1, 100, 254, 255}) {
      QCOMPARE(histogramBinForFraction(histogramFractionForBin(bin, 256), 256), bin);
    }
  }

  // 縦軸の自動の上限は、表示範囲の中の最大。範囲の外の山は数えない。
  void findsTheMaximumInsideTheRange() {
    QVector<int> bins(256, 1);
    bins[10] = 500;  // 範囲の外。
    bins[128] = 40;  // 範囲の中。
    bins[200] = 90;  // 範囲の中。
    const HistogramRange range{histogramFractionForBin(100, 256),
                               histogramFractionForBin(220, 256)};

    QCOMPARE(histogramMaxCountInRange(bins, range), 90);
    QCOMPARE(histogramMaxCountInRange(bins, HistogramRange{}), 500);
  }

  // 範囲の端のビンも数える。
  void includesTheBinsAtTheEdgesOfTheRange() {
    QVector<int> bins(256, 0);
    bins[100] = 7;
    bins[220] = 9;
    const HistogramRange range{histogramFractionForBin(100, 256),
                               histogramFractionForBin(220, 256)};
    QCOMPARE(histogramMaxCountInRange(bins, range), 9);
    bins[220] = 0;
    QCOMPARE(histogramMaxCountInRange(bins, range), 7);
  }

  void emptyBinsHaveNoMaximum() {
    QCOMPARE(histogramMaxCountInRange(QVector<int>(), HistogramRange{}), 0);
  }

  // 固定の上限があればそれ、無ければ自動。
  void prefersAFixedFullScale() {
    QCOMPARE(histogramFullScaleRatio(0.3, 0.0), 0.3);
    QCOMPARE(histogramFullScaleRatio(0.3, 0.05), 0.05);
  }
};

QTEST_MAIN(HistogramAxisTests)

#include "HistogramAxisTests.moc"
