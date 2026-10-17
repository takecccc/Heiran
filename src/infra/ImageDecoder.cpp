#include "heiran/infra/ImageDecoder.hpp"

#include "heiran/infra/AnimationComposer.hpp"
#include "heiran/infra/ApngDecoder.hpp"
#include "heiran/infra/DpxDecoder.hpp"
#include "heiran/infra/ExifOrientation.hpp"
#include "heiran/infra/HdrDecoder.hpp"
#include "heiran/infra/PsdDecoder.hpp"
#include "heiran/infra/RawImage.hpp"
#include "heiran/infra/TgaDecoder.hpp"
#if defined(HEIRAN_HAVE_ZSTD)
#include "heiran/infra/GifStream.hpp"
#endif
#include "heiran/infra/ParallelRows.hpp"

#if defined(HEIRAN_HAVE_LIBSPNG)
#include <spng.h>
#endif

#if defined(HEIRAN_HAVE_LIBWEBP)
#include "heiran/infra/WebpDecoder.hpp"
#endif

#if defined(HEIRAN_HAVE_LIBAVIF)
#include "heiran/infra/AvifDecoder.hpp"
#endif

#if defined(HEIRAN_HAVE_LIBJXL)
#include "heiran/infra/JxlDecoder.hpp"
#endif

#if defined(HEIRAN_HAVE_OPENEXR)
#include "heiran/infra/ExrDecoder.hpp"
#endif

#if defined(HEIRAN_HAVE_LIBTIFF)
#include "heiran/infra/TiffDecoder.hpp"
#endif

#if defined(HEIRAN_HAVE_LIBWEBP)
#endif


#include <QColor>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFloat16>
#include <QImageReader>
#include <QPointF>
#include <QScopeGuard>
#include <QSet>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include <optional>

namespace heiran::infra {

int bytesPerSample(PixelSampleType sampleType) {
  switch (sampleType) {
    case PixelSampleType::UInt8:
      return 1;
    case PixelSampleType::UInt16:
    case PixelSampleType::Float16:
      return 2;
    case PixelSampleType::Float32:
      return 4;
    case PixelSampleType::Unknown:
    default:
      return 0;
  }
}

int channelCountForColorModel(PixelColorModel colorModel) {
  switch (colorModel) {
    case PixelColorModel::Gray:
      return 1;
    case PixelColorModel::GrayAlpha:
      return 2;
    case PixelColorModel::RGB:
      return 3;
    case PixelColorModel::RGBA:
      return 4;
    case PixelColorModel::Unknown:
    default:
      return 0;
  }
}

float PixelBuffer::sample(int x, int y, int channel) const {
  if (!isValid() || !contains(x, y) || channel < 0 || channel >= channelCount) {
    return 0.0F;
  }

  const int sampleBytes = bytesPerSample(sampleType);
  if (sampleBytes == 0) {
    return 0.0F;
  }

  const uchar *row = data() + static_cast<qsizetype>(y) * bytesPerRow;
  const qsizetype offset = (static_cast<qsizetype>(x) * channelCount + channel) * sampleBytes;
  const uchar *samplePtr = row + offset;

  switch (sampleType) {
    case PixelSampleType::UInt8:
      return static_cast<float>(*samplePtr) / 255.0F;
    case PixelSampleType::UInt16: {
      quint16 value = 0;
      std::memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value) / 65535.0F;
    }
    case PixelSampleType::Float16: {
      qfloat16 value{};
      std::memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value);
    }
    case PixelSampleType::Float32: {
      float value = 0.0F;
      std::memcpy(&value, samplePtr, sizeof(value));
      return value;
    }
    case PixelSampleType::Unknown:
    default:
      return 0.0F;
  }
}

float PixelBuffer::sampleRgba(int x, int y, int rgbaChannel) const {
  if (rgbaChannel < 0 || rgbaChannel > 3) {
    return 0.0F;
  }

  switch (colorModel) {
    case PixelColorModel::Gray:
      return rgbaChannel == 3 ? 1.0F : sample(x, y, 0);
    case PixelColorModel::GrayAlpha:
      return rgbaChannel == 3 ? sample(x, y, 1) : sample(x, y, 0);
    case PixelColorModel::RGB:
      return rgbaChannel == 3 ? 1.0F : sample(x, y, rgbaChannel);
    case PixelColorModel::RGBA:
      return sample(x, y, rgbaChannel);
    case PixelColorModel::Unknown:
    default:
      return rgbaChannel == 3 ? 1.0F : sample(x, y, rgbaChannel);
  }
}

float DecodedImage::sampleRgba(int x, int y, int rgbaChannel) const {
  if (analysisBuffer.isValid()) {
    return analysisBuffer.sampleRgba(x, y, rgbaChannel);
  }

  if (displayImage.isNull() || x < 0 || y < 0 || x >= displayImage.width() ||
      y >= displayImage.height() || rgbaChannel < 0 || rgbaChannel > 3) {
    return 0.0F;
  }

  const QColor pixel = displayImage.pixelColor(x, y);
  switch (rgbaChannel) {
    case 0:
      return static_cast<float>(pixel.redF());
    case 1:
      return static_cast<float>(pixel.greenF());
    case 2:
      return static_cast<float>(pixel.blueF());
    default:
      return static_cast<float>(pixel.alphaF());
  }
}

namespace {

bool decoderTraceEnabled() {
  static const bool enabled = qEnvironmentVariableIntValue("HEIRAN_TRACE_DECODER") != 0;
  return enabled;
}

PixelColorModel pixelColorModelForChannelCount(int channelCount) {
  switch (channelCount) {
    case 1:
      return PixelColorModel::Gray;
    case 2:
      return PixelColorModel::GrayAlpha;
    case 3:
      return PixelColorModel::RGB;
    case 4:
      return PixelColorModel::RGBA;
    default:
      return PixelColorModel::Unknown;
  }
}

// RGBA 各出力チャネルの取得元。精度と色モデルの判定を画素ごとに行わないよう、
// 取得元の決定を走査の前に済ませる。
struct RgbaChannelSource {
  int sourceChannel = -1;  // 負なら constantValue を使う
  float constantValue = 1.0F;
};

std::array<RgbaChannelSource, 4> rgbaChannelSources(PixelColorModel colorModel) {
  switch (colorModel) {
    case PixelColorModel::Gray:
      return {{{0}, {0}, {0}, {-1, 1.0F}}};
    case PixelColorModel::GrayAlpha:
      return {{{0}, {0}, {0}, {1}}};
    case PixelColorModel::RGBA:
      return {{{0}, {1}, {2}, {3}}};
    case PixelColorModel::RGB:
    case PixelColorModel::Unknown:
    default:
      return {{{0}, {1}, {2}, {-1, 1.0F}}};
  }
}

struct ReadUInt8 {
  static float read(const uchar *sample) { return static_cast<float>(*sample) / 255.0F; }
};

struct ReadUInt16 {
  static float read(const uchar *sample) {
    quint16 value = 0;
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value) / 65535.0F;
  }
};

struct ReadFloat16 {
  static float read(const uchar *sample) {
    qfloat16 value{};
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value);
  }
};

struct ReadFloat32 {
  static float read(const uchar *sample) {
    float value = 0.0F;
    std::memcpy(&value, sample, sizeof(value));
    return value;
  }
};

// 取得元のバイト位置と定数を先に確定し、内側の走査から色モデルの分岐を除く。
struct RgbaFillPlan {
  qsizetype sourceOffset[4] = {-1, -1, -1, -1};
  uchar constantByte[4] = {0, 0, 0, 255};
};

RgbaFillPlan makeFillPlan(const PixelBuffer &buffer, int sampleBytes) {
  const auto sources = rgbaChannelSources(buffer.colorModel);
  RgbaFillPlan plan;
  for (int channel = 0; channel < 4; ++channel) {
    const auto &source = sources[static_cast<size_t>(channel)];
    if (source.sourceChannel >= 0 && source.sourceChannel < buffer.channelCount) {
      plan.sourceOffset[channel] = static_cast<qsizetype>(source.sourceChannel) * sampleBytes;
    } else {
      plan.constantByte[channel] =
          static_cast<uchar>(std::lround(std::clamp(source.constantValue, 0.0F, 1.0F) * 255.0F));
    }
  }
  return plan;
}

// 画素ごとに 4 つの出力チャネルを 8bit にして、表示用の画像へ詰める。
// `toByte(標本の位置, 出力のチャネル)` が、1 つの標本を 0-255 へ写す。
//
// 行ごとに独立しているため、帯に分けて並列に走らせる。4K の画像は 800 万画素 (3,300 万標本) で、
// 逐次に 1 標本ずつ変換すると 800 ms 近くかかっていた。
template <typename ToByte>
void fillDisplayImageWith(const PixelBuffer &buffer, QImage &image, ToByte toByte) {
  const int sampleBytes = bytesPerSample(buffer.sampleType);
  const qsizetype pixelStride = static_cast<qsizetype>(buffer.channelCount) * sampleBytes;
  const RgbaFillPlan plan = makeFillPlan(buffer, sampleBytes);
  // 行の先頭は、並列に入る前に確定させる (非 const の scanLine() は detach を通る)。
  uchar *destinationBits = image.bits();
  const qsizetype destinationStride = image.bytesPerLine();

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      uchar *destinationRow = destinationBits + static_cast<qsizetype>(y) * destinationStride;
      for (int x = 0; x < buffer.width; ++x) {
        const uchar *sourcePixel = sourceRow + static_cast<qsizetype>(x) * pixelStride;
        uchar *destinationPixel = destinationRow + static_cast<qsizetype>(x) * 4;
        for (int channel = 0; channel < 4; ++channel) {
          destinationPixel[channel] =
              plan.sourceOffset[channel] < 0
                  ? plan.constantByte[channel]
                  : toByte(sourcePixel + plan.sourceOffset[channel], channel);
        }
      }
    }
  });
}

// 浮動小数の値を、0-1 へ収めて 8bit へ。NaN は 0 になる (比較が偽になるため)。
inline uchar floatToByte(float value) {
  const float clamped = value > 0.0F ? (value < 1.0F ? value : 1.0F) : 0.0F;
  return static_cast<uchar>(clamped * 255.0F + 0.5F);
}

template <typename Reader>
void fillDisplayImage(const PixelBuffer &buffer, QImage &image, float gain) {
  fillDisplayImageWith(buffer, image, [gain](const uchar *sample, int channel) {
    const float raw = Reader::read(sample);
    // アルファは露出の対象にしない。
    return floatToByte(channel < 3 ? raw * gain : raw);
  });
}

// 16bit の標本 (整数または half) を 8bit へ写す表。65536 通りを先に引いておけば、画素ごとの
// 変換 (half の展開、掛け算、丸め) が、表を 1 回引くだけで済む。
template <typename Reader> std::vector<uchar> makeByteTable16(float gain) {
  std::vector<uchar> table(65536);
  for (int bits = 0; bits < 65536; ++bits) {
    const quint16 pattern = static_cast<quint16>(bits);
    uchar bytes[sizeof(quint16)];
    std::memcpy(bytes, &pattern, sizeof(pattern));
    const float value = Reader::read(bytes) * gain;
    const float clamped = value > 0.0F ? (value < 1.0F ? value : 1.0F) : 0.0F;
    table[static_cast<size_t>(bits)] = static_cast<uchar>(std::lround(clamped * 255.0F));
  }
  return table;
}

template <typename Reader>
void fillDisplayImage16(const PixelBuffer &buffer, QImage &image, float gain) {
  const std::vector<uchar> colorTable = makeByteTable16<Reader>(gain);
  // アルファは露出の対象にしない。露出が無ければ、同じ表でよい。
  const std::vector<uchar> alphaTable =
      gain == 1.0F ? std::vector<uchar>() : makeByteTable16<Reader>(1.0F);
  const uchar *color = colorTable.data();
  const uchar *alpha = alphaTable.empty() ? color : alphaTable.data();
  fillDisplayImageWith(buffer, image, [color, alpha](const uchar *sample, int channel) {
    quint16 bits = 0;
    std::memcpy(&bits, sample, sizeof(bits));
    return channel < 3 ? color[bits] : alpha[bits];
  });
}

// GPU テクスチャ用の詰め替え計画。丸めないため取得元は float のまま持つ。
struct RgbaPackPlan {
  qsizetype sourceOffset[4] = {-1, -1, -1, -1};
  float constantValue[4] = {0.0F, 0.0F, 0.0F, 1.0F};
};

RgbaPackPlan makePackPlan(const PixelBuffer &buffer, int sampleBytes) {
  const auto sources = rgbaChannelSources(buffer.colorModel);
  RgbaPackPlan plan;
  for (int channel = 0; channel < 4; ++channel) {
    const auto &source = sources[static_cast<size_t>(channel)];
    if (source.sourceChannel >= 0 && source.sourceChannel < buffer.channelCount) {
      plan.sourceOffset[channel] = static_cast<qsizetype>(source.sourceChannel) * sampleBytes;
    } else {
      plan.constantValue[channel] = source.constantValue;
    }
  }
  return plan;
}

// 行ごとに独立しているため帯へ分けて並列に走らせる。輝度やチャネルの面と同じ形。
// 4K の 16bit 画像では 1 枚あたり 800 万画素を詰め直すことになり、逐次では
// ページ送りのたびに 100 ms 近くかかる。
template <typename Reader> void packRgba(const PixelBuffer &buffer, qfloat16 *destination) {
  const int sampleBytes = bytesPerSample(buffer.sampleType);
  const qsizetype pixelStride = static_cast<qsizetype>(buffer.channelCount) * sampleBytes;
  const RgbaPackPlan plan = makePackPlan(buffer, sampleBytes);

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      qfloat16 *destinationRow =
          destination + static_cast<qsizetype>(y) * static_cast<qsizetype>(buffer.width) * 4;
      for (int x = 0; x < buffer.width; ++x) {
        const uchar *sourcePixel = sourceRow + static_cast<qsizetype>(x) * pixelStride;
        qfloat16 *destinationPixel = destinationRow + static_cast<qsizetype>(x) * 4;
        for (int channel = 0; channel < 4; ++channel) {
          const float value = plan.sourceOffset[channel] < 0
                                  ? plan.constantValue[channel]
                                  : Reader::read(sourcePixel + plan.sourceOffset[channel]);
          destinationPixel[channel] = static_cast<qfloat16>(value);
        }
      }
    }
  });
}

// 8bit は表示用と同じ量子化段階のため、浮動小数を経由せずバイトを並べ替えるだけでよい。
void fillDisplayImageFromUInt8(const PixelBuffer &buffer, QImage &image) {
  const qsizetype pixelStride = buffer.channelCount;
  const RgbaFillPlan plan = makeFillPlan(buffer, 1);
  uchar *destinationBits = image.bits();
  const qsizetype destinationStride = image.bytesPerLine();

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      uchar *destinationRow = destinationBits + static_cast<qsizetype>(y) * destinationStride;
      for (int x = 0; x < buffer.width; ++x) {
        const uchar *sourcePixel = sourceRow + static_cast<qsizetype>(x) * pixelStride;
        uchar *destinationPixel = destinationRow + static_cast<qsizetype>(x) * 4;
        destinationPixel[0] =
            plan.sourceOffset[0] < 0 ? plan.constantByte[0] : sourcePixel[plan.sourceOffset[0]];
        destinationPixel[1] =
            plan.sourceOffset[1] < 0 ? plan.constantByte[1] : sourcePixel[plan.sourceOffset[1]];
        destinationPixel[2] =
            plan.sourceOffset[2] < 0 ? plan.constantByte[2] : sourcePixel[plan.sourceOffset[2]];
        destinationPixel[3] =
            plan.sourceOffset[3] < 0 ? plan.constantByte[3] : sourcePixel[plan.sourceOffset[3]];
      }
    }
  });
}

QImage displayImageFromPixelBuffer(const PixelBuffer &buffer, float gain) {
  if (!buffer.isValid()) {
    return {};
  }

  QImage image(buffer.width, buffer.height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }

  switch (buffer.sampleType) {
    case PixelSampleType::UInt8:
      if (gain == 1.0F) {
        fillDisplayImageFromUInt8(buffer, image);
      } else {
        fillDisplayImage<ReadUInt8>(buffer, image, gain);
      }
      break;
    case PixelSampleType::UInt16:
      fillDisplayImage16<ReadUInt16>(buffer, image, gain);
      break;
    case PixelSampleType::Float16:
      fillDisplayImage16<ReadFloat16>(buffer, image, gain);
      break;
    case PixelSampleType::Float32:
      fillDisplayImage<ReadFloat32>(buffer, image, gain);
      break;
    case PixelSampleType::Unknown:
    default:
      return {};
  }

  return image;
}

DecodeResult successResult(const QImage &displayImage, const QString &backendName,
                           const PixelBuffer &analysisBuffer = {},
                           PixelSampleType sourceSampleType = PixelSampleType::Unknown,
                           PixelColorModel sourceColorModel = PixelColorModel::Unknown,
                           int sourceChannelCount = 0) {
  return {.ok = true,
          .decodedImage = {
              .displayImage = displayImage,
              .analysisBuffer = analysisBuffer,
              .decoderBackend = backendName,
              .sourceSampleType = sourceSampleType,
              .sourceColorModel = sourceColorModel,
              .sourceChannelCount = sourceChannelCount,
          }};
}

enum class DecoderBackend {
  Qt,
  // 形式ごとの自前の復号器 (HDR、TGA、DPX、PSD)。
  Native,
#if defined(HEIRAN_HAVE_LIBSPNG)
  Libspng,
#endif
#if defined(HEIRAN_HAVE_LIBWEBP)
  Libwebp,
#endif
#if defined(HEIRAN_HAVE_LIBAVIF)
  Libavif,
#endif
#if defined(HEIRAN_HAVE_LIBJXL)
  Libjxl,
#endif
#if defined(HEIRAN_HAVE_OPENEXR)
  OpenExr,
#endif
#if defined(HEIRAN_HAVE_LIBTIFF)
  Libtiff,
#endif
  Auto,
};

QString decoderBackendName(DecoderBackend backend) {
  switch (backend) {
    case DecoderBackend::Qt:
      return QStringLiteral("qt");
    case DecoderBackend::Native:
      return QStringLiteral("native");
#if defined(HEIRAN_HAVE_LIBSPNG)
    case DecoderBackend::Libspng:
      return QStringLiteral("spng");
#endif
#if defined(HEIRAN_HAVE_LIBWEBP)
    case DecoderBackend::Libwebp:
      return QStringLiteral("webp");
#endif
#if defined(HEIRAN_HAVE_LIBAVIF)
    case DecoderBackend::Libavif:
      return QStringLiteral("avif");
#endif
#if defined(HEIRAN_HAVE_LIBJXL)
    case DecoderBackend::Libjxl:
      return QStringLiteral("jxl");
#endif
#if defined(HEIRAN_HAVE_OPENEXR)
    case DecoderBackend::OpenExr:
      return QStringLiteral("exr");
#endif
#if defined(HEIRAN_HAVE_LIBTIFF)
    case DecoderBackend::Libtiff:
      return QStringLiteral("tiff");
#endif
    case DecoderBackend::Auto:
    default:
      return QStringLiteral("auto");
  }
}

void traceDecoderMessage(const QString &message) {
  if (!decoderTraceEnabled()) {
    return;
  }
  qDebug().noquote() << QStringLiteral("[HeiranDecode] ") + message;
}

// QImage が 8bit を超える精度を持つ場合だけ、解析用バッファを別に確保する。
// 8bit 画像では表示用 QImage がそのまま元データなので、複製するとメモリを二重に消費する。
PixelBuffer analysisBufferFromQImage(const QImage &image) {
  if (image.isNull() || image.depth() <= 32) {
    return {};
  }

  const QImage source = image.convertToFormat(QImage::Format_RGBA64);
  if (source.isNull()) {
    return {};
  }

  constexpr int kChannelCount = 4;
  const qsizetype bytesPerLine =
      static_cast<qsizetype>(source.width()) * kChannelCount * sizeof(quint16);
  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(static_cast<qsizetype>(source.height()) * bytesPerLine);
  auto *pixels = reinterpret_cast<uchar *>(storage->data());
  if (pixels == nullptr) {
    return {};
  }

  for (int y = 0; y < source.height(); ++y) {
    std::memcpy(pixels + static_cast<qsizetype>(y) * bytesPerLine, source.constScanLine(y),
                static_cast<size_t>(bytesPerLine));
  }

  return {
      .storage = storage,
      .bytesPerRow = bytesPerLine,
      .width = source.width(),
      .height = source.height(),
      .channelCount = kChannelCount,
      .sampleType = PixelSampleType::UInt16,
      .colorModel = PixelColorModel::RGBA,
  };
}

// アニメーションの全フレームが使うメモリの上限。超えたら後ろを読まずに打ち切る。
// 数百コマの大きなアニメーションで、キャッシュの 1 エントリが予算を丸ごと使うのを避ける。
// 1080x1080 で 150 コマの APNG が約 700 MB になる。実物でそれを読めるよう、デコードキャッシュの
// 既定の予算 (1 GiB) と同じにしてある。予算がこれより小さくても、エントリは入る。
constexpr qint64 kMaxAnimationBytes = 1024LL * 1024LL * 1024LL;

DecodedImage decodedImageFromQImage(const QImage &image, const QString &backendName) {
  const PixelBuffer analysisBuffer = analysisBufferFromQImage(image);
  const PixelSampleType sourceSampleType =
      analysisBuffer.isValid() ? PixelSampleType::UInt16 : PixelSampleType::UInt8;
  const PixelColorModel sourceColorModel =
      image.hasAlphaChannel() ? PixelColorModel::RGBA : PixelColorModel::RGB;

  return successResult(image, backendName, analysisBuffer, sourceSampleType, sourceColorModel,
                       image.hasAlphaChannel() ? 4 : 3)
      .decodedImage;
}

// 復号したフレームを 1 つの結果へまとめる。フレームが 1 枚なら普通の結果を返す。
DecodeResult resultFromFrames(QVector<DecodedImage> frames, QVector<int> delaysMs, bool truncated) {
  if (frames.isEmpty()) {
    return {.ok = false, .errorMessage = QStringLiteral("The file has no frames")};
  }
  if (frames.size() == 1) {
    return {.ok = true, .decodedImage = frames.first()};
  }

  const int frameCount = static_cast<int>(frames.size());
  for (int index = 0; index < frameCount; ++index) {
    auto &frame = frames[index];
    frame.subImageKind = SubImageKind::Frames;
    frame.subImageCount = frameCount;
    frame.subImageIndex = index;
    frame.frameDelayMs = normalizedFrameDelayMs(delaysMs.value(index));
  }

  auto sequence = QSharedPointer<FrameSequence>::create();
  sequence->frames = frames;
  sequence->truncated = truncated;

  DecodedImage first = frames.first();
  first.frameSequence = sequence;
  return {.ok = true, .decodedImage = first};
}

// Qt が読めるアニメーション (GIF) を全フレーム読む。
//
// 1 枚ずつ後から読むと、GIF は先頭から順に復号するしか無く、再生のたびに先頭から
// 読み直すことになる。最初に全部読んでおく方が、再生の 1 コマあたりの費用が要らない。
DecodeResult decodeAnimationWithQt(QImageReader &reader) {
  QVector<DecodedImage> frames;
  QVector<int> delays;
  qint64 totalBytes = 0;
  bool truncated = false;

  while (reader.canRead()) {
    const QImage image = reader.read();
    if (image.isNull()) {
      break;
    }
    if (totalBytes + image.sizeInBytes() > kMaxAnimationBytes && !frames.isEmpty()) {
      truncated = true;
      break;
    }
    totalBytes += image.sizeInBytes();
    frames.push_back(decodedImageFromQImage(image, QStringLiteral("qt")));
    delays.push_back(reader.nextImageDelay());
  }

  if (frames.isEmpty()) {
    return {.ok = false, .errorMessage = reader.errorString()};
  }
  return resultFromFrames(std::move(frames), std::move(delays), truncated);
}

DecodeResult resultFromStream(const std::shared_ptr<FrameStream> &stream,
                              const QString &backendName);

DecodeResult decodeWithQt(const QString &filePath, bool honorExifOrientation,
                          bool firstFrameOnly = false) {
  QImageReader reader(filePath);
  reader.setAutoTransform(honorExifOrientation);

#if defined(HEIRAN_HAVE_ZSTD)
  // 大きな GIF は、全部を展開せず、再生しながら圧縮して貯める。大きさはヘッダーから見積もる。
  if (!firstFrameOnly && reader.format() == "gif") {
    const GifInfo info = probeGif(filePath);
    if (info.valid && info.frameCount > 1 &&
        info.decodedBytes() > animationStreamingThresholdBytes()) {
      QString streamError;
      if (const auto stream = openGifStream(filePath, &streamError)) {
        return resultFromStream(stream, QStringLiteral("qt"));
      }
      traceDecoderMessage(QString("gif stream failed file=%1 error=%2").arg(filePath, streamError));
    }
  }
#endif

  if (!firstFrameOnly && reader.supportsAnimation() && reader.imageCount() > 1) {
    return decodeAnimationWithQt(reader);
  }

  const QImage image = reader.read();
  if (image.isNull()) {
    return {.ok = false, .errorMessage = reader.errorString()};
  }

  return {.ok = true, .decodedImage = decodedImageFromQImage(image, QStringLiteral("qt"))};
}

// ページを持つファイルの結果へ、何ページ中の何ページ目かを書き込む。
void annotatePages(DecodedImage *decodedImage, int subImageCount, int subImageIndex) {
  if (subImageCount <= 1) {
    return;
  }
  decodedImage->subImageKind = SubImageKind::Pages;
  decodedImage->subImageCount = subImageCount;
  decodedImage->subImageIndex = subImageIndex;
}

void cleanupSharedByteArray(void *info) {
  delete static_cast<QSharedPointer<QByteArray> *>(info);
}

DecoderBackend decoderBackendFromEnvironment() {
  static const DecoderBackend backend = []() {
    const QString value = QString::fromUtf8(qgetenv("HEIRAN_IMAGE_DECODER")).trimmed().toLower();
    if (value == "qt") {
      return DecoderBackend::Qt;
    }
#if defined(HEIRAN_HAVE_LIBSPNG)
    if (value == "spng" || value == "libspng") {
      return DecoderBackend::Libspng;
    }
#endif
    return DecoderBackend::Auto;
  }();

  return backend;
}

// 後から作るアニメーション (FrameStream) を、復号結果にする。結果そのものが先頭のフレーム。
DecodeResult resultFromStream(const std::shared_ptr<FrameStream> &stream,
                              const QString &backendName) {
  DecodedImage decoded = decodedImageFromQImage(stream->frameIfReady(0), backendName);
  decoded.subImageKind = SubImageKind::Frames;
  decoded.subImageCount = stream->frameCount();
  decoded.subImageIndex = 0;
  decoded.frameDelayMs = stream->delayMs(0);
  decoded.frameStream = stream;
  return {.ok = true, .decodedImage = decoded};
}

// 全部を展開したアニメーションを、復号結果にする。
DecodeResult resultFromAnimation(const AnimationResult &animation, const QString &backendName) {
  QVector<DecodedImage> frames;
  QVector<int> delays;
  frames.reserve(animation.frames.size());
  for (const auto &frame : animation.frames) {
    frames.push_back(decodedImageFromQImage(frame.image, backendName));
    delays.push_back(frame.delayMs);
  }
  return resultFromFrames(std::move(frames), std::move(delays), animation.truncated);
}

// フレームの取り出し方から、復号結果を作る。APNG と WebP に共通。
//
// 全部を展開したときの大きさ (幅 x 高さ x 4 x フレーム数) を、画素を読まずに見積もる。
// 境目を超える大きなものは展開せず、圧縮したまま持って、要るフレームだけを背景で作る。
// 1 コマだけのものや、先頭だけを求められた場合 (サムネイル) は、展開してよい。
DecodeResult decodeAnimationSource(const std::shared_ptr<const AnimationSource> &source,
                                   const QString &backendName, const QString &filePath,
                                   bool firstFrameOnly) {
  if (!firstFrameOnly && source->frameCount() > 1 &&
      source->decodedBytes() > animationStreamingThresholdBytes()) {
    QString streamError;
    if (const auto stream = openAnimationStream(source, &streamError)) {
      return resultFromStream(stream, backendName);
    }
    traceDecoderMessage(
        QString("%1 stream failed file=%2 error=%3").arg(backendName, filePath, streamError));
  }

  const AnimationResult animation = composeAllFrames(*source, kMaxAnimationBytes, firstFrameOnly);
  if (!animation.ok) {
    return {.ok = false, .errorMessage = animation.errorMessage};
  }
  return resultFromAnimation(animation, backendName);
}

// アニメーション PNG。Qt も libspng も 2 枚目以降を読めないため、自前の復号器を通す。
// EXIF の向きは適用しない。`decode()` 側で適用したと記録しない。
DecodeResult decodeWithApng(const QString &filePath, bool firstFrameOnly) {
  // APNG は、ヘッダーだけから展開後の大きさが分かる。大きなものは、ファイルを読む前に
  // 逐次展開と決められる。
  if (!firstFrameOnly) {
    const ApngInfo info = probeApng(filePath);
    if (info.animated && info.frameCount > 1 &&
        info.decodedBytes() > animationStreamingThresholdBytes()) {
      QString streamError;
      if (const auto stream = openApngStream(filePath, &streamError)) {
        return resultFromStream(stream, QStringLiteral("apng"));
      }
      traceDecoderMessage(
          QString("apng stream failed file=%1 error=%2").arg(filePath, streamError));
    }
  }

  const ApngResult apng = decodeApng(filePath, kMaxAnimationBytes, firstFrameOnly);
  if (!apng.ok) {
    return {.ok = false, .errorMessage = apng.errorMessage};
  }
  return resultFromAnimation(apng, QStringLiteral("apng"));
}

#if defined(HEIRAN_HAVE_LIBAVIF)
// AVIF。HEIC を伴う libheif は使わず、AV1 だけを復号する libavif と
// dav1d で読む。向き (irot / imir) は復号器の側で適用済み。EXIF の向きは適用したと記録しない。
// アニメーション AVIF は先頭の 1 コマだけ。
DecodeResult decodeWithLibavif(const QString &filePath) {
  const AvifResult avif = decodeAvif(filePath);
  if (!avif.ok) {
    return {.ok = false, .errorMessage = avif.errorMessage};
  }
  DecodedImage decoded = decodedImageFromQImage(avif.image, QStringLiteral("avif"));
  decoded.embeddedColorProfile.iccProfile = avif.iccProfile;
  decoded.embeddedColorProfile.declaredColorSpace = avif.declaredColorSpace;
  return {.ok = true, .decodedImage = decoded};
}
#endif

// 自前の復号器が返した画素を、復号結果にする。解析用バッファと、
// 表示用の画像) にする。8bit の RGBA だけは、2 つで記憶域を共有する。
DecodeResult decodeFromRawImage(RawImage raw, const QString &backendName) {
  if (!raw.ok) {
    return {.ok = false, .errorMessage = raw.errorMessage};
  }

  const qsizetype bytesPerRow = raw.bytesPerRow();
  const PixelSampleType sampleType = raw.sampleType;
  const int channels = raw.channels;
  auto storage = QSharedPointer<QByteArray>::create(std::move(raw.pixels));
  const PixelBuffer analysisBuffer{
      .storage = storage,
      .bytesPerRow = bytesPerRow,
      .width = raw.width,
      .height = raw.height,
      .channelCount = channels,
      .sampleType = sampleType,
      .colorModel = pixelColorModelForChannelCount(channels),
  };

  QImage image;
  if (sampleType == PixelSampleType::UInt8 && channels == 4) {
    image = QImage(reinterpret_cast<const uchar *>(storage->constData()), raw.width, raw.height,
                   bytesPerRow, QImage::Format_RGBA8888, cleanupSharedByteArray,
                   new QSharedPointer<QByteArray>(storage));
  } else {
    image = displayImageFromPixelBuffer(analysisBuffer, 1.0F);
  }
  if (image.isNull()) {
    return {.ok = false, .errorMessage = QStringLiteral("Failed to allocate QImage buffer")};
  }

  auto result = successResult(image, backendName, analysisBuffer, sampleType,
                              pixelColorModelForChannelCount(channels), channels);
  result.decodedImage.embeddedColorProfile.iccProfile = raw.iccProfile;
  return result;
}

// ファイルを全部読む。読めなければ空。
QByteArray readWholeFile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

// HDR、TGA、DPX、PSD。拡張子で復号器を選ぶ。向きは復号器の側で適用済みで、
// EXIF の向きは適用したと記録しない。
DecodeResult decodeWithNative(const QString &filePath) {
  const QString extension = QFileInfo(filePath).suffix().toLower();
  const QByteArray contents = readWholeFile(filePath);
  if (contents.isEmpty()) {
    return {.ok = false, .errorMessage = QStringLiteral("Failed to read the file")};
  }
  if (extension == "hdr") {
    return decodeFromRawImage(decodeRadianceHdr(contents), QStringLiteral("hdr"));
  }
  if (extension == "tga") {
    return decodeFromRawImage(decodeTga(contents), QStringLiteral("tga"));
  }
  if (extension == "dpx") {
    return decodeFromRawImage(decodeDpx(contents), QStringLiteral("dpx"));
  }
  if (extension == "psd") {
    return decodeFromRawImage(decodePsd(contents), QStringLiteral("psd"));
  }
  return {.ok = false, .errorMessage = QStringLiteral("No native decoder for this format")};
}

#if defined(HEIRAN_HAVE_LIBTIFF)
// TIFF。libtiff を直接使う。ページ (IFD) はページとして扱う。
// 向きは、`honorExifOrientation` が真のときだけ、ここで適用する (記録は `decode()` が行う)。
DecodeResult decodeWithLibtiff(const QString &filePath, bool honorExifOrientation, int pageIndex) {
  TiffResult tiff = decodeTiff(filePath, pageIndex, honorExifOrientation);
  if (!tiff.raw.ok) {
    return {.ok = false, .errorMessage = tiff.raw.errorMessage};
  }
  const int pageCount = tiff.pageCount;
  const int appliedOrientation = tiff.appliedOrientation;
  auto result = decodeFromRawImage(std::move(tiff.raw), QStringLiteral("tiff"));
  if (!result.ok) {
    return result;
  }
  annotatePages(&result.decodedImage, pageCount, pageIndex);
  // 適用した向きは、このページのタグの値。`decode()` は、先頭のページの向きを読むため使わない。
  result.decodedImage.appliedExifOrientation = static_cast<ExifOrientation>(appliedOrientation);
  return result;
}
#endif

#if defined(HEIRAN_HAVE_OPENEXR)
// EXR。OpenEXR を直接使う。ファイルが複数のパートを持てば、パートをページとして扱う。
DecodeResult decodeWithOpenExr(const QString &filePath, int partIndex) {
  ExrResult exr = decodeExr(filePath, partIndex);
  if (!exr.raw.ok) {
    return {.ok = false, .errorMessage = exr.raw.errorMessage};
  }
  const int partCount = exr.partCount;
  const QColorSpace declared = exr.declaredColorSpace;
  auto result = decodeFromRawImage(std::move(exr.raw), QStringLiteral("exr"));
  if (!result.ok) {
    return result;
  }
  result.decodedImage.embeddedColorProfile.declaredColorSpace = declared;
  annotatePages(&result.decodedImage, partCount, partIndex);
  return result;
}
#endif

#if defined(HEIRAN_HAVE_LIBJXL)
// JPEG XL。向きは復号器の側で適用済み。EXIF の向きは適用したと記録しない。
// アニメーションは先頭の 1 コマだけ。
DecodeResult decodeWithLibjxl(const QString &filePath) {
  const JxlResult jxl = decodeJxl(filePath);
  if (!jxl.ok) {
    return {.ok = false, .errorMessage = jxl.errorMessage};
  }
  DecodedImage decoded = decodedImageFromQImage(jxl.image, QStringLiteral("jxl"));
  decoded.embeddedColorProfile.iccProfile = jxl.iccProfile;
  decoded.embeddedColorProfile.declaredColorSpace = jxl.declaredColorSpace;
  return {.ok = true, .decodedImage = decoded};
}
#endif

#if defined(HEIRAN_HAVE_LIBWEBP)
// WebP の静止画とアニメーション。APNG と同じ扱い (合成と、逐次展開の判断を共有する)。
//
// Qt の WebP プラグインは標準の配布に含まれない。libwebp の WebPAnimDecoder は先頭から順にしか
// 進められず、コマ戻しや飛び越しのために合成し直せないため使わない。
// EXIF の向きは適用しない。WebP で向きを持つファイルはまれで、適用した値を結果へ
// 残す処理は `decode()` 側で WebP を除いている。
DecodeResult decodeWithLibwebp(const QString &filePath, bool firstFrameOnly) {
  QString error;
  const auto source = openWebpSource(filePath, &error);
  if (source == nullptr) {
    return {.ok = false, .errorMessage = error};
  }
  return decodeAnimationSource(source, QStringLiteral("webp"), filePath, firstFrameOnly);
}
#endif

#if defined(HEIRAN_HAVE_LIBSPNG)
// 比較計測用の PNG デコーダ。既定の経路では使わない。
DecodeResult decodeWithLibspng(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {.ok = false, .errorMessage = QStringLiteral("Could not open the file.")};
  }

  const QByteArray source = file.readAll();
  file.close();

  spng_ctx *context = spng_ctx_new(0);
  if (context == nullptr) {
    return {.ok = false, .errorMessage = QStringLiteral("Failed to create the spng context.")};
  }
  const auto contextGuard = qScopeGuard([context]() { spng_ctx_free(context); });

  spng_set_crc_action(context, SPNG_CRC_USE, SPNG_CRC_USE);
  if (const int status =
          spng_set_png_buffer(context, source.constData(), static_cast<size_t>(source.size()));
      status != 0) {
    return {.ok = false, .errorMessage = QString::fromUtf8(spng_strerror(status))};
  }

  spng_ihdr header{};
  if (const int status = spng_get_ihdr(context, &header); status != 0) {
    return {.ok = false, .errorMessage = QString::fromUtf8(spng_strerror(status))};
  }

  // 8bit を超える PNG は精度を落とさないよう 16bit で受け取る。
  const bool wide = header.bit_depth > 8;
  const int format = wide ? SPNG_FMT_RGBA16 : SPNG_FMT_RGBA8;
  const PixelSampleType sampleType = wide ? PixelSampleType::UInt16 : PixelSampleType::UInt8;

  size_t decodedSize = 0;
  if (const int status = spng_decoded_image_size(context, format, &decodedSize); status != 0) {
    return {.ok = false, .errorMessage = QString::fromUtf8(spng_strerror(status))};
  }

  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(static_cast<qsizetype>(decodedSize));
  auto *pixels = reinterpret_cast<uchar *>(storage->data());
  if (pixels == nullptr) {
    return {.ok = false, .errorMessage = QStringLiteral("Failed to allocate image buffer")};
  }

  if (const int status = spng_decode_image(context, pixels, decodedSize, format, 0); status != 0) {
    return {.ok = false, .errorMessage = QString::fromUtf8(spng_strerror(status))};
  }

  const int width = static_cast<int>(header.width);
  const int height = static_cast<int>(header.height);
  const qsizetype bytesPerLine = static_cast<qsizetype>(width) * 4 * bytesPerSample(sampleType);

  // 解析用バッファは RGBA で受け取るが、元ファイルの色モデルは別に保つ。
  // これを RGBA 固定にすると、アルファを持たない画像でも A が表示されてしまう。
  PixelColorModel sourceColorModel = PixelColorModel::RGB;
  int sourceChannelCount = 3;
  switch (header.color_type) {
    case SPNG_COLOR_TYPE_GRAYSCALE:
      sourceColorModel = PixelColorModel::Gray;
      sourceChannelCount = 1;
      break;
    case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
      sourceColorModel = PixelColorModel::GrayAlpha;
      sourceChannelCount = 2;
      break;
    case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
      sourceColorModel = PixelColorModel::RGBA;
      sourceChannelCount = 4;
      break;
    case SPNG_COLOR_TYPE_INDEXED: {
      // パレットに tRNS があれば透明度を持つ。
      spng_trns transparency{};
      if (spng_get_trns(context, &transparency) == 0) {
        sourceColorModel = PixelColorModel::RGBA;
        sourceChannelCount = 4;
      }
      break;
    }
    case SPNG_COLOR_TYPE_TRUECOLOR:
    default:
      break;
  }

  const PixelBuffer analysisBuffer{
      .storage = storage,
      .bytesPerRow = bytesPerLine,
      .width = width,
      .height = height,
      .channelCount = 4,
      .sampleType = sampleType,
      .colorModel = PixelColorModel::RGBA,
  };

  if (!wide) {
    // 8bit は表示用 QImage と記憶域を共有できる。追加のメモリを使わない。
    QImage image(pixels, width, height, bytesPerLine, QImage::Format_RGBA8888,
                 cleanupSharedByteArray, new QSharedPointer<QByteArray>(storage));
    if (image.isNull()) {
      return {.ok = false, .errorMessage = QStringLiteral("Failed to allocate QImage buffer")};
    }
    return successResult(image, QStringLiteral("spng"), analysisBuffer, sampleType,
                         sourceColorModel, sourceChannelCount);
  }

  const QImage image = displayImageFromPixelBuffer(analysisBuffer, 1.0F);
  if (image.isNull()) {
    return {.ok = false, .errorMessage = QStringLiteral("Failed to allocate QImage buffer")};
  }
  return successResult(image, QStringLiteral("spng"), analysisBuffer, sampleType, sourceColorModel,
                       sourceChannelCount);
}
#endif


DecoderBackend preferredBackendForFile(const QString &filePath) {
  const DecoderBackend forcedBackend = decoderBackendFromEnvironment();
  if (forcedBackend != DecoderBackend::Auto) {
    return forcedBackend;
  }

  const QString extension = QFileInfo(filePath).suffix().toLower();

#if defined(HEIRAN_HAVE_LIBSPNG)
  // PNG は libspng が Qt の約 2 倍速く、全画素が一致することを確認済み。
  // 失敗した場合は decode 側で Qt へ退避する。
  if (extension == "png") {
    return DecoderBackend::Libspng;
  }
#endif

#if defined(HEIRAN_HAVE_LIBWEBP)
  if (extension == "webp") {
    return DecoderBackend::Libwebp;
  }
#endif

#if defined(HEIRAN_HAVE_LIBAVIF)
  if (extension == "avif" || extension == "avifs") {
    return DecoderBackend::Libavif;
  }
#endif

#if defined(HEIRAN_HAVE_LIBJXL)
  if (extension == "jxl") {
    return DecoderBackend::Libjxl;
  }
#endif

#if defined(HEIRAN_HAVE_LIBTIFF)
  // TIFF は libtiff を直接使う。読めない形 (32bit 整数、倍精度、チャンネルが多いものなど) は、
  // Qt へ退避する。
  if (extension == "tif" || extension == "tiff") {
    return DecoderBackend::Libtiff;
  }
#endif

#if defined(HEIRAN_HAVE_OPENEXR)
  // EXR は OpenEXR を直接使う。Qt は EXR を読めないため、退避先は無い。
  if (extension == "exr") {
    return DecoderBackend::OpenExr;
  }
#endif

  static const QSet<QString> qtPreferredExtensions = {"png", "jpg", "jpeg", "bmp", "gif", "webp",
                                                      "ico", "pbm", "pgm",  "ppm", "xbm", "xpm"};
  // HDR、TGA、DPX、PSD は、形式ごとの自前の復号器で読む。
  static const QSet<QString> nativeExtensions = {"hdr", "tga", "dpx", "psd"};
  if (nativeExtensions.contains(extension)) {
    return DecoderBackend::Native;
  }

  if (qtPreferredExtensions.contains(extension)) {
    return DecoderBackend::Qt;
  }

  return DecoderBackend::Qt;
}

}  // namespace

namespace {

DecodeResult decodeWithPreferredBackend(const QString &filePath, bool honorExifOrientation,
                                        int subImageIndex, bool firstFrameOnly) {
  // アニメーション PNG は、どの復号器でも 2 枚目以降を読めない。先に見分けて自前で読む。
  // 通常の PNG を開くたびに通るが、画素を含まない先頭だけを見るため軽い。
  const QString extension = QFileInfo(filePath).suffix().toLower();
  if ((extension == "png" || extension == "apng") && isAnimatedPng(filePath)) {
    auto apngResult = decodeWithApng(filePath, firstFrameOnly);
    if (apngResult.ok) {
      return apngResult;
    }
    traceDecoderMessage(
        QString("backend=apng failed file=%1 error=%2").arg(filePath, apngResult.errorMessage));
  }

  const DecoderBackend preferredBackend = preferredBackendForFile(filePath);
  traceDecoderMessage(
      QString("requested=%1 file=%2").arg(decoderBackendName(preferredBackend), filePath));

  switch (preferredBackend) {
    case DecoderBackend::Qt:
      return decodeWithQt(filePath, honorExifOrientation, firstFrameOnly);
    case DecoderBackend::Native: {
      const auto nativeResult = decodeWithNative(filePath);
      if (!nativeResult.ok) {
        traceDecoderMessage(QString("backend=native failed file=%1 error=%2")
                                .arg(filePath, nativeResult.errorMessage));
      }
      return nativeResult;
    }
#if defined(HEIRAN_HAVE_LIBWEBP)
    case DecoderBackend::Libwebp: {
      const auto webpResult = decodeWithLibwebp(filePath, firstFrameOnly);
      if (webpResult.ok) {
        return webpResult;
      }
      traceDecoderMessage(
          QString("backend=webp failed file=%1 error=%2").arg(filePath, webpResult.errorMessage));
      return decodeWithQt(filePath, honorExifOrientation, firstFrameOnly);
    }
#endif
#if defined(HEIRAN_HAVE_LIBAVIF)
    case DecoderBackend::Libavif: {
      const auto avifResult = decodeWithLibavif(filePath);
      if (avifResult.ok) {
        return avifResult;
      }
      traceDecoderMessage(
          QString("backend=avif failed file=%1 error=%2").arg(filePath, avifResult.errorMessage));
      return avifResult;
    }
#endif
#if defined(HEIRAN_HAVE_LIBTIFF)
    case DecoderBackend::Libtiff: {
      const auto tiffResult = decodeWithLibtiff(filePath, honorExifOrientation, subImageIndex);
      if (tiffResult.ok) {
        return tiffResult;
      }
      traceDecoderMessage(
          QString("backend=tiff failed file=%1 error=%2").arg(filePath, tiffResult.errorMessage));
      // libtiff で読めなかったもの (32bit 整数など) は、Qt が読めれば読む。
      const auto qtResult = decodeWithQt(filePath, honorExifOrientation, false);
      if (qtResult.ok) {
        traceDecoderMessage(QString("fallback=qt file=%1").arg(filePath));
        return qtResult;
      }
      return tiffResult;
    }
#endif
#if defined(HEIRAN_HAVE_OPENEXR)
    case DecoderBackend::OpenExr: {
      const auto exrResult = decodeWithOpenExr(filePath, subImageIndex);
      if (exrResult.ok) {
        return exrResult;
      }
      traceDecoderMessage(
          QString("backend=exr failed file=%1 error=%2").arg(filePath, exrResult.errorMessage));
      return exrResult;
    }
#endif
#if defined(HEIRAN_HAVE_LIBJXL)
    case DecoderBackend::Libjxl: {
      const auto jxlResult = decodeWithLibjxl(filePath);
      if (!jxlResult.ok) {
        traceDecoderMessage(
            QString("backend=jxl failed file=%1 error=%2").arg(filePath, jxlResult.errorMessage));
      }
      return jxlResult;
    }
#endif
#if defined(HEIRAN_HAVE_LIBSPNG)
    case DecoderBackend::Libspng: {
      // 比較計測用。失敗した場合は Qt へ落とす。
      const auto spngResult = decodeWithLibspng(filePath);
      if (spngResult.ok) {
        return spngResult;
      }
      traceDecoderMessage(
          QString("backend=spng failed file=%1 error=%2").arg(filePath, spngResult.errorMessage));
      return decodeWithQt(filePath, honorExifOrientation, firstFrameOnly);
    }
#endif
    case DecoderBackend::Auto:
    default:
      return decodeWithQt(filePath, honorExifOrientation, firstFrameOnly);
  }
}

}  // namespace

namespace {

// 埋め込みの申告が無い場合の色空間。
//
// 浮動小数で持つ形式 (OpenEXR, Radiance HDR) は線形光を入れる約束になっている。
// 整数形式は sRGB を想定する。どちらも慣習であって保証ではないため、`.hrn` から
// 上書きできるようにする。
QColorSpace defaultColorSpaceFor(PixelSampleType sampleType) {
  const bool floatingPoint =
      sampleType == PixelSampleType::Float16 || sampleType == PixelSampleType::Float32;
  return QColorSpace(floatingPoint ? QColorSpace::SRgbLinear : QColorSpace::SRgb);
}

}  // namespace

DecodeResult ImageDecoder::decode(const QString &filePath, bool honorExifOrientation,
                                  int subImageIndex, bool firstFrameOnly) const {
  auto result =
      decodeWithPreferredBackend(filePath, honorExifOrientation, subImageIndex, firstFrameOnly);
  if (!result.ok) {
    return result;
  }

  // 適用した orientation を結果へ残す。どの backend も適用はするが、適用した値を
  // 教えてくれないため、ここでファイルから読み直して記録する。
  // 画素値の位置を元ファイルの座標で出すために要る。
  // libwebp と APNG の復号器は向きを適用しないため、適用したと記録してはいけない。
  const QString &backend = result.decodedImage.decoderBackend;
  if (honorExifOrientation && backend != QStringLiteral("webp") &&
      backend != QStringLiteral("apng") && backend != QStringLiteral("avif") &&
      backend != QStringLiteral("jxl") && backend != QStringLiteral("hdr") &&
      backend != QStringLiteral("tga") && backend != QStringLiteral("dpx") &&
      backend != QStringLiteral("psd") && backend != QStringLiteral("exr") &&
      backend != QStringLiteral("tiff")) {
    result.decodedImage.appliedExifOrientation = readExifOrientation(filePath);
  }

  // 色空間も同じ理由でここで読む。backend ごとに書くと退避経路で漏れる。
  // 自前の復号器は、復号時にしか取れない申告 (EXR の chromaticities など) を
  // 先に入れている。その場合は上書きしない。
  if (result.decodedImage.embeddedColorProfile.isEmpty()) {
    result.decodedImage.embeddedColorProfile = readEmbeddedColorProfile(filePath);
  }
  QColorSpace resolved = result.decodedImage.embeddedColorProfile.resolve();
  if (!resolved.isValid()) {
    // Qt 経路では復号結果が色空間を持っていることがある。自前で読めなかった場合に使う。
    resolved = result.decodedImage.displayImage.colorSpace();
  }
  result.decodedImage.colorSpace =
      resolved.isValid() ? resolved : defaultColorSpaceFor(result.decodedImage.sourceSampleType);

  // 色や向きはファイル単位の情報で、全フレームに共通。フレームの側へも写す。
  if (result.decodedImage.frameSequence != nullptr) {
    auto sequence = QSharedPointer<FrameSequence>::create(*result.decodedImage.frameSequence);
    for (auto &frame : sequence->frames) {
      frame.embeddedColorProfile = result.decodedImage.embeddedColorProfile;
      frame.colorSpace = result.decodedImage.colorSpace;
      frame.appliedExifOrientation = result.decodedImage.appliedExifOrientation;
    }
    result.decodedImage.frameSequence = sequence;
  }
  return result;
}

int normalizedFrameDelayMs(int delayMs) {
  return delayMs <= 10 ? 100 : delayMs;
}

namespace {

std::atomic<qint64> g_streamingThresholdBytes{128LL * 1024LL * 1024LL};

}  // namespace

qint64 animationStreamingThresholdBytes() {
  return g_streamingThresholdBytes.load();
}

void setAnimationStreamingThresholdBytes(qint64 bytes) {
  g_streamingThresholdBytes.store(bytes);
}

std::optional<DecodedImage> tryFrameOf(const DecodedImage &decodedImage, int index) {
  if (decodedImage.frameStream != nullptr) {
    const QImage image = decodedImage.frameStream->frameIfReady(index);
    if (image.isNull()) {
      return std::nullopt;
    }

    // 色や向きなどはファイル単位で、先頭のフレームと同じ。画素だけを差し替える。
    // 8bit のため、解析用バッファは別に持たない。
    const int frameCount = decodedImage.frameStream->frameCount();
    const int wrapped = ((index % frameCount) + frameCount) % frameCount;
    DecodedImage frame = decodedImage;
    frame.frameStream.reset();
    frame.frameSequence.reset();
    frame.displayImage = image;
    frame.analysisBuffer = {};
    frame.subImageIndex = wrapped;
    frame.frameDelayMs = decodedImage.frameStream->delayMs(wrapped);
    return frame;
  }
  return frameOf(decodedImage, index);
}

int frameDelayMsOf(const DecodedImage &decodedImage, int index) {
  if (decodedImage.frameStream != nullptr) {
    return decodedImage.frameStream->delayMs(index);
  }
  return frameOf(decodedImage, index).frameDelayMs;
}

DecodedImage frameOf(const DecodedImage &decodedImage, int index) {
  if (decodedImage.frameStream != nullptr) {
    const auto frame = tryFrameOf(decodedImage, index);
    return frame ? *frame : decodedImage;
  }
  if (decodedImage.frameSequence == nullptr || decodedImage.frameSequence->frames.isEmpty()) {
    return decodedImage;
  }

  const auto &frames = decodedImage.frameSequence->frames;
  const int frameCount = static_cast<int>(frames.size());
  const int wrapped = ((index % frameCount) + frameCount) % frameCount;
  return frames.at(wrapped);
}

QImage renderDisplayImage(const PixelBuffer &buffer, float gain) {
  return displayImageFromPixelBuffer(buffer, gain);
}

namespace {

template <typename Reader>
void fillLumaPlane(const PixelBuffer &buffer, const heiran::domain::LumaWeights &weights,
                   float *destination) {
  const int sampleBytes = bytesPerSample(buffer.sampleType);
  const qsizetype pixelStride = static_cast<qsizetype>(buffer.channelCount) * sampleBytes;
  const RgbaPackPlan plan = makePackPlan(buffer, sampleBytes);

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      float *destinationRow = destination + static_cast<qsizetype>(y) * buffer.width;
      for (int x = 0; x < buffer.width; ++x) {
        const uchar *sourcePixel = sourceRow + static_cast<qsizetype>(x) * pixelStride;
        double channels[3] = {0.0, 0.0, 0.0};
        for (int channel = 0; channel < 3; ++channel) {
          channels[channel] = plan.sourceOffset[channel] < 0
                                  ? plan.constantValue[channel]
                                  : Reader::read(sourcePixel + plan.sourceOffset[channel]);
        }
        destinationRow[x] = static_cast<float>(
            weights.red * channels[0] + weights.green * channels[1] + weights.blue * channels[2]);
      }
    }
  });
}

// 8bit の表示用画像から直接読む。解析用バッファを持たない画像の経路。
void fillLumaPlaneFromDisplayImage(const QImage &image, const heiran::domain::LumaWeights &weights,
                                   float *destination) {
  const QImage rgbaImage = image.format() == QImage::Format_RGBA8888
                               ? image
                               : image.convertToFormat(QImage::Format_RGBA8888);
  heiran::infra::forEachRowBand(rgbaImage.height(), [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = rgbaImage.constScanLine(y);
      float *destinationRow = destination + static_cast<qsizetype>(y) * rgbaImage.width();
      for (int x = 0; x < rgbaImage.width(); ++x) {
        const uchar *sourcePixel = sourceRow + static_cast<qsizetype>(x) * 4;
        destinationRow[x] =
            static_cast<float>((weights.red * sourcePixel[0] + weights.green * sourcePixel[1] +
                                weights.blue * sourcePixel[2]) /
                               255.0);
      }
    }
  });
}

template <typename Reader>
void fillChannelPlane(const PixelBuffer &buffer, int rgbaChannel, float *destination) {
  const int sampleBytes = bytesPerSample(buffer.sampleType);
  const qsizetype pixelStride = static_cast<qsizetype>(buffer.channelCount) * sampleBytes;
  const RgbaPackPlan plan = makePackPlan(buffer, sampleBytes);
  const qsizetype channelOffset = plan.sourceOffset[rgbaChannel];
  const float constantValue = plan.constantValue[rgbaChannel];

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      float *destinationRow = destination + static_cast<qsizetype>(y) * buffer.width;
      if (channelOffset < 0) {
        std::fill_n(destinationRow, buffer.width, constantValue);
        continue;
      }

      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      for (int x = 0; x < buffer.width; ++x) {
        destinationRow[x] =
            Reader::read(sourceRow + static_cast<qsizetype>(x) * pixelStride + channelOffset);
      }
    }
  });
}

template <typename Reader>
void fillRgbPlanes(const PixelBuffer &buffer, float *const destinations[3]) {
  const int sampleBytes = bytesPerSample(buffer.sampleType);
  const qsizetype pixelStride = static_cast<qsizetype>(buffer.channelCount) * sampleBytes;
  const RgbaPackPlan plan = makePackPlan(buffer, sampleBytes);

  heiran::infra::forEachRowBand(buffer.height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = buffer.data() + static_cast<qsizetype>(y) * buffer.bytesPerRow;
      for (int channel = 0; channel < 3; ++channel) {
        float *destinationRow = destinations[channel] + static_cast<qsizetype>(y) * buffer.width;
        if (plan.sourceOffset[channel] < 0) {
          std::fill_n(destinationRow, buffer.width, plan.constantValue[channel]);
          continue;
        }
        const uchar *channelRow = sourceRow + plan.sourceOffset[channel];
        for (int x = 0; x < buffer.width; ++x) {
          destinationRow[x] = Reader::read(channelRow + static_cast<qsizetype>(x) * pixelStride);
        }
      }
    }
  });
}

void fillRgbPlanesFromDisplayImage(const QImage &image, float *const destinations[3]) {
  const QImage rgbaImage = image.format() == QImage::Format_RGBA8888
                               ? image
                               : image.convertToFormat(QImage::Format_RGBA8888);
  heiran::infra::forEachRowBand(rgbaImage.height(), [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = rgbaImage.constScanLine(y);
      float *red = destinations[0] + static_cast<qsizetype>(y) * rgbaImage.width();
      float *green = destinations[1] + static_cast<qsizetype>(y) * rgbaImage.width();
      float *blue = destinations[2] + static_cast<qsizetype>(y) * rgbaImage.width();
      for (int x = 0; x < rgbaImage.width(); ++x) {
        const uchar *pixel = sourceRow + static_cast<qsizetype>(x) * 4;
        red[x] = static_cast<float>(pixel[0]) / 255.0F;
        green[x] = static_cast<float>(pixel[1]) / 255.0F;
        blue[x] = static_cast<float>(pixel[2]) / 255.0F;
      }
    }
  });
}

void fillChannelPlaneFromDisplayImage(const QImage &image, int rgbaChannel, float *destination) {
  const QImage rgbaImage = image.format() == QImage::Format_RGBA8888
                               ? image
                               : image.convertToFormat(QImage::Format_RGBA8888);
  heiran::infra::forEachRowBand(rgbaImage.height(), [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const uchar *sourceRow = rgbaImage.constScanLine(y);
      float *destinationRow = destination + static_cast<qsizetype>(y) * rgbaImage.width();
      for (int x = 0; x < rgbaImage.width(); ++x) {
        destinationRow[x] =
            static_cast<float>(sourceRow[static_cast<qsizetype>(x) * 4 + rgbaChannel]) / 255.0F;
      }
    }
  });
}

}  // namespace

QVector<float> DecodedImage::buildSourceChannelPlane(int rgbaChannel) const {
  if (rgbaChannel < 0 || rgbaChannel > 3) {
    return {};
  }

  const QSize planeSize = analysisBuffer.isValid()
                              ? QSize(analysisBuffer.width, analysisBuffer.height)
                              : displayImage.size();
  if (planeSize.isEmpty()) {
    return {};
  }

  QVector<float> plane(static_cast<qsizetype>(planeSize.width()) * planeSize.height(), 0.0F);
  if (!analysisBuffer.isValid()) {
    fillChannelPlaneFromDisplayImage(displayImage, rgbaChannel, plane.data());
    return plane;
  }

  switch (analysisBuffer.sampleType) {
    case PixelSampleType::UInt8:
      fillChannelPlane<ReadUInt8>(analysisBuffer, rgbaChannel, plane.data());
      break;
    case PixelSampleType::UInt16:
      fillChannelPlane<ReadUInt16>(analysisBuffer, rgbaChannel, plane.data());
      break;
    case PixelSampleType::Float16:
      fillChannelPlane<ReadFloat16>(analysisBuffer, rgbaChannel, plane.data());
      break;
    case PixelSampleType::Float32:
      fillChannelPlane<ReadFloat32>(analysisBuffer, rgbaChannel, plane.data());
      break;
    case PixelSampleType::Unknown:
    default:
      break;
  }
  return plane;
}

bool DecodedImage::buildSourceRgbPlanes(QVector<float> (&planes)[3]) const {
  const QSize planeSize = analysisBuffer.isValid()
                              ? QSize(analysisBuffer.width, analysisBuffer.height)
                              : displayImage.size();
  if (planeSize.isEmpty()) {
    return false;
  }
  const qsizetype pixelCount = static_cast<qsizetype>(planeSize.width()) * planeSize.height();
  // 全部を書き込むため、0 で埋めない。
  for (auto &plane : planes) {
    plane.resizeForOverwrite(pixelCount);
  }
  float *const destinations[3] = {planes[0].data(), planes[1].data(), planes[2].data()};

  if (!analysisBuffer.isValid()) {
    fillRgbPlanesFromDisplayImage(displayImage, destinations);
    return true;
  }
  switch (analysisBuffer.sampleType) {
    case PixelSampleType::UInt8:
      fillRgbPlanes<ReadUInt8>(analysisBuffer, destinations);
      return true;
    case PixelSampleType::UInt16:
      fillRgbPlanes<ReadUInt16>(analysisBuffer, destinations);
      return true;
    case PixelSampleType::Float16:
      fillRgbPlanes<ReadFloat16>(analysisBuffer, destinations);
      return true;
    case PixelSampleType::Float32:
      fillRgbPlanes<ReadFloat32>(analysisBuffer, destinations);
      return true;
    case PixelSampleType::Unknown:
    default:
      return false;
  }
}

QVector<float> DecodedImage::buildSourceLumaPlane(
    const heiran::domain::LumaWeights &weights) const {
  const QSize planeSize = analysisBuffer.isValid()
                              ? QSize(analysisBuffer.width, analysisBuffer.height)
                              : displayImage.size();
  if (planeSize.isEmpty()) {
    return {};
  }

  QVector<float> plane(static_cast<qsizetype>(planeSize.width()) * planeSize.height(), 0.0F);
  if (!analysisBuffer.isValid()) {
    fillLumaPlaneFromDisplayImage(displayImage, weights, plane.data());
    return plane;
  }

  switch (analysisBuffer.sampleType) {
    case PixelSampleType::UInt8:
      fillLumaPlane<ReadUInt8>(analysisBuffer, weights, plane.data());
      break;
    case PixelSampleType::UInt16:
      fillLumaPlane<ReadUInt16>(analysisBuffer, weights, plane.data());
      break;
    case PixelSampleType::Float16:
      fillLumaPlane<ReadFloat16>(analysisBuffer, weights, plane.data());
      break;
    case PixelSampleType::Float32:
      fillLumaPlane<ReadFloat32>(analysisBuffer, weights, plane.data());
      break;
    case PixelSampleType::Unknown:
    default:
      break;
  }
  return plane;
}

QVector<qfloat16> packRgbaFloat16(const PixelBuffer &buffer) {
  if (!buffer.isValid()) {
    return {};
  }

  QVector<qfloat16> packed(static_cast<qsizetype>(buffer.width) *
                           static_cast<qsizetype>(buffer.height) * 4);
  switch (buffer.sampleType) {
    case PixelSampleType::UInt8:
      packRgba<ReadUInt8>(buffer, packed.data());
      break;
    case PixelSampleType::UInt16:
      packRgba<ReadUInt16>(buffer, packed.data());
      break;
    case PixelSampleType::Float16:
      packRgba<ReadFloat16>(buffer, packed.data());
      break;
    case PixelSampleType::Float32:
      packRgba<ReadFloat32>(buffer, packed.data());
      break;
    case PixelSampleType::Unknown:
    default:
      return {};
  }
  return packed;
}

}  // namespace heiran::infra
