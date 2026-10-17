#include "heiran/domain/Ssim.hpp"

#include <QtTest>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace heiran::domain;

namespace {

// 参照値 (scikit-image の structural_similarity、gaussian_weights = True、sigma = 1.5、
// use_sample_covariance = False、data_range = 255) を求めたのと、同じ画像。
struct Pair {
  int width = 40;
  int height = 30;
  std::vector<float> base;
  std::vector<float> target;
};

Pair makeReferencePair() {
  Pair pair;
  const int count = pair.width * pair.height;
  for (int index = 0; index < count; ++index) {
    const int x = (index * 37 + (index / pair.width) * 11) % 256;
    const int y = std::clamp(x + ((index * 13) % 101) - 50, 0, 255);
    pair.base.push_back(static_cast<float>(x));
    pair.target.push_back(static_cast<float>(y));
  }
  return pair;
}

std::vector<float> ssimOf(const Pair &pair) {
  std::vector<float> map(pair.base.size(), -9.0F);
  computeSsimRows(pair.base.data(), pair.target.data(), pair.width, pair.height, 0, pair.height,
                  255.0, map.data());
  return map;
}

}  // namespace

class SsimTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 参照の実装と、画素の値と、指標が一致する (内側の平均、最小、閾値を下回った数)。
  void matchesTheReferenceImplementation() {
    const Pair pair = makeReferencePair();
    const auto map = ssimOf(pair);
    const auto at = [&](int row, int column) {
      return map[static_cast<size_t>(row * pair.width + column)];
    };
    QVERIFY(std::abs(at(10, 10) - 0.9231950890616613) < 1e-4);
    QVERIFY(std::abs(at(15, 20) - 0.925714256562929) < 1e-4);
    QVERIFY(std::abs(at(20, 30) - 0.9375590695052483) < 1e-4);

    const auto metrics = computeSsimMetrics(map.data(), pair.width, pair.height, 0.93);
    QCOMPARE(metrics.pixelCount, 600);  // (40 - 10) x (30 - 10)
    QVERIFY(std::abs(metrics.mean - 0.9329702235947704) < 1e-4);
    QVERIFY(std::abs(metrics.minimum - 0.9027679208856326) < 1e-4);
    QCOMPARE(metrics.belowThresholdCount, 234);
  }

  // 端は、端の画素を延ばして補った値になる (窓が外へ出る)。参照 (反射で補う) とは、少し違うが、範囲には収まる。
  void fillsTheBorderByExtendingTheEdge() {
    const Pair pair = makeReferencePair();
    const auto map = ssimOf(pair);
    for (const float value : map) {
      QVERIFY(value > 0.0F && value <= 1.0F);
    }
  }

  // 同じ画像は、どこも 1。
  void identicalImagesScoreOne() {
    Pair pair = makeReferencePair();
    pair.target = pair.base;
    const auto map = ssimOf(pair);
    for (const float value : map) {
      QVERIFY(std::abs(value - 1.0F) < 1e-5F);
    }
    const auto metrics = computeSsimMetrics(map.data(), pair.width, pair.height, 0.99);
    QVERIFY(std::abs(metrics.mean - 1.0) < 1e-5);
    QCOMPARE(metrics.belowThresholdCount, 0);
  }

  // 帯に分けて計算しても、全体を 1 度に計算した結果と、同じになる (並列の処理は、帯ごとに行を分ける)。
  void bandsGiveTheSameResultAsOneCall() {
    Pair pair;
    pair.width = 37;
    pair.height = 150;  // 塊 (64 行) をまたぐ
    for (int index = 0; index < pair.width * pair.height; ++index) {
      pair.base.push_back(static_cast<float>((index * 31 + (index / pair.width) * 7) % 256));
      pair.target.push_back(static_cast<float>((index * 29 + (index / pair.width) * 5) % 256));
    }
    const auto whole = ssimOf(pair);
    std::vector<float> bands(pair.base.size(), -9.0F);
    for (const auto &range : {std::pair{0, 41}, std::pair{41, 100}, std::pair{100, 150}}) {
      computeSsimRows(pair.base.data(), pair.target.data(), pair.width, pair.height, range.first,
                      range.second, 255.0, bands.data());
    }
    QVERIFY(bands == whole);
  }

  // 一様な 2 枚は、明るさが近ければ 1 に近く、離れていれば小さい (構造が無いため、輝度の項だけで決まる)。
  void flatImagesDependOnBrightness() {
    const int width = 16;
    const int height = 16;
    std::vector<float> dark(width * height, 50.0F);
    std::vector<float> similar(width * height, 52.0F);
    std::vector<float> bright(width * height, 200.0F);
    std::vector<float> map(dark.size());
    computeSsimRows(dark.data(), similar.data(), width, height, 0, height, 255.0, map.data());
    const float nearValue = map[8 * width + 8];
    computeSsimRows(dark.data(), bright.data(), width, height, 0, height, 255.0, map.data());
    const float farValue = map[8 * width + 8];
    QVERIFY(nearValue > 0.99F);
    QVERIFY(farValue < 0.5F);
  }

  // 小さな画像 (どちらかの辺が 11 未満) では、全体を数える。
  void countsTheWholeImageWhenItIsSmall() {
    const std::vector<float> flat(8 * 8, 0.9F);
    const auto metrics = computeSsimMetrics(flat.data(), 8, 8, 0.5);
    QCOMPARE(metrics.pixelCount, 64);
    QCOMPARE(computeSsimMetrics(nullptr, 0, 0, 0.5).pixelCount, 0);
  }
};

QTEST_APPLESS_MAIN(SsimTests)
#include "SsimTests.moc"
