#pragma once

#include <QString>

namespace heiran::domain {

// 差分の求め方と見せ方。差分のセルと、Diff ウィンドウが共有する。
enum class DiffMode {
  Absolute,  // |target - base|
  Signed,    // target - base (正は赤、負は青)
  Binary,    // 差が閾値を超えたかどうか
  DeltaE,    // 色差 (CIEDE2000)。輝度ではなく、RGB 全体の色の違い。閾値は ΔE の単位
  Ssim,      // 構造的類似度 (SSIM)。輝度の局所の構造の違い。見せるのは 1 - SSIM。閾値は SSIM の値 (これを下回った画素を数える)
  Wipe,      // 差ではなく、2 枚を境目で切って、つなぎ合わせる (ワイプ)。境目は、`wipePosition`
};

enum class DiffViewMode {
  Grayscale,
  SignedColor,
  Overlay,  // 基準の画像に重ねる
};

// 大きさの違う 2 枚を、どう比べるか。
enum class DiffSizeMatch {
  Overlap,   // 合わせない。左上をそろえて、重なる部分だけを比べる
  ToBase,    // 比べる側を、基準の大きさへ合わせる
  ToTarget,  // 基準を、比べる側の大きさへ合わせる
  ToLarger,  // 面積の小さいほうを、大きいほうの大きさへ合わせる。細部を捨てない
};

// ワイプの切り方。基準は、左 / 上の側に出る。
enum class WipeOrientation {
  LeftRight,  // 縦の境目。左が基準、右が比べる側
  TopBottom,  // 横の境目。上が基準、下が比べる側
};

struct DiffSettings {
  DiffMode mode = DiffMode::Absolute;
  DiffViewMode view = DiffViewMode::Grayscale;
  // 差を何倍して見せるか。
  double gain = 1.0;
  // Binary と、数値指標の「超えた画素」の閾値。輝度の 0-255 の尺度。
  double threshold = 8.0;
  // 差の最大が 255 になるよう、見せる倍率を自動で合わせる。
  bool autoRange = true;
  // 大きさが違うときの扱い。同じ大きさなら効かない。
  DiffSizeMatch sizeMatch = DiffSizeMatch::Overlap;
  // ワイプ (`mode` が Wipe のとき) の切り方と、境目の位置 (画像の幅 / 高さに対する 0.0-1.0)。
  WipeOrientation wipeOrientation = WipeOrientation::LeftRight;
  double wipePosition = 0.5;
  // 専用の画像差分画面で使う。
  bool ignoreFullyTransparentRgb = false;

  bool operator==(const DiffSettings &) const = default;
};

QString toDiffModeString(DiffMode mode);
DiffMode diffModeFromString(const QString &text);
QString toDiffViewModeString(DiffViewMode view);
DiffViewMode diffViewModeFromString(const QString &text);
QString toWipeOrientationString(WipeOrientation orientation);
WipeOrientation wipeOrientationFromString(const QString &text);
QString toDiffSizeMatchString(DiffSizeMatch match);
DiffSizeMatch diffSizeMatchFromString(const QString &text);

}  // namespace heiran::domain
