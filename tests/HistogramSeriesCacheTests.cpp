#include "heiran/ui/HistogramSeriesCache.hpp"

#include <QImage>
#include <QtTest>

namespace {

heiran::ui::AnalysisImage makeAnalysisImage(const QImage &image, const QString &label = "image",
                                            std::optional<QRect> selectionRect = std::nullopt) {
  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();

  return {.label = label, .image = image, .selectionRect = selectionRect, .sampler = sampler};
}

QImage makeImage(QSize size, int value) {
  QImage image(size, QImage::Format_RGBA8888);
  image.fill(QColor(value, value, value));
  return image;
}

}  // namespace

class HistogramSeriesCacheTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 同じ画素データなら数え直さないこと。ページを行き来したときに効く。
  void reusesTheResultForTheSameImage() {
    const QImage image = makeImage(QSize(32, 32), 100);
    heiran::ui::HistogramSeriesCache cache;

    const auto first = cache.series(makeAnalysisImage(image), 1.0);
    QCOMPARE(cache.computeCount(), 1);

    const auto second = cache.series(makeAnalysisImage(image), 1.0);
    QCOMPARE(cache.computeCount(), 1);

    QCOMPARE(second.pixelCount, first.pixelCount);
    QCOMPARE(second.redBins, first.redBins);
    QCOMPARE(second.redStatistics.mean, first.redStatistics.mean);
  }

  // ラベルは集計に影響しないため鍵に入れていない。取り出すときに入れ直すこと。
  void appliesTheCurrentLabelToAReusedResult() {
    const QImage image = makeImage(QSize(32, 32), 100);
    heiran::ui::HistogramSeriesCache cache;

    cache.series(makeAnalysisImage(image, "before"), 1.0);
    const auto reused = cache.series(makeAnalysisImage(image, "after"), 1.0);

    QCOMPARE(cache.computeCount(), 1);
    QCOMPARE(reused.label, QString("after"));
  }

  // 画素データが違えば数え直すこと。
  void recomputesForADifferentImage() {
    heiran::ui::HistogramSeriesCache cache;
    cache.series(makeAnalysisImage(makeImage(QSize(32, 32), 100)), 1.0);
    cache.series(makeAnalysisImage(makeImage(QSize(32, 32), 200)), 1.0);
    QCOMPARE(cache.computeCount(), 2);
  }

  // 軸の上限が変われば分け方が変わるため、数え直すこと。
  void recomputesForADifferentAxisMaximum() {
    const QImage image = makeImage(QSize(32, 32), 100);
    heiran::ui::HistogramSeriesCache cache;

    const auto atOne = cache.series(makeAnalysisImage(image), 1.0);
    const auto atTwo = cache.series(makeAnalysisImage(image), 2.0);
    QCOMPARE(cache.computeCount(), 2);
    QVERIFY(atOne.redBins != atTwo.redBins);
  }

  // 選択範囲が変われば対象の画素が変わるため、数え直すこと。
  void recomputesForADifferentSelection() {
    const QImage image = makeImage(QSize(32, 32), 100);
    heiran::ui::HistogramSeriesCache cache;

    cache.series(makeAnalysisImage(image), 1.0);
    const auto selected = cache.series(makeAnalysisImage(image, "image", QRect(0, 0, 8, 8)), 1.0);
    QCOMPARE(cache.computeCount(), 2);
    QCOMPARE(selected.pixelCount, 64);

    // 選択を戻したときは取っておいた分が使えること。
    cache.series(makeAnalysisImage(image), 1.0);
    QCOMPARE(cache.computeCount(), 2);
  }

  // 回転や反転が変われば、走査する並びが変わるため数え直すこと。
  void recomputesForADifferentOrientation() {
    const QImage image = makeImage(QSize(32, 16), 100);
    heiran::ui::HistogramSeriesCache cache;

    auto rotated = makeAnalysisImage(image);
    rotated.sampler.orientation.rotationQuarterTurns = 1;

    cache.series(makeAnalysisImage(image), 1.0);
    cache.series(rotated, 1.0);
    QCOMPARE(cache.computeCount(), 2);
  }

  // 保持数を超えた分は古いものから捨てること。
  void evictsTheLeastRecentlyUsedEntry() {
    heiran::ui::HistogramSeriesCache cache(2);
    const QImage first = makeImage(QSize(16, 16), 10);
    const QImage second = makeImage(QSize(16, 16), 20);
    const QImage third = makeImage(QSize(16, 16), 30);

    cache.series(makeAnalysisImage(first), 1.0);
    cache.series(makeAnalysisImage(second), 1.0);
    // 1 つ目を使い直して、捨てる順を入れ替える。
    cache.series(makeAnalysisImage(first), 1.0);
    QCOMPARE(cache.computeCount(), 2);

    cache.series(makeAnalysisImage(third), 1.0);
    QCOMPARE(cache.computeCount(), 3);

    // 直近に使った 1 つ目は残っている。
    cache.series(makeAnalysisImage(first), 1.0);
    QCOMPARE(cache.computeCount(), 3);

    // 2 つ目は捨てられている。
    cache.series(makeAnalysisImage(second), 1.0);
    QCOMPARE(cache.computeCount(), 4);
  }

  void clearsEveryEntry() {
    const QImage image = makeImage(QSize(16, 16), 100);
    heiran::ui::HistogramSeriesCache cache;

    cache.sampleMaximum(makeAnalysisImage(image));
    cache.series(makeAnalysisImage(image), 1.0);
    QCOMPARE(cache.entryCount(), 2);

    cache.clear();
    QCOMPARE(cache.entryCount(), 0);
    cache.series(makeAnalysisImage(image), 1.0);
    QCOMPARE(cache.computeCount(), 3);
  }
};

QTEST_MAIN(HistogramSeriesCacheTests)

#include "HistogramSeriesCacheTests.moc"
