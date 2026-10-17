#include "heiran/ui/ScopeBuilder.hpp"
#include "heiran/ui/ScopeWindows.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QtTest>

using namespace heiran::ui;

namespace {

// 解析用バッファを持たない 8bit 画像 (表示用画像が、そのまま解析の入力になる)。
AnalysisImage makeAnalysisImage(const QString &label, const QImage &image,
                                std::optional<QRect> selection = std::nullopt) {
  AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();
  return {.label = label, .image = image, .selectionRect = selection, .sampler = sampler};
}

QImage splitImage(QSize size) {
  QImage image(size, QImage::Format_RGBA8888);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, x < size.width() / 2 ? QColor(255, 255, 255) : QColor(0, 0, 0));
    }
  }
  return image;
}

}  // namespace

class ScopeWindowsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 解析の入力から、集計ができる。左が白、右が黒の画像では、波形の左は、上、右は、下。
  void buildsTheWaveformOfAnImage() {
    const auto analysisImage = makeAnalysisImage("A", splitImage({64, 32}));
    const auto counts = buildWaveform(analysisImage);
    QVERIFY(counts != nullptr);
    QCOMPARE(counts->columns, 64);
    QCOMPARE(counts->levels, kWaveformLevels);
    QCOMPARE(counts->sourceWidth, 64);
    QCOMPARE(counts->luma[static_cast<size_t>(255) * 64 + 0], 32U);
    QCOMPARE(counts->luma[0 * 64 + 63], 32U);
    // 列の数は、画像の幅を超えない (広い画像でも、上限)。
    const auto wide = buildWaveform(makeAnalysisImage("W", splitImage({3000, 4})));
    QCOMPARE(wide->columns, kWaveformMaxColumns);
  }

  // 選択範囲があれば、その範囲だけを数える。
  void countsOnlyTheSelection() {
    const auto analysisImage = makeAnalysisImage("A", splitImage({64, 32}), QRect(0, 0, 16, 8));
    const auto counts = buildWaveform(analysisImage);
    QCOMPARE(counts->sourceWidth, 16);
    QCOMPARE(counts->sourceHeight, 8);
    std::uint64_t total = 0;
    for (const auto count : counts->luma) {
      total += count;
    }
    QCOMPARE(total, std::uint64_t{16 * 8});
    const auto scope = buildVectorscope(analysisImage);
    QCOMPARE(scope->total, std::int64_t{16 * 8});
    // 空の画像は、集計できない。
    QVERIFY(buildWaveform(makeAnalysisImage("E", QImage())) == nullptr);
  }

  // 波形モニタの窓: セルごとに 1 つの絵。表示の切り替え (Parade) で、絵の幅が、3 倍になる。
  void showsOneWaveformPerCell() {
    WaveformWindow window;
    const QVector<AnalysisImage> images = {makeAnalysisImage("A", splitImage({64, 32})),
                                           makeAnalysisImage("B", splitImage({32, 16}))};
    window.setAnalysisImages(3, images);
    window.showAndRaise();
    QCOMPARE(window.plotCount(), 2);
    QVERIFY(window.plotAt(0)->title().contains("A"));
    QCOMPARE(window.plotAt(0)->image().size(), QSize(64, kWaveformLevels));
    QCOMPARE(window.plotAt(1)->image().size(), QSize(32, kWaveformLevels));
    QVERIFY(window.windowTitle().contains("Waveform"));

    auto *mode = window.findChild<QComboBox *>();
    QVERIFY(mode != nullptr);
    mode->setCurrentText("Parade");
    QCOMPARE(window.plotAt(0)->image().size(), QSize(64 * 3, kWaveformLevels));
  }

  // 同じ画像を渡し直しても、数え直さない。画像が減れば、絵も減る。選択範囲が付けば、数え直す。
  void reusesTheResultsAndFollowsTheImages() {
    WaveformWindow window;
    const auto first = makeAnalysisImage("A", splitImage({64, 32}));
    const auto second = makeAnalysisImage("B", splitImage({48, 24}));
    window.setAnalysisImages(1, {first, second});
    window.showAndRaise();
    QCOMPARE(window.cacheComputeCount(), 2);
    window.setAnalysisImages(1, {first, second});
    QCOMPARE(window.cacheComputeCount(), 2);
    window.setAnalysisImages(2, {first});
    QCOMPARE(window.plotCount(), 1);
    QCOMPARE(window.cacheComputeCount(), 2);
    window.setAnalysisImages(2, {makeAnalysisImage("A", first.image, QRect(0, 0, 8, 8))});
    QCOMPARE(window.cacheComputeCount(), 3);
    QVERIFY(window.plotAt(0)->title().contains("selection"));
  }

  // 隠れている間は、数えない。開いたときに、まとめて数える。
  void doesNotComputeWhileHidden() {
    WaveformWindow window;
    window.setAnalysisImages(1, {makeAnalysisImage("A", splitImage({64, 32}))});
    QCOMPARE(window.cacheComputeCount(), 0);
    QCOMPARE(window.plotCount(), 0);
    window.showAndRaise();
    QCOMPARE(window.cacheComputeCount(), 1);
    QCOMPARE(window.plotCount(), 1);
  }

  // ベクトルスコープの窓: セルごとに 1 つの、正方形の絵。
  void showsOneVectorscopePerCell() {
    VectorscopeWindow window;
    QImage red(32, 32, QImage::Format_RGBA8888);
    red.fill(QColor(230, 40, 40));
    window.setAnalysisImages(1, {makeAnalysisImage("Red", red), makeAnalysisImage("Mixed", splitImage({32, 32}))});
    window.showAndRaise();
    QCOMPARE(window.plotCount(), 2);
    QCOMPARE(window.plotAt(0)->image().size(), QSize(kVectorscopeSize, kVectorscopeSize));
    QVERIFY(window.windowTitle().contains("Vectorscope"));
    // 赤の画像は、中心でなく、上寄りの 1 点へ集まる (中心の升目は、空)。点は、上下左右へ弱くにじむため、5 つの升目が光る。
    const QImage &image = window.plotAt(0)->image();
    QCOMPARE(image.pixel(128, 127), qRgb(0, 0, 0));
    int lit = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        lit += image.pixel(x, y) != qRgb(0, 0, 0) ? 1 : 0;
      }
    }
    QCOMPARE(lit, 5);
    // 白と黒だけの画像は、無彩色で、中心の 1 点 (どちらも、色差が 0)。
    const QImage &gray = window.plotAt(1)->image();
    QVERIFY(gray.pixel(128, 127) != qRgb(0, 0, 0));
  }
};

QTEST_MAIN(ScopeWindowsTests)
#include "ScopeWindowsTests.moc"
