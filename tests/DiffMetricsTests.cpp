// 差分の数値指標 (DiffMetrics)。

#include "heiran/domain/DiffMetrics.hpp"

#include <QtTest>
#include <utility>
#include <vector>

using heiran::domain::computeDiffMetrics;

class DiffMetricsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 差が無ければ、誤差は 0 で、PSNR は無限大。
  void reportsAnInfinitePsnrWhenThereIsNoDifference() {
    const std::vector<float> diffs(100, 0.0F);
    const auto metrics = computeDiffMetrics(diffs.data(), diffs.size(), 8.0);

    QCOMPARE(metrics.rmse, 0.0);
    QCOMPARE(metrics.meanAbsDiff, 0.0);
    QVERIFY(metrics.psnrIsInfinite);
    QCOMPARE(metrics.exceededPixelCount, std::int64_t{0});
  }

  // 全画素が一定の差 d のとき、RMSE は d、PSNR は 20 log10(255 / d)。
  void computesRmseAndPsnrForAConstantDifference() {
    const std::vector<float> diffs(64, 5.0F);
    const auto metrics = computeDiffMetrics(diffs.data(), diffs.size(), 8.0);

    QCOMPARE(metrics.rmse, 5.0);
    QCOMPARE(metrics.meanAbsDiff, 5.0);
    QCOMPARE(metrics.maxAbsDiff, 5.0);
    QVERIFY(!metrics.psnrIsInfinite);
    QVERIFY(qAbs(metrics.psnr - 20.0 * std::log10(255.0 / 5.0)) < 1e-9);
  }

  // 差の平均は符号を持つ。明暗が相殺する向きの差は、絶対値の平均とは別に見える。
  void keepsTheSignOfTheMeanDifference() {
    const std::vector<float> brighter = {4.0F, 4.0F, 4.0F, 4.0F};
    QCOMPARE(computeDiffMetrics(brighter.data(), brighter.size(), 8.0).meanSignedDiff, 4.0);

    const std::vector<float> darker = {-4.0F, -4.0F};
    QCOMPARE(computeDiffMetrics(darker.data(), darker.size(), 8.0).meanSignedDiff, -4.0);

    const std::vector<float> cancelling = {6.0F, -6.0F};
    const auto metrics = computeDiffMetrics(cancelling.data(), cancelling.size(), 8.0);
    QCOMPARE(metrics.meanSignedDiff, 0.0);
    QCOMPARE(metrics.meanAbsDiff, 6.0);
  }

  // 閾値は「超えた」画素を数える。ちょうどは含めない。
  void countsPixelsThatExceedTheThreshold() {
    const std::vector<float> diffs = {0.0F, 8.0F, 8.5F, -9.0F};
    const auto metrics = computeDiffMetrics(diffs.data(), diffs.size(), 8.0);

    QCOMPARE(metrics.exceededPixelCount, std::int64_t{2});
    QCOMPARE(metrics.maxAbsDiff, 9.0);
  }

  // ピークの尺度を変えられること (0-1 の尺度なら 1.0)。
  void honorsThePeakScale() {
    const std::vector<float> diffs(16, 0.1F);
    const auto metrics = computeDiffMetrics(diffs.data(), diffs.size(), 0.05, 1.0);
    QVERIFY(qAbs(metrics.psnr - 20.0) < 1e-6);
  }

  void handlesAnEmptyInput() {
    const auto metrics = computeDiffMetrics(nullptr, 0, 8.0);
    QCOMPARE(metrics.pixelCount, std::int64_t{0});
    QVERIFY(metrics.psnrIsInfinite);
  }
  // 帯に分けて数えて、足し合わせても、1 度に数えた結果と同じになる (並列の指標のため)。
  void mergedPartialsEqualOneCall() {
    std::vector<float> diffs(1000);
    for (size_t index = 0; index < diffs.size(); ++index) {
      diffs[index] = static_cast<float>((static_cast<int>(index * 37) % 61) - 30) / 3.0F;
    }
    const auto whole = computeDiffMetrics(diffs.data(), diffs.size(), 4.0);

    heiran::domain::DiffMetricsPartial merged;
    for (const auto &range : {std::pair{size_t{0}, size_t{300}}, std::pair{size_t{300}, size_t{650}},
                              std::pair{size_t{650}, size_t{1000}}}) {
      heiran::domain::DiffMetricsPartial part;
      part.add(diffs.data() + range.first, range.second - range.first, 4.0);
      merged.merge(part);
    }
    const auto parts = merged.finish();
    QCOMPARE(parts.pixelCount, whole.pixelCount);
    QCOMPARE(parts.maxAbsDiff, whole.maxAbsDiff);
    QCOMPARE(parts.exceededPixelCount, whole.exceededPixelCount);
    QVERIFY(std::abs(parts.meanAbsDiff - whole.meanAbsDiff) < 1e-9);
    QVERIFY(std::abs(parts.meanSignedDiff - whole.meanSignedDiff) < 1e-9);
    QVERIFY(std::abs(parts.rmse - whole.rmse) < 1e-9);
  }
};

QTEST_MAIN(DiffMetricsTests)

#include "DiffMetricsTests.moc"
