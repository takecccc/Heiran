#include "heiran/ui/ColorManagement.hpp"

#include "heiran/ui/DisplayProfile.hpp"

#if defined(HEIRAN_HAVE_LCMS2)
#include <lcms2.h>
#endif

#include <QColorTransform>
#include <QDir>
#include <QRgbaFloat>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

struct ResolvedTransfer {
  ShaderTransfer kind = ShaderTransfer::SRgb;
  float gamma = 1.0F;
};

ResolvedTransfer resolveTransfer(const QColorSpace &colorSpace) {
  switch (colorSpace.transferFunction()) {
    case QColorSpace::TransferFunction::Linear:
      return {.kind = ShaderTransfer::Linear, .gamma = 1.0F};
    case QColorSpace::TransferFunction::Gamma:
      return {.kind = ShaderTransfer::Gamma, .gamma = colorSpace.gamma()};
    case QColorSpace::TransferFunction::SRgb:
    default:
      return {.kind = ShaderTransfer::SRgb, .gamma = 1.0F};
  }
}

// 原色の変換行列を求める。
//
// 色空間から色度を取り出して自分で組むのが素直だが、`primaryPoints()` の戻りは
// 項目がずれて返るため使えない。線形にした色空間どうしの変換へ基底ベクトルを
// 通せば、結果の列がそのまま行列になる。Qt の内部表現がどうであれ正しく出る。
#if defined(HEIRAN_HAVE_LCMS2)
cmsUInt32Number lcmsIntentFor(RenderingIntent intent) {
  switch (intent) {
    case RenderingIntent::Perceptual:
      return INTENT_PERCEPTUAL;
    case RenderingIntent::Saturation:
      return INTENT_SATURATION;
    case RenderingIntent::AbsoluteColorimetric:
      return INTENT_ABSOLUTE_COLORIMETRIC;
    case RenderingIntent::RelativeColorimetric:
    default:
      return INTENT_RELATIVE_COLORIMETRIC;
  }
}

// Little-CMS で表を焼く。プロファイルを開けない場合は false を返し、
// 呼び出し側は Qt の変換へ退避する。
bool fillLutWithLcms(const QColorSpace &source, const QColorSpace &displayLinear,
                     const ColorLutOptions &options, int size, QVector<float> *values) {
  const QByteArray sourceIcc = source.iccProfile();
  const QByteArray displayIcc = displayLinear.iccProfile();
  if (sourceIcc.isEmpty() || displayIcc.isEmpty()) {
    return false;
  }

  cmsHPROFILE sourceProfile =
      cmsOpenProfileFromMem(sourceIcc.constData(), static_cast<cmsUInt32Number>(sourceIcc.size()));
  cmsHPROFILE displayProfile = cmsOpenProfileFromMem(
      displayIcc.constData(), static_cast<cmsUInt32Number>(displayIcc.size()));
  if (sourceProfile == nullptr || displayProfile == nullptr) {
    if (sourceProfile != nullptr) {
      cmsCloseProfile(sourceProfile);
    }
    if (displayProfile != nullptr) {
      cmsCloseProfile(displayProfile);
    }
    return false;
  }

  const cmsUInt32Number flags =
      options.blackPointCompensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0;
  cmsHTRANSFORM transform = cmsCreateTransform(sourceProfile, TYPE_RGB_FLT, displayProfile,
                                               TYPE_RGB_FLT, lcmsIntentFor(options.intent), flags);
  cmsCloseProfile(sourceProfile);
  cmsCloseProfile(displayProfile);
  if (transform == nullptr) {
    return false;
  }

  const float lastIndex = static_cast<float>(size - 1);
  QVector<float> input(static_cast<qsizetype>(size) * size * size * 3, 0.0F);
  qsizetype writeIndex = 0;
  for (int b = 0; b < size; ++b) {
    for (int g = 0; g < size; ++g) {
      for (int r = 0; r < size; ++r) {
        input[writeIndex++] = static_cast<float>(r) / lastIndex;
        input[writeIndex++] = static_cast<float>(g) / lastIndex;
        input[writeIndex++] = static_cast<float>(b) / lastIndex;
      }
    }
  }

  values->resize(input.size());
  cmsDoTransform(transform, input.constData(), values->data(),
                 static_cast<cmsUInt32Number>(size) * size * size);
  cmsDeleteTransform(transform);
  return true;
}
#endif

std::array<float, 9> derivePrimaryMatrix(const QColorSpace &source, const QColorSpace &display) {
  const QColorSpace sourceLinear =
      source.withTransferFunction(QColorSpace::TransferFunction::Linear);
  const QColorSpace displayLinear =
      display.withTransferFunction(QColorSpace::TransferFunction::Linear);

  const QColorTransform transform = sourceLinear.transformationToColorSpace(displayLinear);
  if (!transform.isIdentity()) {
    std::array<float, 9> matrix{};
    for (int column = 0; column < 3; ++column) {
      QRgbaFloat32 basis{};
      basis.r = column == 0 ? 1.0F : 0.0F;
      basis.g = column == 1 ? 1.0F : 0.0F;
      basis.b = column == 2 ? 1.0F : 0.0F;
      basis.a = 1.0F;

      const QRgbaFloat32 mapped = transform.map(basis);
      matrix[column] = mapped.r;
      matrix[3 + column] = mapped.g;
      matrix[6 + column] = mapped.b;
    }
    return matrix;
  }

  return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

bool isIdentityMatrix(const std::array<float, 9> &matrix) {
  const std::array<float, 9> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  for (int index = 0; index < 9; ++index) {
    if (std::abs(matrix[index] - identity[index]) > 1e-4F) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::array<float, 3> ColorLut::sample(float red, float green, float blue) const {
  if (!isValid()) {
    return {red, green, blue};
  }

  const float lastIndex = static_cast<float>(size - 1);
  const float coordinates[3] = {std::clamp(red, 0.0F, 1.0F) * lastIndex,
                                std::clamp(green, 0.0F, 1.0F) * lastIndex,
                                std::clamp(blue, 0.0F, 1.0F) * lastIndex};

  int lower[3] = {};
  float fraction[3] = {};
  for (int axis = 0; axis < 3; ++axis) {
    const float floored = std::floor(coordinates[axis]);
    lower[axis] = std::clamp(static_cast<int>(floored), 0, size - 2);
    fraction[axis] = coordinates[axis] - static_cast<float>(lower[axis]);
  }

  const auto valueAt = [this](int r, int g, int b, int channel) {
    const qsizetype index = ((static_cast<qsizetype>(b) * size + g) * size + r) * 3 + channel;
    return values.at(index);
  };

  std::array<float, 3> result{};
  for (int channel = 0; channel < 3; ++channel) {
    float accumulated = 0.0F;
    for (int corner = 0; corner < 8; ++corner) {
      const int offsetR = corner & 1;
      const int offsetG = (corner >> 1) & 1;
      const int offsetB = (corner >> 2) & 1;
      const float weight = (offsetR != 0 ? fraction[0] : 1.0F - fraction[0]) *
                           (offsetG != 0 ? fraction[1] : 1.0F - fraction[1]) *
                           (offsetB != 0 ? fraction[2] : 1.0F - fraction[2]);
      accumulated +=
          weight * valueAt(lower[0] + offsetR, lower[1] + offsetG, lower[2] + offsetB, channel);
    }
    result[channel] = accumulated;
  }
  return result;
}

std::shared_ptr<const ColorLut> buildColorLut(const QColorSpace &source, const QColorSpace &display,
                                              const ColorLutOptions &options) {
  const int size = options.size;
  if (!source.isValid() || !display.isValid() || size < 2) {
    return {};
  }

  // 出力は表示用の線形光。露出を線形で当てられるようにするため、符号化は
  // 表の外で当てる。
  const QColorSpace displayLinear =
      display.withTransferFunction(QColorSpace::TransferFunction::Linear);

  auto lut = std::make_shared<ColorLut>();
  lut->size = size;

#if defined(HEIRAN_HAVE_LCMS2)
  // インテントと黒点補正を効かせられるのはこちらだけ。Qt の変換には指定が無い。
  if (fillLutWithLcms(source, displayLinear, options, size, &lut->values)) {
    return lut;
  }
#endif

  const QColorTransform transform = source.transformationToColorSpace(displayLinear);
  lut->values.resize(static_cast<qsizetype>(size) * size * size * 3);

  const float lastIndex = static_cast<float>(size - 1);
  qsizetype writeIndex = 0;
  for (int b = 0; b < size; ++b) {
    for (int g = 0; g < size; ++g) {
      for (int r = 0; r < size; ++r) {
        QRgbaFloat32 input{};
        input.r = static_cast<float>(r) / lastIndex;
        input.g = static_cast<float>(g) / lastIndex;
        input.b = static_cast<float>(b) / lastIndex;
        input.a = 1.0F;

        const QRgbaFloat32 mapped = transform.map(input);
        lut->values[writeIndex++] = mapped.r;
        lut->values[writeIndex++] = mapped.g;
        lut->values[writeIndex++] = mapped.b;
      }
    }
  }
  return lut;
}

float decodeShaderTransfer(float value, ShaderTransfer kind, float gamma) {
  const float sign = value < 0.0F ? -1.0F : 1.0F;
  const float magnitude = std::abs(value);
  switch (kind) {
    case ShaderTransfer::Linear:
      return value;
    case ShaderTransfer::Gamma:
      return sign * std::pow(magnitude, gamma > 0.0F ? gamma : 1.0F);
    case ShaderTransfer::SRgb:
    default:
      return sign * (magnitude <= 0.04045F ? magnitude / 12.92F
                                           : std::pow((magnitude + 0.055F) / 1.055F, 2.4F));
  }
}

float encodeShaderTransfer(float value, ShaderTransfer kind, float gamma) {
  const float sign = value < 0.0F ? -1.0F : 1.0F;
  const float magnitude = std::abs(value);
  switch (kind) {
    case ShaderTransfer::Linear:
      return value;
    case ShaderTransfer::Gamma:
      return sign * std::pow(magnitude, gamma > 0.0F ? 1.0F / gamma : 1.0F);
    case ShaderTransfer::SRgb:
    default:
      return sign * (magnitude <= 0.0031308F ? magnitude * 12.92F
                                             : 1.055F * std::pow(magnitude, 1.0F / 2.4F) - 0.055F);
  }
}

bool supportsRenderingIntents() {
#if defined(HEIRAN_HAVE_LCMS2)
  return true;
#else
  return false;
#endif
}

QString renderingIntentToString(RenderingIntent intent) {
  switch (intent) {
    case RenderingIntent::Perceptual:
      return "perceptual";
    case RenderingIntent::Saturation:
      return "saturation";
    case RenderingIntent::AbsoluteColorimetric:
      return "absolute-colorimetric";
    case RenderingIntent::RelativeColorimetric:
    default:
      return "relative-colorimetric";
  }
}

RenderingIntent renderingIntentFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == "perceptual") {
    return RenderingIntent::Perceptual;
  }
  if (normalized == "saturation") {
    return RenderingIntent::Saturation;
  }
  if (normalized == "absolute-colorimetric" || normalized == "absolute") {
    return RenderingIntent::AbsoluteColorimetric;
  }
  return RenderingIntent::RelativeColorimetric;
}

QString analysisValueSpaceToString(AnalysisValueSpace space) {
  return space == AnalysisValueSpace::Linear ? "linear" : "source";
}

AnalysisValueSpace analysisValueSpaceFromString(const QString &text) {
  return text.trimmed().toLower() == "linear" ? AnalysisValueSpace::Linear
                                              : AnalysisValueSpace::Source;
}

QColorSpace colorSpaceFromSessionSpec(const QString &colorSpaceName, const QString &iccProfilePath,
                                      const QString &baseDirectory) {
  if (!iccProfilePath.isEmpty()) {
    const QString resolvedPath = QDir::isAbsolutePath(iccProfilePath)
                                     ? iccProfilePath
                                     : QDir(baseDirectory).filePath(iccProfilePath);
    const QColorSpace fromFile = colorSpaceFromIccFile(resolvedPath);
    if (fromFile.isValid()) {
      return fromFile;
    }
    // 読めない経路でも、名前の指定があればそちらを試す。
  }

  const QString normalized = colorSpaceName.trimmed().toLower();
  if (normalized.isEmpty() || normalized == "auto") {
    return {};
  }
  if (normalized == "srgb") {
    return QColorSpace(QColorSpace::SRgb);
  }
  if (normalized == "srgb-linear" || normalized == "linear") {
    return QColorSpace(QColorSpace::SRgbLinear);
  }
  if (normalized == "display-p3" || normalized == "displayp3" || normalized == "p3") {
    return QColorSpace(QColorSpace::DisplayP3);
  }
  if (normalized == "adobe-rgb" || normalized == "adobergb") {
    return QColorSpace(QColorSpace::AdobeRgb);
  }
  if (normalized == "rec2020" || normalized == "bt2020") {
    return QColorSpace(QColorSpace::Bt2020);
  }
  if (normalized == "prophoto-rgb" || normalized == "prophotorgb") {
    return QColorSpace(QColorSpace::ProPhotoRgb);
  }
  if (normalized == "bt2100-pq" || normalized == "pq") {
    return QColorSpace(QColorSpace::Bt2100Pq);
  }
  if (normalized == "bt2100-hlg" || normalized == "hlg") {
    return QColorSpace(QColorSpace::Bt2100Hlg);
  }
  return {};
}

heiran::domain::LumaWeights lumaWeightsFor(const QColorSpace &colorSpace) {
  const auto srgbWeights = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
  if (!colorSpace.isValid()) {
    return srgbWeights;
  }

  // 元の原色の 3 つを sRGB の線形へ写し、それぞれの輝度を求める。
  const auto matrix = derivePrimaryMatrix(colorSpace, QColorSpace(QColorSpace::SRgb));
  const auto weightOf = [&matrix, &srgbWeights](int column) {
    return srgbWeights.red * matrix[column] + srgbWeights.green * matrix[3 + column] +
           srgbWeights.blue * matrix[6 + column];
  };
  return {.red = weightOf(0), .green = weightOf(1), .blue = weightOf(2)};
}

heiran::domain::Matrix3 rgbToXyzFor(const QColorSpace &colorSpace) {
  const auto srgbToXyz = heiran::domain::rgbToXyzMatrix(heiran::domain::ColorPrimaries::SRgb);
  if (!colorSpace.isValid()) {
    return srgbToXyz;
  }
  // 元の原色の線形を sRGB の線形へ写してから、sRGB の行列で XYZ へ。
  const auto toSrgb = derivePrimaryMatrix(colorSpace, QColorSpace(QColorSpace::SRgb));
  heiran::domain::Matrix3 conversion{};
  for (int index = 0; index < 9; ++index) {
    conversion[index] = static_cast<double>(toSrgb[static_cast<size_t>(index)]);
  }
  return heiran::domain::multiply(srgbToXyz, conversion);
}

bool isShaderRepresentable(const QColorSpace &colorSpace) {
  if (!colorSpace.isValid()) {
    return false;
  }
  if (colorSpace.transformModel() != QColorSpace::TransformModel::ThreeComponentMatrix) {
    return false;
  }
  if (colorSpace.colorModel() != QColorSpace::ColorModel::Rgb &&
      colorSpace.colorModel() != QColorSpace::ColorModel::Gray) {
    return false;
  }

  switch (colorSpace.transferFunction()) {
    case QColorSpace::TransferFunction::Linear:
    case QColorSpace::TransferFunction::SRgb:
      return true;
    case QColorSpace::TransferFunction::Gamma:
      return colorSpace.gamma() > 0.0F;
    default:
      // PQ / HLG / ProPhoto の曲線や、表で持つ曲線はシェーダに載せない。
      return false;
  }
}

ColorTransformParams makeColorTransformParams(const QColorSpace &source,
                                              const QColorSpace &display) {
  ColorTransformParams params;
  if (!source.isValid() || !display.isValid()) {
    return params;
  }

  const ResolvedTransfer sourceTransfer = resolveTransfer(source);
  const ResolvedTransfer displayTransfer = resolveTransfer(display);
  params.sourceTransfer = sourceTransfer.kind;
  params.sourceGamma = sourceTransfer.gamma;
  params.displayTransfer = displayTransfer.kind;
  params.displayGamma = displayTransfer.gamma;

  params.primaryMatrix = derivePrimaryMatrix(source, display);
  params.primariesMatch = isIdentityMatrix(params.primaryMatrix);
  return params;
}

}  // namespace heiran::ui
