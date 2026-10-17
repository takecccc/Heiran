#pragma once

#include "heiran/domain/ColorSpace.hpp"

#include <QColorSpace>
#include <QString>
#include <QVector>
#include <array>
#include <memory>

namespace heiran::ui {

// シェーダで扱える曲線の種類。これ以外の曲線を持つ色空間は、復号時に CPU で
// 変換してから渡す。
enum class ShaderTransfer {
  Linear = 0,
  SRgb = 1,
  Gamma = 2,
};

// 解析の値をどの空間で出すか。
//
// 画素値表示、ルーペ、ヒストグラム、差分、変化グラフのすべてに効く。
enum class AnalysisValueSpace {
  // ファイルに入っている符号化された値。既定。
  Source,
  // 線形光へ戻した値。露光量として意味のある比較をしたい場合に使う。
  Linear,
};

QString analysisValueSpaceToString(AnalysisValueSpace space);
AnalysisValueSpace analysisValueSpaceFromString(const QString &text);

// 表示のための色変換。シェーダの uniform へそのまま渡せる形にする。
//
// 元の値 -> 線形光 -> (原色の変換) -> 表示用の符号化 の順で当てる。
struct ColorTransformParams {
  ShaderTransfer sourceTransfer = ShaderTransfer::SRgb;
  float sourceGamma = 1.0F;
  ShaderTransfer displayTransfer = ShaderTransfer::SRgb;
  float displayGamma = 1.0F;
  // 線形光のまま原色を移す 3x3。行優先で持つ。
  std::array<float, 9> primaryMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  // 原色の変換が要らない場合は真。シェーダ側で行列を飛ばせる。
  bool primariesMatch = true;
};

// シェーダで扱える色空間かどうか。
//
// 3 成分の行列で表せて、曲線が線形 / sRGB / 指数のいずれかである必要がある。
// LUT を持つ ICC プロファイルや PQ / HLG はここで偽になる。
bool isShaderRepresentable(const QColorSpace &colorSpace);

// 曲線を当てる。シェーダ側の実装と同じ式にする。
//
// 表示は GPU と CPU の 2 経路があり、どちらも同じ結果にならないと、ルーペや
// 差分表示だけ色が違うことになる。式をここに 1 つ置いて、GLSL 側はこれを写す。
//
// 負の値は符号を保ったまま同じ曲線を当てる。広い色域から狭い色域へ移すと
// 負の値が出るため、そこで形が崩れないようにする。
float decodeShaderTransfer(float value, ShaderTransfer kind, float gamma);
float encodeShaderTransfer(float value, ShaderTransfer kind, float gamma);

// 色域の外へ出る色をどう扱うか。ICC が定める 4 つ。
//
// 対応表を組むときにだけ効く。行列と曲線で表せる組み合わせでは、どの指定でも
// 同じ結果になる (色域の写し方を持つのは表を持つプロファイルだけ)。
enum class RenderingIntent {
  // 色域内の色をそのまま保ち、外の色は境界へ寄せる。写真以外の比較に向く。
  RelativeColorimetric,
  // 色域全体を縮めて収める。階調のつながりを保つ。写真の閲覧に向く。
  Perceptual,
  // 彩度を優先する。図やグラフ向け。
  Saturation,
  // 白色点の順応も行わない。校正に使う。
  AbsoluteColorimetric,
};

QString renderingIntentToString(RenderingIntent intent);
RenderingIntent renderingIntentFromString(const QString &text);

// 対応表を組むときの指定。
struct ColorLutOptions {
  RenderingIntent intent = RenderingIntent::RelativeColorimetric;
  // 黒点補正。元の黒が表示先の黒へ来るように伸縮する。暗部が浮くのを防ぐ。
  bool blackPointCompensation = false;
  // 表の刻み。
  int size = 33;

  bool operator==(const ColorLutOptions &other) const = default;
};

// レンダリングインテントを指定できるかどうか。
// Little-CMS を組み込んでいない構成では偽になり、指定は無視される。
bool supportsRenderingIntents();

// 3 次元の対応表。元の値から表示用の「線形光」へ写す。
//
// 行列と曲線では表せない色空間 (LUT を持つ ICC プロファイル、PQ、HLG) は、
// 変換をこの表へ焼いてから当てる。表は GPU では 3D テクスチャ、CPU では同じ
// 三重線形補間で引く。両方が同じ表と同じ補間を使うため、結果がそろう。
//
// 出力を線形光にしてあるのは、露出を線形で当てられるようにするため。表示用の
// 符号化は表を引いた後に当てる。
struct ColorLut {
  int size = 0;
  // size^3 個の RGB。並びは赤が最も速く変わる。
  QVector<float> values;

  bool isValid() const {
    return size >= 2 && values.size() == static_cast<qsizetype>(size) * size * size * 3;
  }

  // 0.0-1.0 の入力を三重線形補間で引く。範囲外は端で止める。
  std::array<float, 3> sample(float red, float green, float blue) const;
};

// 変換を表へ焼く。組み立てには `QColorTransform` を使うため、Qt が扱える
// プロファイルはすべて通せる。
//
// 表の刻みは 33 を既定とする。一般的な 3D LUT の形式と同じ刻みで、なめらかな
// プロファイルならこれで足りる。刻みを増やすと精度は上がるが、組み立ての時間が
// 刻みの 3 乗で効く。
std::shared_ptr<const ColorLut> buildColorLut(const QColorSpace &source, const QColorSpace &display,
                                              const ColorLutOptions &options = {});

// `.hrn` の指定から色空間を組み立てる。指定が無ければ無効を返す。
//
// `iccProfilePath` があればそちらを優先する。ファイルの経路は `.hrn` からの
// 相対で書けるため、基準にするディレクトリを渡す。
// 名前は `srgb`, `srgb-linear`, `display-p3`, `adobe-rgb`, `rec2020`,
// `prophoto-rgb`, `bt2100-pq`, `bt2100-hlg` を受ける。
QColorSpace colorSpaceFromSessionSpec(const QString &colorSpaceName, const QString &iccProfilePath,
                                      const QString &baseDirectory);

// 色空間から輝度の重みを求める。
//
// 重みは原色で決まる。色度を取り出して自分で組むのが素直だが、
// `QColorSpace::primaryPoints()` は項目がずれて返るため使えない。線形にした
// 色空間から sRGB へ写す行列を求め、sRGB の重みを通して求める。輝度は
// 測色的な変換で保たれるため、これで元の色空間の重みが出る。
heiran::domain::LumaWeights lumaWeightsFor(const QColorSpace &colorSpace);

// 元の色空間の線形 RGB から XYZ (D65) への行列。色差の計算に使う。無効な色空間は sRGB。
heiran::domain::Matrix3 rgbToXyzFor(const QColorSpace &colorSpace);

// 元の色空間から表示用の色空間への変換を組み立てる。
//
// 原色の行列は `QColorTransform` に基底ベクトルを通して求める。
// `QColorSpace::primaryPoints()` は項目がずれて返るため使わない
// (Qt 6.11.1 で確認。docs/設計判断.md を参照)。
ColorTransformParams makeColorTransformParams(const QColorSpace &source,
                                              const QColorSpace &display);

}  // namespace heiran::ui
