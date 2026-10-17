#pragma once

#include "heiran/domain/ColorSpace.hpp"
#include "heiran/domain/ImageTransform.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/ColorManagement.hpp"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QVector>
#include <optional>

namespace heiran::ui {

// 解析入力の取得元。表示用画像ではなく解析用バッファから値を読む。
// 座標は表示座標で受け取り、回転と反転を戻して元画像座標へ写してから読む。
struct AnalysisSampler {
  heiran::infra::DecodedImage decodedImage;
  heiran::domain::ImageOrientation orientation;
  QSize sourceSize;
  // 輝度の重み。元画像の色空間から導く。既定は sRGB の値で、
  // `heiran::domain::lumaWeights(ColorPrimaries::SRgb)` と一致する。
  heiran::domain::LumaWeights lumaWeights{.red = 0.2126, .green = 0.7152, .blue = 0.0722};
  // 元の線形 RGB から XYZ への行列。色差で使う。空なら sRGB。
  std::optional<heiran::domain::Matrix3> rgbToXyz;
  // 値をどの空間で返すか。既定はファイルに入っているまま。
  AnalysisValueSpace valueSpace = AnalysisValueSpace::Source;
  // 線形光へ戻すための曲線。`valueSpace` が Linear のときだけ使う。
  ShaderTransfer sourceTransfer = ShaderTransfer::SRgb;
  float sourceGamma = 1.0F;

  // 1 つの値を `valueSpace` へ写す。
  float toValueSpace(float sourceValue) const;

  bool isValid() const { return sourceSize.isValid(); }

  // 正規化値を返す。整数型は 0.0-1.0、浮動小数型は 1.0 を超えうる。
  float sampleRgbaAtDisplayPixel(int x, int y, int rgbaChannel) const;

  // 0-255 スケールの輝度を返す。
  double lumaAtDisplayPixel(int x, int y) const;

  // 表示座標の矩形について 0-255 スケールの輝度を並べて返す。
  // 全画素を走査する解析では、画素ごとに `lumaAtDisplayPixel()` を呼ばずにこちらを使う。
  // 型と色モデルの判定を走査の外へ出せるため、画素あたりの処理が桁違いに減る。
  QVector<float> buildLumaPlane(const QRect &displayRegion) const;

  // 表示座標の矩形について、指定した RGBA チャネルを 0.0-1.0 で並べて返す。
  // ヒストグラムのようにチャネルごとに集計する処理で使う。
  QVector<float> buildChannelPlane(const QRect &displayRegion, int rgbaChannel) const;

  // `buildChannelPlane()` の 3 チャネル分 (R、G、B) を、画像を 1 回だけ走査して作る。
  // 色差のように、3 チャネルを同時に要る処理で使う。作れないときは false。
  bool buildRgbPlanes(const QRect &displayRegion, QVector<float> (&planes)[3]) const;

  // 色モデルが不明な場合は表示用画像の性質で補う。
  bool isGrayscale(const QImage &fallback) const;
  bool hasAlpha(const QImage &fallback) const;
};

}  // namespace heiran::ui
