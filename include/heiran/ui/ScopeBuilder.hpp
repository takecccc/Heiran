#pragma once

#include "heiran/domain/ColorSpace.hpp"
#include "heiran/domain/Scopes.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QImage>
#include <memory>

namespace heiran::ui {

// 波形モニタとベクトルスコープの、解析入力からの集計と、絵への描き起こし。
// 集計は、解析用バッファの値から (表示の露出、ガンマ、チャネル表示には影響されない)。選択範囲があれば、その範囲だけ。

using WaveformPtr = std::shared_ptr<const heiran::domain::WaveformCounts>;
using VectorscopePtr = std::shared_ptr<const heiran::domain::VectorscopeCounts>;

// 縦の升目の数 (値の分解能)。横は、画像の幅へ合わせる (上限 `kWaveformMaxColumns`)。
inline constexpr int kWaveformLevels = 256;
inline constexpr int kWaveformMaxColumns = 1024;
// ベクトルスコープの 1 辺の升目の数。
inline constexpr int kVectorscopeSize = 256;

// 集計できなければ (範囲が空など)、null。RGB の平面を 1 回の走査で作り、行を帯に分けて並列に数える。
WaveformPtr buildWaveform(const AnalysisImage &analysisImage);
VectorscopePtr buildVectorscope(const AnalysisImage &analysisImage);

enum class WaveformMode {
  Luma,      // 輝度 1 つ
  Rgb,       // R、G、B を、色を付けて、重ねる
  Parade,    // R、G、B を、横に並べる
};

// 升目の数を、濃さへ直して、絵にする。縦は、上が、値の最大。濃さは、列ごとの密度 (一様に散らばっていれば 1) で決めるため、
// 画像の大きさに依らない。`gain` は、濃さの倍率。Parade の絵は、幅が、3 つ分。
QImage renderWaveformImage(const heiran::domain::WaveformCounts &counts, WaveformMode mode,
                           double gain);

// 色差の升目を、絵にする (`kVectorscopeSize` 四方)。升目の色は、その升目の色差を、一定の輝度で RGB へ戻した色。
// 濃さは、対数で決める (まばらな升目も見える)。
QImage renderVectorscopeImage(const heiran::domain::VectorscopeCounts &counts,
                              const heiran::domain::LumaWeights &weights, double gain);

}  // namespace heiran::ui
