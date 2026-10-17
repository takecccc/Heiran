#pragma once

#include <QString>
#include <array>

namespace heiran::domain {

// 原色。色域の3頂点と白色点を決める。
// 輝度の重みと、原色間の変換行列はここから導出する。
enum class ColorPrimaries {
  // 復号結果から推測する。
  Auto,
  // sRGB と Rec.709 は原色も白色点も同じ。伝達関数だけが違う。
  SRgb,
  DisplayP3,
  Rec2020,
  AdobeRgb,
};

// 伝達関数 (符号化)。蓄積されている値と光量の対応を決める。
enum class TransferFunction {
  // 復号結果から推測する。
  Auto,
  SRgb,
  Rec709,
  Linear,
  Gamma22,
  Gamma24,
};

// 画像 1 つ分の色空間。
struct ColorSpace {
  ColorPrimaries primaries = ColorPrimaries::Auto;
  TransferFunction transfer = TransferFunction::Auto;

  bool operator==(const ColorSpace &other) const = default;
};

// 行優先の 3x3 行列。原色間の変換に使う。
using Matrix3 = std::array<double, 9>;

// 輝度の重み。RGB->XYZ 行列の Y 行にあたる。合計は 1.0 になる。
struct LumaWeights {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

// 原色から輝度の重みを求める。
//
// 重みは原色と白色点だけで決まる。RGB->XYZ 行列の Y 行がそのまま重みになるため、
// 定数表を持たずに色度から導出する。`Auto` は sRGB として扱う。
LumaWeights lumaWeights(ColorPrimaries primaries);

// 原色から RGB->XYZ 行列を求める。値は線形光を前提とする。
Matrix3 rgbToXyzMatrix(ColorPrimaries primaries);

Matrix3 xyzToRgbMatrix(ColorPrimaries primaries);

// ある原色の線形 RGB を別の原色の線形 RGB へ写す行列。
// 白色点はいずれも D65 のため、順応の変換は挟まない。
Matrix3 primariesConversionMatrix(ColorPrimaries from, ColorPrimaries to);

Matrix3 multiply(const Matrix3 &left, const Matrix3 &right);

// 符号化された値を線形光へ戻す。
//
// 負の値と 1.0 を超える値も扱う。浮動小数画像は範囲外の値を持ちうるため、
// 負側は符号を保ったまま同じ曲線を当てる。`Auto` は sRGB として扱う。
double decodeToLinear(double encodedValue, TransferFunction transfer);

// 線形光を符号化する。`decodeToLinear()` の逆。
double encodeFromLinear(double linearValue, TransferFunction transfer);

QString toColorPrimariesString(ColorPrimaries primaries);
ColorPrimaries colorPrimariesFromString(const QString &text);

QString toTransferFunctionString(TransferFunction transfer);
TransferFunction transferFunctionFromString(const QString &text);

}  // namespace heiran::domain
