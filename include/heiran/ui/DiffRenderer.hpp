#pragma once

#include "heiran/domain/DiffMetrics.hpp"
#include "heiran/domain/DiffSettings.hpp"
#include "heiran/ui/AnalysisSampler.hpp"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace heiran::ui {

// 差分の入力。表示用の画像 (重ね合わせの下地) と、輝度を取る解析用の読み出し。
// `origin` は、切り出した場合の、解析用バッファでの左上 (切り出さなければ 0, 0)。
struct DiffInput {
  QImage image;
  AnalysisSampler sampler;
  QPoint origin;
};

struct DiffOutput {
  bool ok = false;
  // 失敗の理由 (大きさが違う、など)。
  QString error;
  // 差分の絵。ok のときだけ。
  QImage image;
  heiran::domain::DiffMetrics metrics;
};

// 輝度の平面 (幅 x 高さ、行の順) を、別の大きさへ変える。縮める軸は、元の画素の重なりで
// 平均し (エイリアシングを避ける)、広げる軸は、線形に補間する。水平、垂直の順に分けて行う。
QVector<float> resizeLumaPlane(const QVector<float> &plane, QSize from, QSize to);

// 差の場: 画素ごとの、見せる値の元 (輝度の差、ΔE、1 -
// SSIM)。**計算が重いのは、場を作るところ**で、色や明るさの倍率、見せ方、
// 閾値を替えても、場は同じ。場を取っておけば、これらの変更では、場を作り直さずに、絵と指標だけを求め直せる。
enum class DiffFieldKind {
  Luma,    // 輝度の差 (比べる側 - 基準)。absolute、signed、binary が共有する
  DeltaE,  // 色差 (CIEDE2000)
  Ssim,    // 1 - SSIM
};

struct DiffInspection {
  QSize baseSize;
  QSize targetSize;
  heiran::domain::DiffComparisonBasis comparisonBasis =
      heiran::domain::DiffComparisonBasis::PixelValues;
  QImage compositedBase;
  bool ignoreFullyTransparentRgb = false;
  std::vector<unsigned char> alphaChanged;
  std::vector<unsigned char> bothTransparent;
};

struct DiffField {
  DiffFieldKind kind = DiffFieldKind::Luma;
  QSize size;
  heiran::domain::DiffSizeMatch sizeMatch = heiran::domain::DiffSizeMatch::Overlap;
  std::vector<float> values;
  std::optional<DiffInspection> inspection;
};

using DiffFieldPtr = std::shared_ptr<const DiffField>;

QVector<QRect> findDiffRegions(const DiffField &field, double threshold);
std::array<QImage, 2> prepareDiffBlinkFrames(const QImage &base, const QImage &target,
                                             heiran::domain::DiffSizeMatch sizeMatch,
                                             bool includeOutsideOverlap = false);
std::array<QImage, 2> prepareDiffCompositeFrames(const DiffInput &base, const DiffInput &target,
                                                 heiran::domain::DiffSizeMatch sizeMatch,
                                                 heiran::domain::DiffComparisonBasis basis);

DiffFieldKind diffFieldKindOf(heiran::domain::DiffMode mode);

// この場で、`settings` の絵を作れるか
// (計算の種類と、大きさの合わせ方が同じか)。ワイプは、場を使わない。
bool diffFieldServes(const DiffField &field, const heiran::domain::DiffSettings &settings);

// 入力から、場を作る。作れなければ null (理由は `error`)。ワイプは、場を持たないため、呼ばない。
DiffFieldPtr computeDiffField(const DiffInput &base, const DiffInput &target,
                              const heiran::domain::DiffSettings &settings, QString *error,
                              bool inspectCoverageAndAlpha = false);

// 場から、絵と指標を作る (色の付け方、倍率、閾値は、`settings` に従う)。`base`
// は、重ね合わせの下地。
DiffOutput renderDiffFromField(const DiffField &field, const DiffInput &base,
                               const heiran::domain::DiffSettings &settings);

// 2 枚から、差分の絵と数値指標を作る (差は target - base の輝度)。
DiffOutput renderDiff(const DiffInput &base, const DiffInput &target,
                      const heiran::domain::DiffSettings &settings);

}  // namespace heiran::ui
