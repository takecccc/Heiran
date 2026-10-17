#include "heiran/domain/Scopes.hpp"
#include "heiran/ui/ScopeBuilder.hpp"

#include <QtTest>
#include <cmath>
#include <limits>
#include <vector>

using namespace heiran::domain;

namespace {

// BT.709 の輝度の重み。
constexpr double kWr = 0.2126;
constexpr double kWg = 0.7152;
constexpr double kWb = 0.0722;

std::uint32_t at(const WaveformCounts &counts, const std::vector<std::uint32_t> &plane, int level,
                 int column) {
  return plane[static_cast<size_t>(level) * counts.columns + column];
}

}  // namespace

class ScopesTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 左の列が白、右の列が黒。波形の左の列は、最上段、右の列は、最下段に集まる (列ごとの値の分布)。
  void countsWhiteAndBlackColumnsAtTheTopAndTheBottom() {
    const int width = 2;
    const int height = 3;
    const std::vector<float> red = {1, 0, 1, 0, 1, 0};
    WaveformCounts counts;
    counts.allocate(2, 256, width, height);
    accumulateWaveformRows(red.data(), red.data(), red.data(), width, 0, height, kWr, kWg, kWb,
                           &counts);
    QCOMPARE(at(counts, counts.luma, 255, 0), 3U);
    QCOMPARE(at(counts, counts.luma, 0, 1), 3U);
    QCOMPARE(at(counts, counts.red, 255, 0), 3U);
    QCOMPARE(at(counts, counts.green, 0, 1), 3U);
    // ほかの升目は、空。
    std::uint64_t total = 0;
    for (const auto count : counts.luma) {
      total += count;
    }
    QCOMPARE(total, std::uint64_t{6});
  }

  // 画像の幅より少ない升目へ、列を割り当てる。1 つの列に入る画素の数は、`samplesPerColumn` で分かる。
  void mapsImageColumnsToFewerBins() {
    const int width = 8;
    const int height = 2;
    std::vector<float> plane(width * height, 0.5F);
    WaveformCounts counts;
    counts.allocate(4, 256, width, height);
    accumulateWaveformRows(plane.data(), plane.data(), plane.data(), width, 0, height, kWr, kWg,
                           kWb, &counts);
    for (int column = 0; column < 4; ++column) {
      QCOMPARE(at(counts, counts.luma, 128, column), 4U);  // 2 列 x 2 行
    }
    QCOMPARE(counts.samplesPerColumn(), 4.0);
  }

  // 範囲外 (HDR の 1.0 超え、負、NaN) は、端の升目へ収める。落ちない。
  void clampsOutOfRangeValues() {
    const std::vector<float> values = {5.0F, -2.0F, std::numeric_limits<float>::quiet_NaN()};
    WaveformCounts counts;
    counts.allocate(3, 16, 3, 1);
    accumulateWaveformRows(values.data(), values.data(), values.data(), 3, 0, 1, kWr, kWg, kWb,
                           &counts);
    QCOMPARE(at(counts, counts.red, 15, 0), 1U);
    QCOMPARE(at(counts, counts.red, 0, 1), 1U);
    QCOMPARE(at(counts, counts.red, 0, 2), 1U);
  }

  // 帯に分けて数えて、足し合わせても、1 度に数えた結果と同じ。
  void mergedBandsEqualOneCall() {
    const int width = 17;
    const int height = 40;
    std::vector<float> plane(width * height);
    for (size_t index = 0; index < plane.size(); ++index) {
      plane[index] = static_cast<float>((index * 37) % 101) / 100.0F;
    }
    WaveformCounts whole;
    whole.allocate(9, 64, width, height);
    accumulateWaveformRows(plane.data(), plane.data(), plane.data(), width, 0, height, kWr, kWg,
                           kWb, &whole);
    WaveformCounts merged;
    merged.allocate(9, 64, width, height);
    for (const auto &range : {std::pair{0, 13}, std::pair{13, 27}, std::pair{27, 40}}) {
      WaveformCounts part;
      part.allocate(9, 64, width, height);
      accumulateWaveformRows(plane.data(), plane.data(), plane.data(), width, range.first,
                             range.second, kWr, kWg, kWb, &part);
      merged.merge(part);
    }
    QVERIFY(merged.luma == whole.luma);
    QVERIFY(merged.red == whole.red);
  }

  // ベクトルスコープ: 無彩色は、中心。赤は、+Cr の端 (上) の、-Cb 寄り。
  void placesGrayAtTheCenterAndRedAtTheTop() {
    const std::vector<float> gray = {0.5F};
    const std::vector<float> one = {1.0F};
    const std::vector<float> zero = {0.0F};
    VectorscopeCounts counts;
    counts.allocate(256);
    accumulateVectorscopeRows(gray.data(), gray.data(), gray.data(), 1, 0, 1, kWr, kWg, kWb, &counts);
    // 中心: 列 128、行 127 (上が +Cr のため、縦は反転)。
    QCOMPARE(counts.counts[127 * 256 + 128], 1U);

    VectorscopeCounts red;
    red.allocate(256);
    accumulateVectorscopeRows(one.data(), zero.data(), zero.data(), 1, 0, 1, kWr, kWg, kWb, &red);
    QCOMPARE(red.total, std::int64_t{1});
    // Cr = 0.5 (端) と、Cb = -0.1146 (中心より左) へ、1 つ。
    const auto chroma = chromaOf(1.0, 0.0, 0.0, kWr, kWg, kWb);
    QVERIFY(std::abs(chroma.cr - 0.5) < 1e-9);
    QVERIFY(std::abs(chroma.cb + 0.1146) < 1e-3);
    QCOMPARE(red.counts[0 * 256 + 98], 1U);
  }

  // 目盛りの位置に使う色差。補色は、反対側へ出る (黄は、青と、中心に対して反対)。
  void complementaryColorsAreOpposite() {
    const auto yellow = chromaOf(1, 1, 0, kWr, kWg, kWb);
    const auto blue = chromaOf(0, 0, 1, kWr, kWg, kWb);
    QVERIFY(std::abs(yellow.cb + blue.cb) < 1e-9);
    QVERIFY(std::abs(yellow.cr + blue.cr) < 1e-9);
  }

  // 波形の絵: 大きさと、白の列が上、黒の列が下。濃さは、列ごとの密度で決まる。
  void rendersTheWaveformImage() {
    WaveformCounts counts;
    counts.allocate(2, 256, 2, 4);
    const std::vector<float> plane = {1, 0, 1, 0, 1, 0, 1, 0};
    accumulateWaveformRows(plane.data(), plane.data(), plane.data(), 2, 0, 4, kWr, kWg, kWb, &counts);

    const QImage luma = heiran::ui::renderWaveformImage(counts, heiran::ui::WaveformMode::Luma, 1.0);
    QCOMPARE(luma.size(), QSize(2, 256));
    QVERIFY(qGray(luma.pixel(0, 0)) > 200);    // 左の列 (白) は、最上段
    QVERIFY(qGray(luma.pixel(0, 128)) == 0);   // 途中は、空
    QVERIFY(qGray(luma.pixel(1, 255)) > 200);  // 右の列 (黒) は、最下段

    const QImage parade = heiran::ui::renderWaveformImage(counts, heiran::ui::WaveformMode::Parade, 1.0);
    QCOMPARE(parade.size(), QSize(6, 256));
    // R の絵 (左の 2 列) は赤、G の絵 (中) は緑。
    QVERIFY(qRed(parade.pixel(0, 0)) > 200 && qGreen(parade.pixel(0, 0)) == 0);
    QVERIFY(qGreen(parade.pixel(2, 0)) > 200 && qRed(parade.pixel(2, 0)) == 0);

    // 明るさの倍率で、濃くなる。
    const QImage bright = heiran::ui::renderWaveformImage(counts, heiran::ui::WaveformMode::Rgb, 8.0);
    const QImage dim = heiran::ui::renderWaveformImage(counts, heiran::ui::WaveformMode::Rgb, 0.001);
    QVERIFY(qRed(bright.pixel(0, 0)) > qRed(dim.pixel(0, 0)));
    QVERIFY(heiran::ui::renderWaveformImage(WaveformCounts{}, heiran::ui::WaveformMode::Luma, 1.0).isNull());
  }

  // ベクトルスコープの絵: 正方形。無彩色の点は、中心の升目で、無彩色。何も数えていなければ、空。
  void rendersTheVectorscopeImage() {
    VectorscopeCounts counts;
    counts.allocate(256);
    const std::vector<float> gray = {0.5F, 0.5F, 0.5F, 0.5F};
    accumulateVectorscopeRows(gray.data(), gray.data(), gray.data(), 4, 0, 1, kWr, kWg, kWb, &counts);
    const QImage image = heiran::ui::renderVectorscopeImage(
        counts, {.red = kWr, .green = kWg, .blue = kWb}, 1.0);
    QCOMPARE(image.size(), QSize(256, 256));
    const QRgb center = image.pixel(128, 127);
    QVERIFY(qGray(center) > 40);
    QVERIFY(std::abs(qRed(center) - qBlue(center)) < 40);  // 色は、ほぼ無彩色
    QCOMPARE(image.pixel(10, 10), qRgb(0, 0, 0));
    VectorscopeCounts empty;
    empty.allocate(256);
    QVERIFY(heiran::ui::renderVectorscopeImage(empty, {.red = kWr, .green = kWg, .blue = kWb}, 1.0).isNull());
  }
};

QTEST_APPLESS_MAIN(ScopesTests)
#include "ScopesTests.moc"
