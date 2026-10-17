#include "heiran/domain/ColorSpace.hpp"

#include <cmath>

namespace heiran::domain {

namespace {

// 色度座標。
struct Chromaticity {
  double x = 0.0;
  double y = 0.0;
};

struct PrimarySet {
  Chromaticity red;
  Chromaticity green;
  Chromaticity blue;
  Chromaticity white;
};

// D65。ここで扱う原色はすべてこの白色点を使うため、色順応の変換は要らない。
constexpr Chromaticity kD65{.x = 0.3127, .y = 0.3290};

PrimarySet primarySetFor(ColorPrimaries primaries) {
  switch (primaries) {
    case ColorPrimaries::DisplayP3:
      return {
          .red = {0.680, 0.320}, .green = {0.265, 0.690}, .blue = {0.150, 0.060}, .white = kD65};
    case ColorPrimaries::Rec2020:
      return {
          .red = {0.708, 0.292}, .green = {0.170, 0.797}, .blue = {0.131, 0.046}, .white = kD65};
    case ColorPrimaries::AdobeRgb:
      return {
          .red = {0.640, 0.330}, .green = {0.210, 0.710}, .blue = {0.150, 0.060}, .white = kD65};
    case ColorPrimaries::SRgb:
    case ColorPrimaries::Auto:
    default:
      // sRGB と Rec.709 は同じ原色を使う。
      return {
          .red = {0.640, 0.330}, .green = {0.300, 0.600}, .blue = {0.150, 0.060}, .white = kD65};
  }
}

// 色度座標を Y = 1 の XYZ へ写す。
std::array<double, 3> toXyz(const Chromaticity &chromaticity) {
  if (chromaticity.y == 0.0) {
    return {0.0, 0.0, 0.0};
  }
  return {chromaticity.x / chromaticity.y, 1.0,
          (1.0 - chromaticity.x - chromaticity.y) / chromaticity.y};
}

Matrix3 inverseOf(const Matrix3 &matrix) {
  const double a = matrix[0];
  const double b = matrix[1];
  const double c = matrix[2];
  const double d = matrix[3];
  const double e = matrix[4];
  const double f = matrix[5];
  const double g = matrix[6];
  const double h = matrix[7];
  const double i = matrix[8];

  const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (determinant == 0.0) {
    return {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  }

  const double scale = 1.0 / determinant;
  return {
      (e * i - f * h) * scale, (c * h - b * i) * scale, (b * f - c * e) * scale,
      (f * g - d * i) * scale, (a * i - c * g) * scale, (c * d - a * f) * scale,
      (d * h - e * g) * scale, (b * g - a * h) * scale, (a * e - b * d) * scale,
  };
}

std::array<double, 3> applyMatrix(const Matrix3 &matrix, const std::array<double, 3> &vector) {
  return {
      matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
      matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
      matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2],
  };
}

// 各原色の寄与の大きさを求める。この 3 つがそのまま輝度の重みになる。
//
// 原色を Y = 1 に正規化した XYZ を並べた行列に、白色点の XYZ を通すと、
// 「白を作るために各原色をどれだけ足すか」が出る。Y 行は 1 が並んでいるため、
// その係数の並びが輝度の重みそのものになる。
std::array<double, 3> primaryScales(const PrimarySet &set) {
  const auto red = toXyz(set.red);
  const auto green = toXyz(set.green);
  const auto blue = toXyz(set.blue);
  const Matrix3 byPrimary = {
      red[0], green[0], blue[0],  //
      red[1], green[1], blue[1],  //
      red[2], green[2], blue[2],
  };
  return applyMatrix(inverseOf(byPrimary), toXyz(set.white));
}

// 符号を保ったまま曲線を当てる。浮動小数画像は負の値を持ちうる。
double withSign(double value, double (*curve)(double)) {
  return value < 0.0 ? -curve(-value) : curve(value);
}

double srgbDecode(double value) {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double srgbEncode(double value) {
  return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double rec709Decode(double value) {
  return value < 0.081 ? value / 4.5 : std::pow((value + 0.099) / 1.099, 1.0 / 0.45);
}

double rec709Encode(double value) {
  return value < 0.018 ? value * 4.5 : 1.099 * std::pow(value, 0.45) - 0.099;
}

}  // namespace

LumaWeights lumaWeights(ColorPrimaries primaries) {
  const auto scales = primaryScales(primarySetFor(primaries));
  return {.red = scales[0], .green = scales[1], .blue = scales[2]};
}

Matrix3 rgbToXyzMatrix(ColorPrimaries primaries) {
  const PrimarySet set = primarySetFor(primaries);
  const auto scales = primaryScales(set);
  const auto red = toXyz(set.red);
  const auto green = toXyz(set.green);
  const auto blue = toXyz(set.blue);

  return {
      scales[0] * red[0], scales[1] * green[0], scales[2] * blue[0],  //
      scales[0] * red[1], scales[1] * green[1], scales[2] * blue[1],  //
      scales[0] * red[2], scales[1] * green[2], scales[2] * blue[2],
  };
}

Matrix3 xyzToRgbMatrix(ColorPrimaries primaries) {
  return inverseOf(rgbToXyzMatrix(primaries));
}

Matrix3 primariesConversionMatrix(ColorPrimaries from, ColorPrimaries to) {
  return multiply(xyzToRgbMatrix(to), rgbToXyzMatrix(from));
}

Matrix3 multiply(const Matrix3 &left, const Matrix3 &right) {
  Matrix3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      double sum = 0.0;
      for (int index = 0; index < 3; ++index) {
        sum += left[row * 3 + index] * right[index * 3 + column];
      }
      result[row * 3 + column] = sum;
    }
  }
  return result;
}

double decodeToLinear(double encodedValue, TransferFunction transfer) {
  switch (transfer) {
    case TransferFunction::Linear:
      return encodedValue;
    case TransferFunction::Rec709:
      return withSign(encodedValue, rec709Decode);
    case TransferFunction::Gamma22:
      return withSign(encodedValue, [](double value) { return std::pow(value, 2.2); });
    case TransferFunction::Gamma24:
      return withSign(encodedValue, [](double value) { return std::pow(value, 2.4); });
    case TransferFunction::SRgb:
    case TransferFunction::Auto:
    default:
      return withSign(encodedValue, srgbDecode);
  }
}

double encodeFromLinear(double linearValue, TransferFunction transfer) {
  switch (transfer) {
    case TransferFunction::Linear:
      return linearValue;
    case TransferFunction::Rec709:
      return withSign(linearValue, rec709Encode);
    case TransferFunction::Gamma22:
      return withSign(linearValue, [](double value) { return std::pow(value, 1.0 / 2.2); });
    case TransferFunction::Gamma24:
      return withSign(linearValue, [](double value) { return std::pow(value, 1.0 / 2.4); });
    case TransferFunction::SRgb:
    case TransferFunction::Auto:
    default:
      return withSign(linearValue, srgbEncode);
  }
}

QString toColorPrimariesString(ColorPrimaries primaries) {
  switch (primaries) {
    case ColorPrimaries::SRgb:
      return "srgb";
    case ColorPrimaries::DisplayP3:
      return "display-p3";
    case ColorPrimaries::Rec2020:
      return "rec2020";
    case ColorPrimaries::AdobeRgb:
      return "adobe-rgb";
    case ColorPrimaries::Auto:
    default:
      return "auto";
  }
}

ColorPrimaries colorPrimariesFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == "srgb" || normalized == "rec709" || normalized == "bt709") {
    return ColorPrimaries::SRgb;
  }
  if (normalized == "display-p3" || normalized == "displayp3" || normalized == "p3") {
    return ColorPrimaries::DisplayP3;
  }
  if (normalized == "rec2020" || normalized == "bt2020") {
    return ColorPrimaries::Rec2020;
  }
  if (normalized == "adobe-rgb" || normalized == "adobergb") {
    return ColorPrimaries::AdobeRgb;
  }
  return ColorPrimaries::Auto;
}

QString toTransferFunctionString(TransferFunction transfer) {
  switch (transfer) {
    case TransferFunction::SRgb:
      return "srgb";
    case TransferFunction::Rec709:
      return "rec709";
    case TransferFunction::Linear:
      return "linear";
    case TransferFunction::Gamma22:
      return "gamma2.2";
    case TransferFunction::Gamma24:
      return "gamma2.4";
    case TransferFunction::Auto:
    default:
      return "auto";
  }
}

TransferFunction transferFunctionFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == "srgb") {
    return TransferFunction::SRgb;
  }
  if (normalized == "rec709" || normalized == "bt709") {
    return TransferFunction::Rec709;
  }
  if (normalized == "linear") {
    return TransferFunction::Linear;
  }
  if (normalized == "gamma2.2" || normalized == "gamma22") {
    return TransferFunction::Gamma22;
  }
  if (normalized == "gamma2.4" || normalized == "gamma24") {
    return TransferFunction::Gamma24;
  }
  return TransferFunction::Auto;
}

}  // namespace heiran::domain
