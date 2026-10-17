#include "heiran/infra/AvifDecoder.hpp"

#include <QFile>
#include <QThread>
#include <QTransform>
#include <algorithm>
#include <avif/avif.h>
#include <memory>

namespace heiran::infra {

namespace {

struct DecoderDeleter {
  void operator()(avifDecoder *decoder) const { avifDecoderDestroy(decoder); }
};

AvifResult failure(const QString &message) {
  AvifResult result;
  result.errorMessage = message;
  return result;
}

// 向きの指定を適用する。irot は反時計回りの 90 度の回数、imir は反転の軸
// (0: 上下を入れ替える、1: 左右を入れ替える)。仕様の順は、回転、反転。
QImage applyTransforms(QImage image, const avifImage &avif) {
  if ((avif.transformFlags & AVIF_TRANSFORM_IROT) != 0) {
    const int quarterTurnsCounterClockwise = avif.irot.angle & 3;
    if (quarterTurnsCounterClockwise != 0) {
      QTransform transform;
      // Qt の rotate は時計回り。
      transform.rotate(-90.0 * quarterTurnsCounterClockwise);
      image = image.transformed(transform, Qt::FastTransformation);
    }
  }
  if ((avif.transformFlags & AVIF_TRANSFORM_IMIR) != 0) {
    const bool flipVertically = (avif.imir.axis & 1) == 0;
    image = image.mirrored(!flipVertically, flipVertically);
  }
  return image;
}

}  // namespace

QColorSpace colorSpaceFromCicp(int colorPrimaries, int transferCharacteristics) {
  QColorSpace::Primaries primaries = QColorSpace::Primaries::SRgb;
  switch (colorPrimaries) {
    case 1:  // BT.709 / sRGB
    case 5:  // BT.601 (625)
    case 6:  // BT.601 (525)
      primaries = QColorSpace::Primaries::SRgb;
      break;
    case 9:  // BT.2020
      primaries = QColorSpace::Primaries::Bt2020;
      break;
    case 12:  // Display P3 (D65 の P3)
      primaries = QColorSpace::Primaries::DciP3D65;
      break;
    default:
      return {};
  }

  QColorSpace::TransferFunction transfer = QColorSpace::TransferFunction::SRgb;
  switch (transferCharacteristics) {
    case 1:   // BT.709
    case 6:   // BT.601
    case 13:  // sRGB
      transfer = QColorSpace::TransferFunction::SRgb;
      break;
    case 8:  // 線形
      transfer = QColorSpace::TransferFunction::Linear;
      break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    case 14:  // BT.2020 (10bit)
    case 15:  // BT.2020 (12bit)
      transfer = QColorSpace::TransferFunction::Bt2020;
      break;
    case 16:  // PQ
      transfer = QColorSpace::TransferFunction::St2084;
      break;
    case 18:  // HLG
      transfer = QColorSpace::TransferFunction::Hlg;
      break;
#endif
    default:
      return {};
  }
  return QColorSpace(primaries, transfer);
}

AvifResult decodeAvif(const QString &filePath) {
  // ファイルは全部読んでから渡す。libavif のファイル入力は、狭い文字のパスを取るため、
  // Windows で日本語などのパスが開けない。
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return failure(QStringLiteral("Failed to open the file"));
  }
  const QByteArray bytes = file.readAll();
  if (bytes.isEmpty()) {
    return failure(QStringLiteral("The file is empty"));
  }

  std::unique_ptr<avifDecoder, DecoderDeleter> decoder(avifDecoderCreate());
  if (decoder == nullptr) {
    return failure(QStringLiteral("Failed to create the AVIF decoder"));
  }
  decoder->maxThreads = std::clamp(QThread::idealThreadCount(), 1, 16);
  // dav1d を使う。入っていなければ、libavif が選べるものへ任せる。
  decoder->codecChoice = AVIF_CODEC_CHOICE_AUTO;

  avifResult status =
      avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const uint8_t *>(bytes.constData()),
                             static_cast<size_t>(bytes.size()));
  if (status != AVIF_RESULT_OK) {
    return failure(QString::fromUtf8(avifResultToString(status)));
  }
  status = avifDecoderParse(decoder.get());
  if (status != AVIF_RESULT_OK) {
    return failure(QString::fromUtf8(avifResultToString(status)));
  }
  status = avifDecoderNextImage(decoder.get());
  if (status != AVIF_RESULT_OK) {
    return failure(QString::fromUtf8(avifResultToString(status)));
  }

  const avifImage *avif = decoder->image;
  const bool highPrecision = avif->depth > 8;
  const bool hasAlpha = avif->alphaPlane != nullptr;

  QImage image(static_cast<int>(avif->width), static_cast<int>(avif->height),
               highPrecision ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
  if (image.isNull()) {
    return failure(QStringLiteral("Failed to allocate the image"));
  }

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, avif);
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  rgb.depth = highPrecision ? 16 : 8;
  rgb.alphaPremultiplied = AVIF_FALSE;
  rgb.pixels = image.bits();
  rgb.rowBytes = static_cast<uint32_t>(image.bytesPerLine());
  rgb.maxThreads = decoder->maxThreads;
  status = avifImageYUVToRGB(avif, &rgb);
  if (status != AVIF_RESULT_OK) {
    return failure(QString::fromUtf8(avifResultToString(status)));
  }

  // アルファが無ければ、不透明の画像として持つ (アルファのある画像と見分けが付く)。
  if (!hasAlpha) {
    image = image.convertToFormat(highPrecision ? QImage::Format_RGBX64 : QImage::Format_RGBX8888);
  }
  image = applyTransforms(std::move(image), *avif);

  AvifResult result;
  result.ok = true;
  result.image = std::move(image);
  result.bitDepth = static_cast<int>(avif->depth);
  result.hasAlpha = hasAlpha;
  result.imageCount = decoder->imageCount;
  if (avif->icc.size > 0) {
    result.iccProfile = QByteArray(reinterpret_cast<const char *>(avif->icc.data),
                                   static_cast<qsizetype>(avif->icc.size));
  } else {
    result.declaredColorSpace = colorSpaceFromCicp(static_cast<int>(avif->colorPrimaries),
                                                   static_cast<int>(avif->transferCharacteristics));
  }
  return result;
}

}  // namespace heiran::infra
