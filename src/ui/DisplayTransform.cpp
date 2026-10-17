#include "heiran/ui/DisplayTransform.hpp"

#include "heiran/domain/ImageTransform.hpp"

#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>

namespace heiran::ui {

namespace {

int applyGammaToChannel(int value, double gamma) {
  if (gamma <= 0.0) {
    return value;
  }

  const double normalized = static_cast<double>(value) / 255.0;
  const double corrected = std::pow(normalized, 1.0 / gamma);
  return std::clamp(static_cast<int>(std::lround(corrected * 255.0)), 0, 255);
}

// 露出、ガンマ、白黒反転をこの順で適用する。
// 反転は表示される値に対して行うため、1.0 を超えて丸められた画素は黒になる。
std::array<uchar, 256> buildToneLut(double gamma, double gain, bool invert) {
  std::array<uchar, 256> lut{};
  for (int value = 0; value < 256; ++value) {
    const int gained =
        std::clamp(static_cast<int>(std::lround(static_cast<double>(value) * gain)), 0, 255);
    const int toned = applyGammaToChannel(gained, gamma);
    lut[static_cast<size_t>(value)] = static_cast<uchar>(invert ? 255 - toned : toned);
  }
  return lut;
}

// 色管理を当てた 1 チャネル分の値。0.0-1.0 の入出力。
// シェーダの main と同じ順序で当てる。
float applyManagedChannel(float value, const ColorTransformParams &transform, double gain) {
  const float linear = decodeShaderTransfer(value, transform.sourceTransfer, transform.sourceGamma);
  const float gained = linear * static_cast<float>(gain);
  return encodeShaderTransfer(gained, transform.displayTransfer, transform.displayGamma);
}

// 色管理つきの階調表。原色の変換が要らない場合に使える。
std::array<uchar, 256> buildManagedToneLut(const DisplayTransformParams &params, double gain) {
  std::array<uchar, 256> lut{};
  for (int value = 0; value < 256; ++value) {
    const float managed =
        applyManagedChannel(static_cast<float>(value) / 255.0F, params.colorTransform, gain);
    const int clamped = std::clamp(static_cast<int>(std::lround(managed * 255.0F)), 0, 255);
    const int toned = applyGammaToChannel(clamped, params.gamma);
    lut[static_cast<size_t>(value)] = static_cast<uchar>(params.invert ? 255 - toned : toned);
  }
  return lut;
}

// 対応表を使う場合。表はチャネルをまたぐため画素ごとに引く。
// GPU 側と同じ表を同じ三重線形補間で引くので、結果はそろう。
QRgb applyLutPixel(QRgb pixel, const DisplayTransformParams &params, double gain) {
  const auto &transform = params.colorTransform;
  const auto linear = params.colorLut->sample(static_cast<float>(qRed(pixel)) / 255.0F,
                                              static_cast<float>(qGreen(pixel)) / 255.0F,
                                              static_cast<float>(qBlue(pixel)) / 255.0F);

  int result[3] = {};
  for (int index = 0; index < 3; ++index) {
    const float encoded = encodeShaderTransfer(linear[index] * static_cast<float>(gain),
                                               transform.displayTransfer, transform.displayGamma);
    const int clamped = std::clamp(static_cast<int>(std::lround(encoded * 255.0F)), 0, 255);
    const int toned = applyGammaToChannel(clamped, params.gamma);
    result[index] = params.invert ? 255 - toned : toned;
  }
  return qRgba(result[0], result[1], result[2], qAlpha(pixel));
}

// 原色の変換が要る場合。表として持てないため画素ごとに当てる。
QRgb applyManagedPixel(QRgb pixel, const DisplayTransformParams &params, double gain) {
  const auto &transform = params.colorTransform;
  const float channels[3] = {static_cast<float>(qRed(pixel)) / 255.0F,
                             static_cast<float>(qGreen(pixel)) / 255.0F,
                             static_cast<float>(qBlue(pixel)) / 255.0F};

  float linear[3] = {};
  for (int index = 0; index < 3; ++index) {
    linear[index] =
        decodeShaderTransfer(channels[index], transform.sourceTransfer, transform.sourceGamma);
  }

  float moved[3] = {};
  for (int row = 0; row < 3; ++row) {
    moved[row] = transform.primaryMatrix[row * 3] * linear[0] +
                 transform.primaryMatrix[row * 3 + 1] * linear[1] +
                 transform.primaryMatrix[row * 3 + 2] * linear[2];
  }

  int result[3] = {};
  for (int index = 0; index < 3; ++index) {
    const float encoded = encodeShaderTransfer(moved[index] * static_cast<float>(gain),
                                               transform.displayTransfer, transform.displayGamma);
    const int clamped = std::clamp(static_cast<int>(std::lround(encoded * 255.0F)), 0, 255);
    const int toned = applyGammaToChannel(clamped, params.gamma);
    result[index] = params.invert ? 255 - toned : toned;
  }
  return qRgba(result[0], result[1], result[2], qAlpha(pixel));
}

QRgb channelPixel(QRgb sourcePixel, ChannelMode channelMode,
                  const heiran::domain::LumaWeights &lumaWeights) {
  switch (channelMode) {
    case ChannelMode::Color:
      return sourcePixel;
    case ChannelMode::Gray: {
      // シェーダと同じ式で求める。片方だけ違うと GPU と CPU で色が変わる。
      const double luma = lumaWeights.red * qRed(sourcePixel) +
                          lumaWeights.green * qGreen(sourcePixel) +
                          lumaWeights.blue * qBlue(sourcePixel);
      const int value = std::clamp(static_cast<int>(std::lround(luma)), 0, 255);
      return qRgba(value, value, value, 255);
    }
    case ChannelMode::Red: {
      const int value = qRed(sourcePixel);
      return qRgba(value, value, value, 255);
    }
    case ChannelMode::Green: {
      const int value = qGreen(sourcePixel);
      return qRgba(value, value, value, 255);
    }
    case ChannelMode::Blue: {
      const int value = qBlue(sourcePixel);
      return qRgba(value, value, value, 255);
    }
    case ChannelMode::Alpha: {
      const int value = qAlpha(sourcePixel);
      return qRgba(value, value, value, 255);
    }
  }

  return sourcePixel;
}

}  // namespace

bool DisplayTransformParams::isIdentity() const {
  const bool gammaIsIdentity = gamma <= 0.0 || std::abs(gamma - 1.0) < 0.0001;
  const bool exposureIsIdentity = std::abs(exposureStops) < 0.0001;
  const int normalizedQuarterTurns = heiran::domain::normalizeQuarterTurns(rotationQuarterTurns);
  // 色管理が要る場合は素通りできない。他が既定でも色は変わる。
  return !colorManaged && channelMode == ChannelMode::Color && gammaIsIdentity &&
         exposureIsIdentity && !invert && normalizedQuarterTurns == 0 && !flipX && !flipY;
}

QImage renderDisplayImageOnCpu(const heiran::infra::DecodedImage &decodedImage,
                               const DisplayTransformParams &params) {
  const QImage &sourceImage = decodedImage.displayImage;
  if (sourceImage.isNull()) {
    return {};
  }
  if (params.isIdentity()) {
    return sourceImage;
  }

  const double gain = std::pow(2.0, params.exposureStops);
  const bool exposureIsIdentity = std::abs(params.exposureStops) < 0.0001;
  // 解析用バッファへ先に露出を掛けると 1.0 超の階調を保てる。ただしそれは値を
  // そのまま倍にする形のため、線形光を入れている場合しか正しくない。符号化された
  // 値を持つ画像では、色管理の中で戻してから掛ける。
  const bool preExposeFromAnalysisBuffer =
      !exposureIsIdentity && decodedImage.hasAnalysisBuffer() &&
      (!params.colorManaged || params.colorTransform.sourceTransfer == ShaderTransfer::Linear);
  const QImage exposedImage =
      preExposeFromAnalysisBuffer
          ? heiran::infra::renderDisplayImage(decodedImage.analysisBuffer, static_cast<float>(gain))
          : QImage();
  const QImage &baseImage = exposedImage.isNull() ? sourceImage : exposedImage;

  const QImage sourceArgb = baseImage.convertToFormat(QImage::Format_ARGB32);
  QImage converted(sourceArgb.size(), QImage::Format_ARGB32);
  if (converted.isNull()) {
    return {};
  }

  // 解析用バッファを持たない画像は、丸め済みの値に対して LUT で露出を掛ける。
  const double lutGain = exposedImage.isNull() ? gain : 1.0;

  // チャネルを 1 つだけ出しているときは原色を移さない。値を並べ替えた偽色表示で
  // あって色ではないため、移すと意味の無い色が付く。シェーダ側と同じ判断。
  const bool applyPrimaries = params.colorManaged && !params.colorTransform.primariesMatch &&
                              params.channelMode == ChannelMode::Color;
  // 対応表もチャネルをまたぐため、色として出しているときだけ引く。
  const bool applyLut = params.colorManaged && params.colorLut && params.colorLut->isValid() &&
                        params.channelMode == ChannelMode::Color;
  const auto toneLut = params.colorManaged ? buildManagedToneLut(params, lutGain)
                                           : buildToneLut(params.gamma, lutGain, params.invert);

  for (int y = 0; y < sourceArgb.height(); ++y) {
    const auto *sourceLine = reinterpret_cast<const QRgb *>(sourceArgb.constScanLine(y));
    auto *destinationLine = reinterpret_cast<QRgb *>(converted.scanLine(y));
    for (int x = 0; x < sourceArgb.width(); ++x) {
      const QRgb transformedPixel =
          channelPixel(sourceLine[x], params.channelMode, params.lumaWeights);
      if (applyLut) {
        destinationLine[x] = applyLutPixel(transformedPixel, params, lutGain);
        continue;
      }
      if (applyPrimaries) {
        destinationLine[x] = applyManagedPixel(transformedPixel, params, lutGain);
        continue;
      }
      destinationLine[x] =
          qRgba(toneLut[static_cast<size_t>(qRed(transformedPixel))],
                toneLut[static_cast<size_t>(qGreen(transformedPixel))],
                toneLut[static_cast<size_t>(qBlue(transformedPixel))], qAlpha(transformedPixel));
    }
  }

  // 回転してから反転する。docs/テスト観点.md 座標変換 Case 9 の順序に合わせる。
  const int normalizedQuarterTurns =
      heiran::domain::normalizeQuarterTurns(params.rotationQuarterTurns);
  if (normalizedQuarterTurns != 0) {
    QTransform transform;
    transform.rotate(90.0 * normalizedQuarterTurns);
    converted = converted.transformed(transform);
  }
  if (params.flipX || params.flipY) {
    converted = converted.mirrored(params.flipX, params.flipY);
  }

  return converted;
}

}  // namespace heiran::ui
