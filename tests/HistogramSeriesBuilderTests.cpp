#include "heiran/ui/HistogramSeriesBuilder.hpp"
#include "heiran/ui/HistogramHighlightController.hpp"

#include <QImage>
#include <QTransform>
#include <QtTest>

namespace {

// 解析用バッファを持たない 8bit 画像として組み立てる。表示用画像がそのまま
// 解析入力になるため、統計の期待値を手で計算できる。
heiran::ui::AnalysisImage makeAnalysisImage(const QImage &image,
                                            std::optional<QRect> selectionRect = std::nullopt) {
  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();

  return {.label = "image", .image = image, .selectionRect = selectionRect, .sampler = sampler};
}

// 上半分と下半分で別の値を持つ灰色画像。
QImage makeTwoLevelImage(QSize size, int topValue, int bottomValue) {
  QImage image(size, QImage::Format_RGBA8888);
  for (int y = 0; y < size.height(); ++y) {
    const int value = y < size.height() / 2 ? topValue : bottomValue;
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, QColor(value, value, value));
    }
  }
  return image;
}

}  // namespace

class HistogramSeriesBuilderTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 統計値が入力どおりに出ること。表示は描画なので、値はここで守る。
  void computesStatisticsForAKnownImage() {
    // 半分が 51/255 = 0.2、半分が 153/255 = 0.6。
    // 平均は 0.4、標準偏差は 0.2 になる。
    const QImage image = makeTwoLevelImage(QSize(20, 10), 51, 153);
    const auto series = heiran::ui::buildHistogramSeries(makeAnalysisImage(image), 1.0);

    QCOMPARE(series.pixelCount, 200);
    QVERIFY(series.grayscale);

    const auto &statistics = series.redStatistics;
    QVERIFY2(std::abs(statistics.mean - 0.4) < 0.005, qPrintable(QString::number(statistics.mean)));
    QVERIFY2(std::abs(statistics.standardDeviation - 0.2) < 0.005,
             qPrintable(QString::number(statistics.standardDeviation)));
    QVERIFY2(std::abs(statistics.minimum - 51.0 / 255.0) < 0.005,
             qPrintable(QString::number(statistics.minimum)));
    QVERIFY2(std::abs(statistics.maximum - 153.0 / 255.0) < 0.005,
             qPrintable(QString::number(statistics.maximum)));

    // 2 つの値が同数なので、山はどちらか一方で割合は 0.5 になる。
    QCOMPARE(statistics.peakRatio, 0.5);
    const bool peakIsOneOfTheTwoLevels = std::abs(statistics.peakValue - 51.0 / 255.0) < 0.005 ||
                                         std::abs(statistics.peakValue - 153.0 / 255.0) < 0.005;
    QVERIFY2(peakIsOneOfTheTwoLevels, qPrintable(QString::number(statistics.peakValue)));
  }

  // ビンの合計が画素数と一致すること。縦軸はこの画素数で割って割合にするため、
  // ずれると割合が 1.0 を超えたり足りなくなったりする。
  void putsEveryPixelIntoABin() {
    QImage image(37, 23, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        image.setPixelColor(x, y, QColor((x * 7) % 256, (y * 11) % 256, (x + y) % 256));
      }
    }

    const auto series = heiran::ui::buildHistogramSeries(makeAnalysisImage(image), 1.0);
    QCOMPARE(series.pixelCount, 37 * 23);

    const auto total = [](const QVector<int> &bins) {
      return std::accumulate(bins.begin(), bins.end(), qint64(0));
    };
    QCOMPARE(total(series.redBins), series.pixelCount);
    QCOMPARE(total(series.greenBins), series.pixelCount);
    QCOMPARE(total(series.blueBins), series.pixelCount);
  }

  // 選択範囲があるときは、そこだけを集計すること。
  void aggregatesOnlyTheSelectedRegion() {
    // 上半分だけを選ぶ。下半分の 153 は集計に入らない。
    const QImage image = makeTwoLevelImage(QSize(20, 10), 51, 153);
    const auto series =
        heiran::ui::buildHistogramSeries(makeAnalysisImage(image, QRect(0, 0, 20, 5)), 1.0);

    QVERIFY(series.selectionApplied);
    QCOMPARE(series.pixelCount, 100);
    QVERIFY2(std::abs(series.redStatistics.maximum - 51.0 / 255.0) < 0.005,
             qPrintable(QString::number(series.redStatistics.maximum)));
    QCOMPARE(series.redStatistics.standardDeviation, 0.0);
    QCOMPARE(series.redStatistics.peakRatio, 1.0);
  }

  void highlightsTheRotatedSelectionInTheChosenValueSpace() {
    using namespace heiran::ui;
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.fill(QColor(128, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 255, 0));
    auto selected = makeAnalysisImage(image, QRect(0, 1, 1, 1));
    selected.image = image.transformed(QTransform().rotate(90));
    selected.sampler.orientation.rotationQuarterTurns = 1;
    auto outside = selected;
    outside.selectionRect = QRect();

    QVector<QImage> masks;
    HistogramHighlightController controller;
    connect(&controller, &HistogramHighlightController::masksChanged, this,
            [&](const QVector<QImage> &newMasks) { masks = newMasks; });
    const auto channel = HistogramHighlightChannel::Green;
    controller.request({selected, outside}, {1.0, 1.0}, channel, Qt::cyan);
    QTRY_COMPARE(masks.size(), 2);
    QCOMPARE(masks.first().offset(), QPoint(0, 1));
    QCOMPARE(masks.first().size(), QSize(1, 1));
    QCOMPARE(masks.first().pixelIndex(0, 0), 1);
    QVERIFY(masks.last().isNull());
    QCOMPARE(buildHistogramSeries(outside, 1.0).pixelCount, 0);

    auto linear = makeAnalysisImage(image, QRect(0, 0, 1, 1));
    linear.sampler.valueSpace = AnalysisValueSpace::Linear;
    controller.request({linear}, {0.20, 0.25}, HistogramHighlightChannel::Red, Qt::cyan);
    QTRY_COMPARE(masks.size(), 1);
    QCOMPARE(masks.first().pixelIndex(0, 0), 1);
    linear.sampler.valueSpace = AnalysisValueSpace::Source;
    controller.request({linear}, {0.20, 0.25}, HistogramHighlightChannel::Red, Qt::cyan);
    QTRY_COMPARE(masks.size(), 1);
    QCOMPARE(masks.first().pixelIndex(0, 0), 0);
  }

  // 横軸の上限を広げると、同じ画素がより小さいビンへ入ること。
  // 浮動小数画像で 1.0 超を収めるための軸合わせが効いていることを守る。
  void scalesBinsWithTheAxisMaximum() {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255));
    const auto image1 = makeAnalysisImage(image);

    const auto atOne = heiran::ui::buildHistogramSeries(image1, 1.0);
    QCOMPARE(atOne.redBins.at(255), 64);

    const auto atTwo = heiran::ui::buildHistogramSeries(image1, 2.0);
    QCOMPARE(atTwo.redBins.at(255), 0);
    QCOMPARE(atTwo.redBins.at(128), 64);
    // 山の値は軸の目盛りへ戻して報告する。
    QVERIFY2(std::abs(atTwo.redStatistics.peakValue - 1.0) < 0.01,
             qPrintable(QString::number(atTwo.redStatistics.peakValue)));
  }

  // 整数画像は軸を走査せず 1.0 を返すこと。走査は浮動小数のときだけでよい。
  void reportsUnitMaximumForIntegerImages() {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255));
    QCOMPARE(heiran::ui::histogramSampleMaximum(makeAnalysisImage(image)), 1.0);
  }
};

QTEST_MAIN(HistogramSeriesBuilderTests)

#include "HistogramSeriesBuilderTests.moc"
