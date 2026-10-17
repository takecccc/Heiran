#pragma once

#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/ColorManagement.hpp"

#include <QImage>

namespace heiran::ui {

// 表示用画像を作るときに適用する変換。
// 解析には影響させない。解析は常に解析用バッファの値を使う。
struct DisplayTransformParams {
  ChannelMode channelMode = ChannelMode::Color;
  double gamma = 1.0;
  // 表示の露出。2^exposureStops を掛けてから 0.0-1.0 へ丸める。
  double exposureStops = 0.0;
  bool invert = false;
  // 元の色空間から表示用の色空間への変換。`colorManaged` が偽なら当てない。
  //
  // 露出は色管理が有効なときだけ線形光で当てる。stops の意味からは線形が正しいが、
  // 色空間が分からない状態で当てても根拠が無いため、無効な間は従来どおり
  // 符号化された値へ掛ける。
  // 輝度表示の重み。元画像の色空間から導く。既定は sRGB。
  heiran::domain::LumaWeights lumaWeights{.red = 0.2126, .green = 0.7152, .blue = 0.0722};
  bool colorManaged = false;
  ColorTransformParams colorTransform;
  // 行列と曲線で表せない色空間のときに使う対応表。
  // 入っていればこちらを優先する。表の出力は表示用の線形光。
  std::shared_ptr<const ColorLut> colorLut;
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;

  // 何も変換しない場合は元の画像をそのまま使える。
  bool isIdentity() const;
};

// CPU で表示用画像を作る。
// 露出を既定以外にした場合、解析用バッファがあればクリップ前の値から作り直す。
// 表示用画像は 0.0-1.0 でクリップ済みで、そこからでは 1.0 超を復元できないため。
QImage renderDisplayImageOnCpu(const heiran::infra::DecodedImage &decodedImage,
                               const DisplayTransformParams &params);

}  // namespace heiran::ui
