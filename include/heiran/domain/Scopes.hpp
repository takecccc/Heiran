#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace heiran::domain {

// 波形モニタとベクトルスコープの集計。どちらも、画素を、2 次元の升目へ数える (画像の面ではなく、信号の見え方を見るための道具)。
// 平面は、0.0-1.0 へ正規化した値 (範囲外は、端の升目へ収める)。行ごとに独立しているため、帯に分けて並列に数え、`merge` で合わせる。

// 波形モニタ: 横が、画像の列 (位置)、縦が、値。升目の数は、その列、その値の画素の数。
// 4 つの平面 (R、G、B、輝度) を、同時に数える (表示の切り替えで、数え直さなくて済む)。
struct WaveformCounts {
  int columns = 0;       // 横の升目の数 (画像の幅より多くしない)
  int levels = 0;        // 縦の升目の数 (値の分解能)
  int sourceWidth = 0;   // 数えた範囲の幅 (画素)
  int sourceHeight = 0;  // 数えた範囲の高さ (画素)
  // 行優先。`levels` 行 x `columns` 列。行 0 が、値の最小。
  std::vector<std::uint32_t> red;
  std::vector<std::uint32_t> green;
  std::vector<std::uint32_t> blue;
  std::vector<std::uint32_t> luma;

  void allocate(int columnCount, int levelCount, int width, int height);
  void merge(const WaveformCounts &other);
  // 1 つの列に入る画素の数 (その列へ割り当てた画像の列の数 x 高さ)。濃さを、列ごとの密度へ直すのに使う。
  double samplesPerColumn() const {
    return columns > 0 ? static_cast<double>(sourceWidth) / columns * sourceHeight : 0.0;
  }
};

// `firstRow` から `lastRow` (含まない) の行を、`counts` へ足す。`counts` は、`allocate` 済み。
// 輝度は、`weightRed` などの重みで、R、G、B から求める (値の空間は、呼び出し側が、平面へ適用済み)。
void accumulateWaveformRows(const float *red, const float *green, const float *blue, int width,
                            int firstRow, int lastRow, double weightRed, double weightGreen,
                            double weightBlue, WaveformCounts *counts);

// ベクトルスコープ: 色差 (Cb が横、Cr が縦。上が +Cr) の平面へ、画素を数える。
// Cb、Cr は、輝度の重みから導く (Cb = (B - Y) / (2 (1 - wB))、Cr = (R - Y) / (2 (1 - wR))。範囲は、-0.5 から 0.5)。
struct VectorscopeCounts {
  int size = 0;          // 1 辺の升目の数
  std::int64_t total = 0;  // 数えた画素の数
  // 行優先。行 0 が、+Cr の端 (画面の上)。
  std::vector<std::uint32_t> counts;

  void allocate(int gridSize);
  void merge(const VectorscopeCounts &other);
};

void accumulateVectorscopeRows(const float *red, const float *green, const float *blue, int width,
                               int firstRow, int lastRow, double weightRed, double weightGreen,
                               double weightBlue, VectorscopeCounts *counts);

// 輝度の重みから、ある RGB の色差 (Cb、Cr、-0.5 から 0.5) を求める。目盛り (色の目標) の位置に使う。
struct ChromaPoint {
  double cb = 0.0;
  double cr = 0.0;
};
ChromaPoint chromaOf(double red, double green, double blue, double weightRed, double weightGreen,
                     double weightBlue);

}  // namespace heiran::domain
