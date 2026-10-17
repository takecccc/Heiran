#pragma once

#include <cstddef>
#include <cstdint>

namespace heiran::domain {

// 構造的類似度 (SSIM、Wang ら 2004)。2 枚の輝度の平面から、画素ごとの類似度 (1 が同一) を求める。
//
// 窓は、標準の 11 x 11 のガウス窓 (σ = 1.5)。値の幅 (`dynamicRange`) は、8bit の輝度の 255。
// 窓が画像の外へ出るところは、端の画素を延ばして補う (そのため、端から 5 画素の内側だけが、窓がすべて画像の中に入った値になる)。
inline constexpr int kSsimWindow = 11;
inline constexpr int kSsimRadius = kSsimWindow / 2;

// `firstRow` から `lastRow` (含まない) の行について、類似度を `output` へ書く (`output` は、画像全体の大きさ。他の行には触れない)。
// 1 つの帯を、1 スレッドが受け持つ。行を小さな塊に分けて処理するため、作業用の領域は、幅に比例した大きさで済む。
void computeSsimRows(const float *base, const float *target, int width, int height, int firstRow,
                     int lastRow, double dynamicRange, float *output);

struct SsimMetrics {
  std::int64_t pixelCount = 0;
  double mean = 1.0;
  double minimum = 1.0;
  // 類似度が閾値を下回った画素の数。
  std::int64_t belowThresholdCount = 0;
};

// 画像全体の類似度から、指標を求める。窓がすべて画像の中に入る内側 (端から 5 画素を除いた範囲) だけを数える。
// 内側が無いほど小さな画像 (どちらかの辺が 11 画素未満) では、全体を数える。
SsimMetrics computeSsimMetrics(const float *ssim, int width, int height, double threshold);

}  // namespace heiran::domain
