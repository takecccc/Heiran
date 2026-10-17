// RGB の 3 次元ヒストグラム (Histogram3D)。

#include "heiran/ui/Histogram3D.hpp"

#include <QImage>
#include <QtTest>
#include <array>
#include <cmath>

using namespace heiran::ui;

namespace {

AnalysisImage makeAnalysisImage(const QImage &image,
                                std::optional<QRect> selectionRect = std::nullopt) {
  AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();
  return {.label = "image", .image = image, .selectionRect = selectionRect, .sampler = sampler};
}

}  // namespace

class Histogram3DTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void clampsTheBinCount() {
    QCOMPARE(clampHistogram3DBins(0), kMinHistogram3DBins);
    QCOMPARE(clampHistogram3DBins(10000), kMaxHistogram3DBins);
    QCOMPARE(clampHistogram3DBins(16), 16);
  }

  // 値とビンの対応。上限ちょうどは最後のビンへ入れる (はみ出さない)。
  void mapsValuesToBins() {
    QCOMPARE(histogram3DBinForValue(0.0, 1.0, 4), 0);
    QCOMPARE(histogram3DBinForValue(0.24, 1.0, 4), 0);
    QCOMPARE(histogram3DBinForValue(0.25, 1.0, 4), 1);
    QCOMPARE(histogram3DBinForValue(0.99, 1.0, 4), 3);
    QCOMPARE(histogram3DBinForValue(1.0, 1.0, 4), 3);
    // 範囲外は端へ。
    QCOMPARE(histogram3DBinForValue(-0.5, 1.0, 4), 0);
    QCOMPARE(histogram3DBinForValue(7.0, 1.0, 4), 3);
    // 上限が 1.0 を超える軸 (float 画像)。
    QCOMPARE(histogram3DBinForValue(2.0, 4.0, 4), 2);
  }

  void roundTripsTheIndex() {
    constexpr int kBins = 7;
    const std::array<std::array<int, 3>, 6> samples = {
        {{0, 0, 0}, {6, 0, 0}, {0, 6, 0}, {0, 0, 6}, {3, 4, 5}, {6, 6, 6}}};
    for (const auto &rgb : samples) {
      const int index = histogram3DIndex(rgb[0], rgb[1], rgb[2], kBins);
      QVERIFY(index >= 0 && index < kBins * kBins * kBins);
      int r = -1;
      int g = -1;
      int b = -1;
      histogram3DSplit(index, kBins, &r, &g, &b);
      QCOMPARE(r, rgb[0]);
      QCOMPARE(g, rgb[1]);
      QCOMPARE(b, rgb[2]);
    }
  }

  void describesTheRangeOfABin() {
    QCOMPARE(histogram3DBinLow(1, 1.0, 4), 0.25);
    QCOMPARE(histogram3DBinHigh(1, 1.0, 4), 0.5);
    QCOMPARE(histogram3DBinHigh(3, 2.0, 4), 2.0);
  }

  // ビンの色は中心の値。上限が 1.0 を超えても、表示できる範囲へ収める。
  void coloursABinByItsCenter() {
    QCOMPARE(histogram3DBinColor(0, 0, 0, 1.0, 2), QColor(64, 64, 64));
    QCOMPARE(histogram3DBinColor(1, 1, 1, 1.0, 2), QColor(191, 191, 191));
    QCOMPARE(histogram3DBinColor(3, 0, 0, 8.0, 4).red(), 255);
  }

  // 既知の画像の集計。
  void countsPixelsIntoBins() {
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(0, 0, 0));
    image.setPixelColor(1, 0, QColor(255, 0, 0));
    image.setPixelColor(0, 1, QColor(255, 255, 255));
    image.setPixelColor(1, 1, QColor(255, 255, 255));

    const auto histogram = buildHistogram3D(makeAnalysisImage(image), 4, 1.0);

    QCOMPARE(histogram.binsPerAxis, 4);
    QCOMPARE(histogram.pixelCount, qint64{4});
    QCOMPARE(histogram.counts.size(), 64);
    QCOMPARE(histogram.counts.at(histogram3DIndex(0, 0, 0, 4)), 1);
    QCOMPARE(histogram.counts.at(histogram3DIndex(3, 0, 0, 4)), 1);
    QCOMPARE(histogram.counts.at(histogram3DIndex(3, 3, 3, 4)), 2);
    QCOMPARE(histogram.maxCount, 2);
    QCOMPARE(histogram.occupiedBinCount, 3);
    QVERIFY(!histogram.selectionApplied);

    qint64 total = 0;
    for (const int count : histogram.counts) {
      total += count;
    }
    QCOMPARE(total, histogram.pixelCount);
  }

  // 選択範囲があれば、その中だけを数える。
  void countsOnlyTheSelection() {
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(255, 0, 0));
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        image.setPixelColor(x, y, QColor(0, 0, 255));
      }
    }

    const auto histogram = buildHistogram3D(makeAnalysisImage(image, QRect(0, 0, 2, 2)), 4, 1.0);

    QVERIFY(histogram.selectionApplied);
    QCOMPARE(histogram.pixelCount, qint64{4});
    QCOMPARE(histogram.counts.at(histogram3DIndex(0, 0, 3, 4)), 4);
    QCOMPARE(histogram.occupiedBinCount, 1);
  }

  // 帯の高さを超える大きさでも、全画素が数えられること。
  void countsEveryPixelOfATallImage() {
    QImage image(10, 300, QImage::Format_RGBA8888);
    image.fill(QColor(0, 255, 0));

    const auto histogram = buildHistogram3D(makeAnalysisImage(image), 8, 1.0);

    QCOMPARE(histogram.pixelCount, qint64{3000});
    QCOMPARE(histogram.counts.at(histogram3DIndex(0, 7, 0, 8)), 3000);
  }

  // 指定した数は範囲へ収められる。
  void clampsTheRequestedBins() {
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QCOMPARE(buildHistogram3D(makeAnalysisImage(image), 1, 1.0).binsPerAxis, kMinHistogram3DBins);
    QCOMPARE(buildHistogram3D(makeAnalysisImage(image), 999, 1.0).binsPerAxis, kMaxHistogram3DBins);
  }

  // 向きが 0 なら、赤が右、緑が上 (画面の y は下が正なので負)、青が手前へ向かう。
  void projectsWithoutRotation() {
    const auto center = projectHistogram3DPoint(0.5, 0.5, 0.5, 0.0, 0.0);
    QVERIFY(qAbs(center.x) < 1e-12 && qAbs(center.y) < 1e-12 && qAbs(center.depth) < 1e-12);

    const auto red = projectHistogram3DPoint(1.0, 0.5, 0.5, 0.0, 0.0);
    QVERIFY(qAbs(red.x - 0.5) < 1e-12 && qAbs(red.y) < 1e-12);

    const auto green = projectHistogram3DPoint(0.5, 1.0, 0.5, 0.0, 0.0);
    QVERIFY(qAbs(green.y + 0.5) < 1e-12);

    const auto blue = projectHistogram3DPoint(0.5, 0.5, 1.0, 0.0, 0.0);
    QVERIFY(qAbs(blue.depth - 0.5) < 1e-12);
  }

  // 水平に 90 度回すと、青が右へ来る。回しても原点からの距離は変わらない。
  void rotatesAroundTheVerticalAxis() {
    const auto blue = projectHistogram3DPoint(0.5, 0.5, 1.0, 90.0, 0.0);
    QVERIFY(qAbs(blue.x - 0.5) < 1e-9);
    QVERIFY(qAbs(blue.depth) < 1e-9);

    const auto corner = projectHistogram3DPoint(1.0, 1.0, 1.0, 37.0, 21.0);
    const double length =
        std::sqrt(corner.x * corner.x + corner.y * corner.y + corner.depth * corner.depth);
    QVERIFY(qAbs(length - std::sqrt(0.75)) < 1e-9);
  }
};

QTEST_MAIN(Histogram3DTests)

#include "Histogram3DTests.moc"
