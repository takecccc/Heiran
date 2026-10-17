#include "heiran/ui/DiffWindow.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QtTest>

namespace {

// 差分の入力。解析用バッファを持たない 8bit 画像として組み立てる。
// 表示用画像がそのまま解析入力になるため、期待値を手で計算できる。
heiran::ui::AnalysisImage makeAnalysisImage(const QString &label, QSize size, int gray,
                                            int shiftedGray, const QRect &shiftedRect) {
  QImage image(size, QImage::Format_RGBA8888);
  image.fill(QColor(gray, gray, gray));
  for (int y = shiftedRect.top(); y <= shiftedRect.bottom(); ++y) {
    for (int x = shiftedRect.left(); x <= shiftedRect.right(); ++x) {
      image.setPixelColor(x, y, QColor(shiftedGray, shiftedGray, shiftedGray));
    }
  }

  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = size;

  return {.label = label, .image = image, .selectionRect = std::nullopt, .sampler = sampler};
}

}  // namespace

class DiffWindowTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 差分の統計が入力どおりに出ること。最適化で結果が変わらないことを守る。
  void reportsStatisticsForAKnownDifference() {
    const QSize size(64, 48);
    // 8x4 の領域だけ 100 から 140 へ変える。輝度差は 40。
    const QRect shifted(0, 0, 8, 4);
    const QVector<heiran::ui::AnalysisImage> images = {
        makeAnalysisImage("base", size, 100, 100, QRect()),
        makeAnalysisImage("target", size, 100, 140, shifted)};

    heiran::ui::DiffWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setAnalysisImages(1, images);

    const auto labels = window.findChildren<QLabel *>();
    QString statsText;
    for (const auto *label : labels) {
      if (label->text().contains("max absolute diff")) {
        statsText = label->text();
      }
    }
    QVERIFY2(!statsText.isEmpty(), "the statistics label was not found");

    // 差のある画素は 32 個、全体は 3072 個。平均は 40 * 32 / 3072 = 0.4167。
    QVERIFY2(statsText.contains("max absolute diff=40.00"), qPrintable(statsText));
    QVERIFY2(statsText.contains("mean absolute diff=0.42"), qPrintable(statsText));
    QVERIFY2(statsText.contains("different pixel count=32 / 3072"), qPrintable(statsText));
  }

  // まとめて作った平面が、画素ごとの取得と一致すること。
  // 高速化はこの読み替えが要になるため、ずれていないことを守る。
  void buildsPlanesThatMatchPerPixelSampling() {
    const QSize size(23, 17);
    QImage image(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
      for (int x = 0; x < size.width(); ++x) {
        image.setPixelColor(x, y, QColor((x * 11) % 256, (y * 7) % 256, (x + y) % 256));
      }
    }

    heiran::ui::AnalysisSampler sampler;
    sampler.decodedImage = {.displayImage = image};
    sampler.sourceSize = size;

    const QRect region(0, 0, size.width(), size.height());
    const QVector<float> luma = sampler.buildLumaPlane(region);
    QCOMPARE(luma.size(), static_cast<qsizetype>(size.width()) * size.height());

    for (int channel = 0; channel < 3; ++channel) {
      const QVector<float> plane = sampler.buildChannelPlane(region, channel);
      QCOMPARE(plane.size(), luma.size());
      for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
          const float expected = sampler.sampleRgbaAtDisplayPixel(x, y, channel);
          const float actual = plane.at(static_cast<qsizetype>(y) * size.width() + x);
          QVERIFY2(std::abs(expected - actual) < 0.0001F,
                   qPrintable(QString("channel %1 at (%2, %3): %4 vs %5")
                                  .arg(channel)
                                  .arg(x)
                                  .arg(y)
                                  .arg(expected)
                                  .arg(actual)));
        }
      }
    }

    for (int y = 0; y < size.height(); ++y) {
      for (int x = 0; x < size.width(); ++x) {
        const double expected = sampler.lumaAtDisplayPixel(x, y);
        const float actual = luma.at(static_cast<qsizetype>(y) * size.width() + x);
        QVERIFY2(
            std::abs(expected - actual) < 0.01,
            qPrintable(
                QString("luma at (%1, %2): %3 vs %4").arg(x).arg(y).arg(expected).arg(actual)));
      }
    }
  }

  // 大きな画像での所要時間。最適化の前後を比べるために出す。
  void reportsElapsedTimeForALargeDifference() {
    const QSize size(2048, 2048);
    const QVector<heiran::ui::AnalysisImage> images = {
        makeAnalysisImage("base", size, 100, 100, QRect()),
        makeAnalysisImage("target", size, 100, 140, QRect(0, 0, 1024, 1024))};

    heiran::ui::DiffWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QElapsedTimer timer;
    timer.start();
    window.setAnalysisImages(1, images);
    const qint64 elapsedNs = timer.nsecsElapsed();
    qInfo() << "diff 2048x2048:" << (static_cast<double>(elapsedNs) / 1'000'000.0) << "ms";
  }
};

QTEST_MAIN(DiffWindowTests)

#include "DiffWindowTests.moc"
