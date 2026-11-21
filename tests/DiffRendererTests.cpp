#include "heiran/ui/DiffRenderer.hpp"

#include <QImage>
#include <QtTest>
#include <cmath>

using namespace heiran::ui;
using heiran::domain::DiffMode;
using heiran::domain::DiffSettings;
using heiran::domain::DiffViewMode;

namespace {

// 解析用バッファを持たない 8bit 画像。表示用画像がそのまま解析入力になる。
DiffInput makeInput(const QImage &image) {
  AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();
  return {.image = image, .sampler = sampler, .origin = QPoint(0, 0)};
}

QImage solid(QSize size, int value) {
  QImage image(size, QImage::Format_RGBA8888);
  image.fill(QColor(value, value, value));
  return image;
}

}  // namespace

class DiffRendererTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void groupsNearbyChangesAndSortsSeparatedRegions() {
    DiffField field{.size = {80, 48}, .values = std::vector<float>(80 * 48, 0.0F)};
    const auto set = [&](int x, int y, float difference) { field.values[y * 80 + x] = difference; };
    set(15, 3, 10.0F);
    set(16, 4, -12.0F);
    set(71, 6, 11.0F);
    set(2, 34, 9.0F);
    set(40, 20, 8.0F);
    QCOMPARE(findDiffRegions(field, 8.0),
             (QVector<QRect>{QRect(15, 3, 2, 2), QRect(71, 6, 1, 1), QRect(2, 34, 1, 1)}));
    QVERIFY(findDiffRegions(field, 12.0).isEmpty());
    field.values = std::vector<float>(80 * 48, 0.1F);
    QVERIFY(findDiffRegions(field, 0.1).isEmpty());
  }

  void usesTheSsimThresholdAndExcludesItsWindowBorder() {
    DiffField field{
        .kind = DiffFieldKind::Ssim, .size = {16, 16}, .values = std::vector<float>(16 * 16, 0.0F)};
    field.values[0] = 1.0F;
    field.values[7 * 16 + 6] = 0.05F;
    field.values[8 * 16 + 6] = 0.2F;
    QCOMPARE(findDiffRegions(field, 0.9), (QVector<QRect>{QRect(6, 8, 1, 1)}));
  }

  void alignsBlinkFramesUsingTheDiffSizeSettings() {
    using heiran::domain::DiffSizeMatch;
    const auto base = solid({10, 20}, 50);
    const auto target = solid({12, 15}, 220);
    const auto overlap = prepareDiffBlinkFrames(base, target, DiffSizeMatch::Overlap);
    QCOMPARE(overlap[0].size(), QSize(10, 15));
    QCOMPARE(overlap[1].size(), overlap[0].size());
    QCOMPARE(overlap[0].pixelColor(0, 0), QColor(50, 50, 50));
    QCOMPARE(overlap[1].pixelColor(0, 0), QColor(220, 220, 220));
    const auto scaled = prepareDiffBlinkFrames(base, target, DiffSizeMatch::ToTarget);
    QCOMPARE(scaled[0].size(), target.size());
    QCOMPARE(scaled[1].size(), target.size());
  }

  // 絶対値の差。自動の範囲合わせを切れば、差の大きさがそのまま階調になる。
  void rendersTheAbsoluteDifference() {
    DiffSettings settings;
    settings.autoRange = false;
    const auto output =
        renderDiff(makeInput(solid({8, 4}, 100)), makeInput(solid({8, 4}, 140)), settings);
    QVERIFY2(output.ok, qPrintable(output.error));
    QCOMPARE(output.image.size(), QSize(8, 4));
    QCOMPARE(qRed(output.image.pixel(3, 2)), 40);
    QCOMPARE(output.metrics.pixelCount, 32);
    QCOMPARE(output.metrics.maxAbsDiff, 40.0);
    QCOMPARE(output.metrics.meanSignedDiff, 40.0);
  }

  // 自動の範囲合わせは、差の最大を 255 に合わせる。
  void autoRangeStretchesTheLargestDifferenceTo255() {
    const auto output =
        renderDiff(makeInput(solid({4, 4}, 100)), makeInput(solid({4, 4}, 110)), DiffSettings{});
    QVERIFY(output.ok);
    QCOMPARE(qRed(output.image.pixel(0, 0)), 255);
  }

  // 符号付きは、正が赤、負が青。
  void signedColorSeparatesThePositiveAndTheNegative() {
    DiffSettings settings;
    settings.mode = DiffMode::Signed;
    settings.view = DiffViewMode::SignedColor;
    settings.autoRange = false;
    const auto brighter =
        renderDiff(makeInput(solid({2, 2}, 100)), makeInput(solid({2, 2}, 130)), settings);
    QCOMPARE(qRed(brighter.image.pixel(0, 0)), 30);
    QCOMPARE(qBlue(brighter.image.pixel(0, 0)), 0);
    const auto darker =
        renderDiff(makeInput(solid({2, 2}, 130)), makeInput(solid({2, 2}, 100)), settings);
    QCOMPARE(qRed(darker.image.pixel(0, 0)), 0);
    QCOMPARE(qBlue(darker.image.pixel(0, 0)), 30);
  }

  // 2 値は、閾値を超えたかどうか。
  void binaryMarksOnlyPixelsAboveTheThreshold() {
    QImage target = solid({4, 1}, 100);
    target.setPixelColor(1, 0, QColor(120, 120, 120));  // 差 20
    target.setPixelColor(2, 0, QColor(105, 105, 105));  // 差 5
    DiffSettings settings;
    settings.mode = DiffMode::Binary;
    settings.threshold = 8.0;
    const auto output = renderDiff(makeInput(solid({4, 1}, 100)), makeInput(target), settings);
    QVERIFY(output.ok);
    QCOMPARE(qRed(output.image.pixel(0, 0)), 0);
    QCOMPARE(qRed(output.image.pixel(1, 0)), 255);
    QCOMPARE(qRed(output.image.pixel(2, 0)), 0);
    QCOMPARE(output.metrics.exceededPixelCount, 1);
  }

  // 重ね合わせは、基準の画像と混ぜる。差が無ければ、基準の半分の明るさになる (差のぶんは 0
  // を混ぜる)。
  void overlayBlendsWithTheBaseImage() {
    DiffSettings settings;
    settings.view = DiffViewMode::Overlay;
    settings.autoRange = false;
    const auto output =
        renderDiff(makeInput(solid({2, 2}, 100)), makeInput(solid({2, 2}, 100)), settings);
    QVERIFY(output.ok);
    QCOMPARE(qRed(output.image.pixel(0, 0)), 50);
  }

  // 同じ画像は、差が無い。
  void identicalImagesHaveNoDifference() {
    const auto output =
        renderDiff(makeInput(solid({4, 4}, 77)), makeInput(solid({4, 4}, 77)), DiffSettings{});
    QVERIFY(output.ok);
    QCOMPARE(output.metrics.maxAbsDiff, 0.0);
    QVERIFY(output.metrics.psnrIsInfinite);
  }

  // ---- 大きさの違う画像 ----

  // 縮めるときは、重なる元の画素の平均。2 倍に縮めると、2x2 の平均になる。
  void downscalingAveragesTheCoveredPixels() {
    QVector<float> plane = {0.0F,  100.0F, 200.0F, 40.0F,  //
                            50.0F, 150.0F, 20.0F,  60.0F};
    const auto half = resizeLumaPlane(plane, QSize(4, 2), QSize(2, 1));
    QCOMPARE(half.size(), 2);
    QCOMPARE(half.at(0), (0.0F + 100.0F + 50.0F + 150.0F) / 4.0F);
    QCOMPARE(half.at(1), (200.0F + 40.0F + 20.0F + 60.0F) / 4.0F);
    // 割り切れない縮小でも、全体の平均は保たれる (重みの合計が 1)。
    QVector<float> flat(7 * 5, 80.0F);
    for (const float value : resizeLumaPlane(flat, QSize(7, 5), QSize(3, 2))) {
      QVERIFY(std::abs(value - 80.0F) < 1e-4F);
    }
  }

  // 広げるときは線形補間。一様な面は変わらず、傾きのある面は傾きのまま。
  void upscalingInterpolatesLinearly() {
    QVector<float> ramp = {0.0F, 100.0F};
    const auto wide = resizeLumaPlane(ramp, QSize(2, 1), QSize(4, 1));
    QCOMPARE(wide.size(), 4);
    QVERIFY(std::abs(wide.at(0) - 0.0F) < 1e-3F);
    QVERIFY(std::abs(wide.at(1) - 25.0F) < 1e-3F);
    QVERIFY(std::abs(wide.at(2) - 75.0F) < 1e-3F);
    QVERIFY(std::abs(wide.at(3) - 100.0F) < 1e-3F);
    // 同じ大きさ、不正な入力は、そのまま返す。
    QCOMPARE(resizeLumaPlane(ramp, QSize(2, 1), QSize(2, 1)), ramp);
    QCOMPARE(resizeLumaPlane(ramp, QSize(5, 5), QSize(2, 2)), ramp);
  }

  // 一様な画像は、どの大きさへ合わせても、差が変わらない (リサイズで値が動かない)。
  void sizeMatchModesPickTheExpectedSize() {
    using heiran::domain::DiffSizeMatch;
    const DiffInput small = makeInput(solid({8, 6}, 100));
    const DiffInput large = makeInput(solid({16, 12}, 130));
    const DiffInput wide = makeInput(solid({20, 4}, 130));  // 面積 80 (small は 48)
    struct Case {
      DiffSizeMatch match;
      const DiffInput *base;
      const DiffInput *target;
      QSize expected;
      double expectedDifference;
    };
    const Case cases[] = {{DiffSizeMatch::ToBase, &small, &large, QSize(8, 6), 30.0},
                          {DiffSizeMatch::ToTarget, &small, &large, QSize(16, 12), 30.0},
                          {DiffSizeMatch::ToLarger, &small, &large, QSize(16, 12), 30.0},
                          {DiffSizeMatch::ToLarger, &large, &small, QSize(16, 12), -30.0},
                          {DiffSizeMatch::ToLarger, &small, &wide, QSize(20, 4), 30.0}};
    for (const auto &item : cases) {
      DiffSettings settings;
      settings.sizeMatch = item.match;
      settings.autoRange = false;
      const auto output = renderDiff(*item.base, *item.target, settings);
      QVERIFY2(output.ok, qPrintable(output.error));
      QCOMPARE(output.image.size(), item.expected);
      // 値は、リサイズしても同じ差のまま (一様な画像なので、補間や平均で動かない)。
      QVERIFY2(std::abs(output.metrics.meanSignedDiff - item.expectedDifference) < 0.01,
               qPrintable(QString::number(output.metrics.meanSignedDiff)));
      QCOMPARE(output.metrics.pixelCount,
               static_cast<qint64>(item.expected.width()) * item.expected.height());
    }
  }

  // 大きさ違いで、縮小してから比べる。細かい模様は、平均されて消える
  // (半分の大きさへ縮めた市松の平均は一様な灰色)。
  void downscaledDifferenceAveragesFineDetail() {
    QImage checker(8, 8, QImage::Format_RGBA8888);
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        const int value = (x + y) % 2 == 0 ? 0 : 200;
        checker.setPixelColor(x, y, QColor(value, value, value));
      }
    }
    DiffSettings settings;
    settings.sizeMatch = heiran::domain::DiffSizeMatch::ToBase;
    settings.autoRange = false;
    // 基準は 4x4 の一様な 100。比べる側の 8x8 の市松 (平均 100) を 4x4 へ縮める。
    const auto output = renderDiff(makeInput(solid({4, 4}, 100)), makeInput(checker), settings);
    QVERIFY2(output.ok, qPrintable(output.error));
    QVERIFY2(output.metrics.maxAbsDiff < 1.0,
             qPrintable(QString::number(output.metrics.maxAbsDiff)));
  }

  // 合わせる指定が無ければ、左上をそろえて、重なる部分だけを比べる。はみ出した部分は比べない。
  void comparesOnlyTheOverlapWhenSizesDiffer() {
    // 基準は 6x4。右の 2 列だけ、比べる側 (4x3) には無い部分が大きく違う。
    QImage base = solid({6, 4}, 100);
    for (int y = 0; y < 4; ++y) {
      for (int x = 4; x < 6; ++x) {
        base.setPixelColor(x, y, QColor(250, 250, 250));
      }
    }
    QImage target = solid({4, 3}, 100);
    target.setPixelColor(0, 0, QColor(140, 140, 140));  // 左上だけ違う
    DiffSettings settings;
    settings.autoRange = false;
    const auto output = renderDiff(makeInput(base), makeInput(target), settings);
    QVERIFY2(output.ok, qPrintable(output.error));
    QCOMPARE(output.image.size(), QSize(4, 3));
    QCOMPARE(output.metrics.pixelCount, 12);
    // 左上が重なる。はみ出した部分 (右 2 列、下 1 行) は、差に入らない。
    QCOMPARE(output.metrics.maxAbsDiff, 40.0);
    QCOMPARE(qRed(output.image.pixel(0, 0)), 40);
    QCOMPARE(qRed(output.image.pixel(3, 2)), 0);
    QCOMPARE(output.metrics.exceededPixelCount, 1);

    // 逆向き (比べる側が大きい) でも、小さいほうの大きさ。符号は、比べる側 - 基準。
    const auto reversed = renderDiff(makeInput(target), makeInput(base), settings);
    QVERIFY(reversed.ok);
    QCOMPARE(reversed.image.size(), QSize(4, 3));
    QCOMPARE(reversed.metrics.meanSignedDiff, -40.0 / 12.0);
  }

  // 重ね合わせの表示でも、重なる部分だけを、基準の画像の左上から取る。
  void overlapOnlyUsesTheTopLeftOfTheBaseImageForOverlay() {
    DiffSettings settings;
    settings.view = DiffViewMode::Overlay;
    settings.autoRange = false;
    const auto output =
        renderDiff(makeInput(solid({6, 4}, 100)), makeInput(solid({3, 2}, 100)), settings);
    QVERIFY(output.ok);
    QCOMPARE(output.image.size(), QSize(3, 2));
    QCOMPARE(qRed(output.image.pixel(2, 1)), 50);
  }

  // 空の入力は、理由を付けて断る。
  void rejectsEmptyInputs() {
    const auto empty = renderDiff(DiffInput{}, makeInput(solid({4, 4}, 1)), DiffSettings{});
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());
  }

  // 設定の文字列の往復。保存形式 (.hrn) へ使う。
  void settingsRoundTripThroughStrings() {
    for (const auto mode : {DiffMode::Absolute, DiffMode::Signed, DiffMode::Binary,
                            DiffMode::DeltaE, DiffMode::Ssim, DiffMode::Wipe}) {
      QCOMPARE(heiran::domain::diffModeFromString(heiran::domain::toDiffModeString(mode)), mode);
    }
    for (const auto view :
         {DiffViewMode::Grayscale, DiffViewMode::SignedColor, DiffViewMode::Overlay}) {
      QCOMPARE(heiran::domain::diffViewModeFromString(heiran::domain::toDiffViewModeString(view)),
               view);
    }
    QCOMPARE(heiran::domain::diffModeFromString("unknown"), DiffMode::Absolute);
    for (const auto match :
         {heiran::domain::DiffSizeMatch::Overlap, heiran::domain::DiffSizeMatch::ToBase,
          heiran::domain::DiffSizeMatch::ToTarget, heiran::domain::DiffSizeMatch::ToLarger}) {
      QCOMPARE(
          heiran::domain::diffSizeMatchFromString(heiran::domain::toDiffSizeMatchString(match)),
          match);
    }
  }

  // 色差 (ΔE2000)。同じ画像は 0、黒と白は 100 に近い。輝度の差とは別の指標として出る。
  void rendersTheColorDifference() {
    DiffSettings settings;
    settings.mode = DiffMode::DeltaE;
    settings.threshold = 2.3;
    const auto same =
        renderDiff(makeInput(solid({8, 4}, 90)), makeInput(solid({8, 4}, 90)), settings);
    QVERIFY2(same.ok, qPrintable(same.error));
    QVERIFY(same.metrics.colorDifference);
    QCOMPARE(same.metrics.maxAbsDiff, 0.0);
    QCOMPARE(same.metrics.exceededPixelCount, 0);

    const auto extremes =
        renderDiff(makeInput(solid({8, 4}, 0)), makeInput(solid({8, 4}, 255)), settings);
    QVERIFY(extremes.ok);
    QVERIFY(extremes.metrics.maxAbsDiff > 99.0 && extremes.metrics.maxAbsDiff < 101.0);
    QCOMPARE(extremes.metrics.pixelCount, 32);
    QCOMPARE(extremes.metrics.exceededPixelCount, 32);
    // 自動の範囲合わせで、最大の差が最も明るくなる。
    QCOMPARE(qRed(extremes.image.pixel(3, 2)), 255);
  }

  // 輝度がほぼ同じで色相が違う組を、輝度の差は見逃し、色差は拾う。
  void colorDifferenceSeesWhatLumaMisses() {
    QImage red(8, 4, QImage::Format_RGBA8888);
    red.fill(QColor(255, 0, 0));
    QImage green(8, 4, QImage::Format_RGBA8888);
    // 赤 (0.2126 * 255 = 54) と同じ輝度になる緑 (0.7152 * g = 54.2)。
    green.fill(QColor(0, 76, 0));

    DiffSettings luma;
    luma.autoRange = false;
    const auto byLuma = renderDiff(makeInput(red), makeInput(green), luma);
    QVERIFY(byLuma.ok);
    QVERIFY(byLuma.metrics.maxAbsDiff < 1.0);

    DiffSettings color = luma;
    color.mode = DiffMode::DeltaE;
    const auto byColor = renderDiff(makeInput(red), makeInput(green), color);
    QVERIFY(byColor.ok);
    QVERIFY2(byColor.metrics.maxAbsDiff > 30.0,
             qPrintable(QString::number(byColor.metrics.maxAbsDiff)));
  }

  // 色差も、大きさの違う画像を、重なる部分だけで比べる。
  void colorDifferenceComparesTheOverlapOnly() {
    DiffSettings settings;
    settings.mode = DiffMode::DeltaE;
    const auto output =
        renderDiff(makeInput(solid({8, 6}, 100)), makeInput(solid({5, 9}, 100)), settings);
    QVERIFY(output.ok);
    QCOMPARE(output.image.size(), QSize(5, 6));
    QCOMPARE(output.metrics.pixelCount, 30);
  }

  // ワイプ: 境目の左が基準、右が比べる側。境目の位置は、幅に対する割合。
  void wipesLeftToRight() {
    DiffSettings settings;
    settings.mode = DiffMode::Wipe;
    settings.wipePosition = 0.25;  // 幅 8 の 2 画素目まで基準
    const auto output =
        renderDiff(makeInput(solid({8, 4}, 40)), makeInput(solid({8, 4}, 200)), settings);
    QVERIFY2(output.ok, qPrintable(output.error));
    QVERIFY(output.metrics.wipe);
    QCOMPARE(output.image.size(), QSize(8, 4));
    for (int x = 0; x < 8; ++x) {
      QCOMPARE(qRed(output.image.pixel(x, 2)), x < 2 ? 40 : 200);
    }
    // 位置の両端: 0 は、すべて比べる側。1 は、すべて基準。
    settings.wipePosition = 0.0;
    QCOMPARE(qRed(renderDiff(makeInput(solid({8, 4}, 40)), makeInput(solid({8, 4}, 200)), settings)
                      .image.pixel(0, 0)),
             200);
    settings.wipePosition = 1.0;
    QCOMPARE(qRed(renderDiff(makeInput(solid({8, 4}, 40)), makeInput(solid({8, 4}, 200)), settings)
                      .image.pixel(7, 3)),
             40);
  }

  // 上下の切り方: 上が基準、下が比べる側。
  void wipesTopToBottom() {
    DiffSettings settings;
    settings.mode = DiffMode::Wipe;
    settings.wipeOrientation = heiran::domain::WipeOrientation::TopBottom;
    settings.wipePosition = 0.5;
    const auto output =
        renderDiff(makeInput(solid({6, 8}, 40)), makeInput(solid({6, 8}, 200)), settings);
    QVERIFY(output.ok);
    QCOMPARE(qRed(output.image.pixel(3, 3)), 40);
    QCOMPARE(qRed(output.image.pixel(3, 4)), 200);
  }

  // 大きさが違うときは、差分と同じ規則 (既定は、重なる部分だけ)。
  void wipeComparesTheOverlapOnly() {
    DiffSettings settings;
    settings.mode = DiffMode::Wipe;
    const auto output =
        renderDiff(makeInput(solid({8, 6}, 40)), makeInput(solid({5, 9}, 200)), settings);
    QVERIFY(output.ok);
    QCOMPARE(output.image.size(), QSize(5, 6));
  }

  // SSIM: 同じ画像は 1 (見せる値は 0)。明るさが同じでも、構造が違えば、1 を下回る。
  void rendersTheStructuralSimilarity() {
    DiffSettings settings;
    settings.mode = DiffMode::Ssim;
    settings.threshold = 0.9;

    QImage pattern(32, 24, QImage::Format_RGBA8888);
    QImage shifted(32, 24, QImage::Format_RGBA8888);
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 32; ++x) {
        const int a = (x * 37 + y * 11) % 256;
        const int b = (x * 29 + y * 17) % 256;
        pattern.setPixelColor(x, y, QColor(a, a, a));
        shifted.setPixelColor(x, y, QColor(b, b, b));
      }
    }
    const auto same = renderDiff(makeInput(pattern), makeInput(pattern), settings);
    QVERIFY2(same.ok, qPrintable(same.error));
    QVERIFY(same.metrics.ssim);
    QVERIFY(std::abs(same.metrics.ssimMean - 1.0) < 1e-4);
    QCOMPARE(same.metrics.exceededPixelCount, 0);
    QCOMPARE(qRed(same.image.pixel(10, 10)), 0);
    // 窓がすべて画像の中に入る内側だけを数える: (32 - 10) x (24 - 10)。
    QCOMPARE(same.metrics.pixelCount, 22 * 14);

    const auto different = renderDiff(makeInput(pattern), makeInput(shifted), settings);
    QVERIFY(different.ok);
    QVERIFY(different.metrics.ssimMean < 0.5);
    QVERIFY(different.metrics.exceededPixelCount > 0);
    QVERIFY(different.metrics.meanAbsDiff > 0.5);  // 1 - SSIM
    QVERIFY(different.metrics.ssimMin <= different.metrics.ssimMean);
    // 自動の範囲合わせで、最も似ていない所が、最も明るくなる。
    int brightest = 0;
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 32; ++x) {
        brightest = std::max(brightest, qRed(different.image.pixel(x, y)));
      }
    }
    QCOMPARE(brightest, 255);
  }

  // 取っておいた場から作った絵と指標は、最初から作ったものと、まったく同じになる
  // (倍率、見せ方、閾値、範囲合わせの、どの組でも)。
  void rerenderingFromTheFieldMatchesRenderingFromScratch() {
    QImage base(23, 17, QImage::Format_RGBA8888);
    QImage target(23, 17, QImage::Format_RGBA8888);
    for (int y = 0; y < 17; ++y) {
      for (int x = 0; x < 23; ++x) {
        base.setPixelColor(x, y,
                           QColor((x * 37 + y * 11) % 256, (x * 5 + y * 23) % 256, (x * 13) % 256));
        target.setPixelColor(
            x, y, QColor((x * 31 + y * 7) % 256, (x * 9 + y * 17) % 256, (y * 19) % 256));
      }
    }
    const DiffInput baseInput = makeInput(base);
    const DiffInput targetInput = makeInput(target);

    for (const auto mode : {DiffMode::Absolute, DiffMode::Signed, DiffMode::Binary,
                            DiffMode::DeltaE, DiffMode::Ssim}) {
      DiffSettings first;
      first.mode = mode;
      QString error;
      const auto field = computeDiffField(baseInput, targetInput, first, &error);
      QVERIFY2(field != nullptr, qPrintable(error));
      QVERIFY(diffFieldServes(*field, first));

      for (const auto view :
           {DiffViewMode::Grayscale, DiffViewMode::SignedColor, DiffViewMode::Overlay}) {
        for (const double gain : {0.5, 1.0, 4.0}) {
          for (const bool autoRange : {true, false}) {
            for (const double threshold : {0.5, 8.0}) {
              DiffSettings settings = first;
              settings.view = view;
              settings.gain = gain;
              settings.autoRange = autoRange;
              settings.threshold = threshold;
              // 計算の種類と、大きさの合わせ方が同じなら、同じ場で足りる。
              QVERIFY(diffFieldServes(*field, settings));
              const auto fromField = renderDiffFromField(*field, baseInput, settings);
              const auto scratch = renderDiff(baseInput, targetInput, settings);
              QVERIFY2(fromField.ok && scratch.ok, qPrintable(scratch.error));
              QVERIFY2(fromField.image == scratch.image,
                       qPrintable(QString("mode %1 view %2")
                                      .arg(static_cast<int>(mode))
                                      .arg(static_cast<int>(view))));
              QCOMPARE(fromField.metrics.maxAbsDiff, scratch.metrics.maxAbsDiff);
              QCOMPARE(fromField.metrics.meanAbsDiff, scratch.metrics.meanAbsDiff);
              QCOMPARE(fromField.metrics.exceededPixelCount, scratch.metrics.exceededPixelCount);
              QCOMPARE(fromField.metrics.pixelCount, scratch.metrics.pixelCount);
            }
          }
        }
      }
    }
  }

  // 場を使い回してよいのは、計算の種類と、大きさの合わせ方が同じときだけ。absolute、signed、binary
  // は、同じ場を使う。
  void aFieldServesOnlyTheSameKindAndSizeMatch() {
    const DiffInput input = makeInput(solid({8, 6}, 100));
    DiffSettings absolute;
    QString error;
    const auto field = computeDiffField(input, input, absolute, &error);
    QVERIFY(field != nullptr);
    DiffSettings signedMode = absolute;
    signedMode.mode = DiffMode::Signed;
    QVERIFY(diffFieldServes(*field, signedMode));
    signedMode.mode = DiffMode::Binary;
    QVERIFY(diffFieldServes(*field, signedMode));
    DiffSettings other = absolute;
    other.mode = DiffMode::DeltaE;
    QVERIFY(!diffFieldServes(*field, other));
    other.mode = DiffMode::Ssim;
    QVERIFY(!diffFieldServes(*field, other));
    other.mode = DiffMode::Wipe;
    QVERIFY(!diffFieldServes(*field, other));
    DiffSettings resized = absolute;
    resized.sizeMatch = heiran::domain::DiffSizeMatch::ToBase;
    QVERIFY(!diffFieldServes(*field, resized));
    // 入力が足りなければ、null と理由。
    QVERIFY(computeDiffField(DiffInput{}, input, absolute, &error) == nullptr);
    QVERIFY(!error.isEmpty());
  }
};

QTEST_MAIN(DiffRendererTests)

#include "DiffRendererTests.moc"
