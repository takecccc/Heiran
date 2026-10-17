#include "heiran/ui/ChangeGraphSeriesBuilder.hpp"
#include "heiran/ui/ChangeGraphWindow.hpp"

#include <QCheckBox>
#include <QComboBox>
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

// 左から右へ明るくなる画像。列ごとの平均は列位置に比例する。
QImage makeHorizontalRamp(QSize size) {
  QImage image(size, QImage::Format_RGBA8888);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      const int value = x * 255 / std::max(1, size.width() - 1);
      image.setPixelColor(x, y, QColor(value, value, value));
    }
  }
  return image;
}

}  // namespace

class ChangeGraphTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 縦に平均して横方向の変化を出すこと。列ごとの平均が並ぶ。
  void averagesEachColumnForTheHorizontalDirection() {
    const QImage image = makeHorizontalRamp(QSize(5, 4));
    const auto series = heiran::ui::buildChangeGraphSeries(
        makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Horizontal);

    QCOMPARE(series.redMeans.size(), 5);
    QCOMPARE(series.samplesPerPoint, 4);
    QCOMPARE(series.origin, 0);

    // 列 0 は 0、列 4 は 255。列ごとに一定なので平均もその値になる。
    QVERIFY(std::abs(series.redMeans.at(0) - 0.0F) < 0.01F);
    QVERIFY(std::abs(series.redMeans.at(4) - 1.0F) < 0.01F);
    // 中央の列は 127/255。
    QVERIFY2(std::abs(series.redMeans.at(2) - 127.0F / 255.0F) < 0.01F,
             qPrintable(QString::number(series.redMeans.at(2))));
  }

  // 横に平均して縦方向の変化を出すこと。左右へ変化する画像では、
  // 行ごとの平均はどの行も同じになる。
  void averagesEachRowForTheVerticalDirection() {
    const QImage image = makeHorizontalRamp(QSize(5, 4));
    const auto series = heiran::ui::buildChangeGraphSeries(
        makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Vertical);

    QCOMPARE(series.redMeans.size(), 4);
    QCOMPARE(series.samplesPerPoint, 5);
    for (int index = 1; index < series.redMeans.size(); ++index) {
      QVERIFY(std::abs(series.redMeans.at(index) - series.redMeans.at(0)) < 0.001F);
    }
  }

  // チャネルごとに別の値を出すこと。
  void separatesTheChannels() {
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(255, 128, 0));
    const auto series = heiran::ui::buildChangeGraphSeries(
        makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Horizontal);

    QVERIFY(!series.grayscale);
    QVERIFY(std::abs(series.redMeans.at(0) - 1.0F) < 0.01F);
    QVERIFY(std::abs(series.greenMeans.at(0) - 128.0F / 255.0F) < 0.01F);
    QVERIFY(std::abs(series.blueMeans.at(0) - 0.0F) < 0.01F);
    // 輝度は他のチャネルと同じ 0.0-1.0 でそろえる。
    QVERIFY(series.lumaMeans.at(0) > 0.0F && series.lumaMeans.at(0) < 1.0F);
  }

  // 選択範囲があるときは、そこだけを対象にすること。横軸の左端も選択範囲に合わせる。
  void restrictsToTheSelection() {
    const QImage image = makeHorizontalRamp(QSize(10, 8));
    const auto series =
        heiran::ui::buildChangeGraphSeries(makeAnalysisImage(image, "image", QRect(4, 2, 3, 5)),
                                           heiran::ui::ChangeGraphDirection::Horizontal);

    QVERIFY(series.selectionApplied);
    QCOMPARE(series.redMeans.size(), 3);
    QCOMPARE(series.samplesPerPoint, 5);
    QCOMPARE(series.origin, 4);
    // 選択範囲の先頭は元画像の列 4 にあたる。
    QVERIFY2(std::abs(series.redMeans.at(0) - (4 * 255 / 9) / 255.0F) < 0.01F,
             qPrintable(QString::number(series.redMeans.at(0))));
  }

  // 縦軸の上限に使う最大値が、全チャネルを通した値になること。
  void reportsTheMaximumAcrossChannels() {
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(0, 255, 0));
    const auto series = heiran::ui::buildChangeGraphSeries(
        makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Horizontal);

    QVERIFY(std::abs(series.maximumMean() - 1.0F) < 0.01F);
  }

  // 窓が解析対象を受け取って中身を作ること。操作も出ていること。
  void fillsTheWindowWhenItIsShown() {
    heiran::ui::ChangeGraphWindow window;
    window.showAndRaise();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const auto combos = window.findChildren<QComboBox *>();
    QCOMPARE(combos.size(), 2);
    QVERIFY(window.findChild<QCheckBox *>() != nullptr);

    const QImage image = makeHorizontalRamp(QSize(32, 16));
    window.setAnalysisImages(2, {makeAnalysisImage(image, "cell")});
    QCOMPARE(window.windowTitle(), QString("Change Graph  Page 2"));

    // 描けること。統計は図の中にあるため、描画が通ることで確かめる。
    QImage rendered(window.size(), QImage::Format_ARGB32);
    rendered.fill(Qt::white);
    window.render(&rendered);
    QVERIFY(!rendered.isNull());
  }

  // 向きを変えると計算し直すこと。取っておいた結果を取り違えない。
  void recomputesWhenTheDirectionChanges() {
    const QImage image = makeHorizontalRamp(QSize(16, 8));
    heiran::ui::AnalysisResultCache<heiran::ui::ChangeGraphSeries, heiran::ui::ChangeGraphDirection>
        cache;

    const auto horizontal =
        cache.value(makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Horizontal, [&]() {
          return heiran::ui::buildChangeGraphSeries(makeAnalysisImage(image),
                                                    heiran::ui::ChangeGraphDirection::Horizontal);
        });
    const auto vertical =
        cache.value(makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Vertical, [&]() {
          return heiran::ui::buildChangeGraphSeries(makeAnalysisImage(image),
                                                    heiran::ui::ChangeGraphDirection::Vertical);
        });

    QCOMPARE(cache.computeCount(), 2);
    QCOMPARE(horizontal.redMeans.size(), 16);
    QCOMPARE(vertical.redMeans.size(), 8);

    // 同じ向きなら取っておいた分が使えること。
    cache.value(makeAnalysisImage(image), heiran::ui::ChangeGraphDirection::Horizontal,
                [&]() { return heiran::ui::ChangeGraphSeries{}; });
    QCOMPARE(cache.computeCount(), 2);
  }
};

QTEST_MAIN(ChangeGraphTests)

#include "ChangeGraphTests.moc"
