#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/DisplayTransform.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QTextStream>
#include <algorithm>
#include <numeric>

namespace {

QString sampleTypeName(heiran::infra::PixelSampleType sampleType) {
  switch (sampleType) {
    case heiran::infra::PixelSampleType::UInt8:
      return QStringLiteral("uint8");
    case heiran::infra::PixelSampleType::UInt16:
      return QStringLiteral("uint16");
    case heiran::infra::PixelSampleType::Float16:
      return QStringLiteral("half");
    case heiran::infra::PixelSampleType::Float32:
      return QStringLiteral("float");
    case heiran::infra::PixelSampleType::Unknown:
    default:
      return QStringLiteral("unknown");
  }
}

double toMilliseconds(qint64 nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);

  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.size() < 2) {
    out << "usage: heiran_decode_benchmark <image-file> [iterations]\n";
    out << "  The backend is selected with HEIRAN_IMAGE_DECODER (auto|qt|spng).\n";
    return 1;
  }

  const QString filePath = arguments.at(1);
  if (!QFileInfo::exists(filePath)) {
    out << "file not found: " << filePath << "\n";
    return 1;
  }

  const int iterations = arguments.size() >= 3 ? std::max(1, arguments.at(2).toInt()) : 10;

  const heiran::infra::ImageDecoder decoder;

  // 初回はファイルキャッシュの影響が大きいため計測から外す。
  const auto warmup = decoder.decode(filePath, true);
  if (!warmup.ok) {
    out << "decode failed: " << warmup.errorMessage << "\n";
    return 1;
  }

  QVector<qint64> durations;
  durations.reserve(iterations);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    QElapsedTimer timer;
    timer.start();
    const auto result = decoder.decode(filePath, true);
    const qint64 elapsedNs = timer.nsecsElapsed();
    if (!result.ok) {
      out << "decode failed: " << result.errorMessage << "\n";
      return 1;
    }
    durations.push_back(elapsedNs);
  }

  std::sort(durations.begin(), durations.end());
  const qint64 totalNs = std::accumulate(durations.begin(), durations.end(), qint64{0});

  const auto &decodedImage = warmup.decodedImage;
  out << "file       : " << QFileInfo(filePath).fileName() << "\n";
  out << "size       : " << decodedImage.size().width() << "x" << decodedImage.size().height()
      << "\n";
  out << "backend    : " << decodedImage.decoderBackend << "\n";
  out << "source     : " << sampleTypeName(decodedImage.sourceSampleType) << " x"
      << decodedImage.sourceChannelCount << "\n";
  out << "analysis   : "
      << (decodedImage.hasAnalysisBuffer() ? sampleTypeName(decodedImage.analysisBuffer.sampleType)
                                           : QStringLiteral("(display image)"))
      << "\n";
  // 全画素のチェックサム。backend 間で一致するかを確認するために出す。
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (int y = 0; y < decodedImage.size().height(); ++y) {
    for (int x = 0; x < decodedImage.size().width(); ++x) {
      for (int channel = 0; channel < 4; ++channel) {
        const float value = decodedImage.sampleRgba(x, y, channel);
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(&value), sizeof(value)));
      }
    }
  }
  out << "checksum   : " << hash.result().toHex().left(16) << Qt::endl;

  const int centerX = decodedImage.size().width() / 2;
  const int centerY = decodedImage.size().height() / 2;
  out << Qt::fixed << qSetRealNumberPrecision(4);
  out << "center px  : ";
  for (int channel = 0; channel < 4; ++channel) {
    out << decodedImage.sampleRgba(centerX, centerY, channel) << " ";
  }
  out << "at (" << centerX << ", " << centerY << ")" << Qt::endl;
  out << "iterations : " << iterations << "\n";
  out << qSetRealNumberPrecision(2);
  out << "min        : " << toMilliseconds(durations.first()) << " ms\n";
  out << "median     : " << toMilliseconds(durations.at(durations.size() / 2)) << " ms\n";
  out << "mean       : " << toMilliseconds(totalNs / durations.size()) << " ms\n";
  out << "max        : " << toMilliseconds(durations.last()) << " ms\n";

  // 間引きデコードの効き方。表示に必要な解像度だけを読めば速くなるかを見る。
  // `QImageReader::setScaledSize()` は形式によって扱いが違う。JPEG は DCT の
  // 段階で間引けるが、PNG は全画素を読んでから縮小するだけになる。
  for (const int divisor : {2, 4, 8}) {
    const QSize scaledSize(std::max(1, decodedImage.size().width() / divisor),
                           std::max(1, decodedImage.size().height() / divisor));
    QVector<qint64> scaledDurations;
    bool scaledOk = true;
    for (int iteration = 0; iteration < iterations && scaledOk; ++iteration) {
      QImageReader reader(filePath);
      reader.setScaledSize(scaledSize);
      QElapsedTimer timer;
      timer.start();
      const QImage image = reader.read();
      scaledDurations.push_back(timer.nsecsElapsed());
      scaledOk = !image.isNull();
    }
    if (!scaledOk || scaledDurations.isEmpty()) {
      out << "scaled 1/" << divisor << "  : not supported by the Qt reader\n";
      continue;
    }
    std::sort(scaledDurations.begin(), scaledDurations.end());
    out << "scaled 1/" << divisor << "  : "
        << toMilliseconds(scaledDurations.at(scaledDurations.size() / 2)) << " ms (median, "
        << scaledSize.width() << "x" << scaledSize.height() << ")\n";
  }

  // 比べる相手として、間引かずに Qt で読んだ場合も測る。
  {
    QVector<qint64> plainDurations;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      QImageReader reader(filePath);
      QElapsedTimer timer;
      timer.start();
      const QImage image = reader.read();
      plainDurations.push_back(timer.nsecsElapsed());
      if (image.isNull()) {
        plainDurations.clear();
        break;
      }
    }
    if (!plainDurations.isEmpty()) {
      std::sort(plainDurations.begin(), plainDurations.end());
      out << "qt full    : " << toMilliseconds(plainDurations.at(plainDurations.size() / 2))
          << " ms (median)\n";
    }
  }

  // 表示変換のコスト。デコードはページごとに 1 回だが、
  // 表示変換はガンマなどを変えるたびに走るため、別に測る。
  const heiran::ui::DisplayTransformParams params{
      .channelMode = heiran::ui::ChannelMode::Color,
      .gamma = 2.2,
  };
  QVector<qint64> transformDurations;
  transformDurations.reserve(iterations);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    QElapsedTimer transformTimer;
    transformTimer.start();
    const QImage transformed = heiran::ui::renderDisplayImageOnCpu(warmup.decodedImage, params);
    transformDurations.push_back(transformTimer.nsecsElapsed());
    if (transformed.isNull()) {
      out << "display transform failed" << Qt::endl;
      return 1;
    }
  }
  std::sort(transformDurations.begin(), transformDurations.end());
  out << "transform  : " << toMilliseconds(transformDurations.at(transformDurations.size() / 2))
      << " ms (gamma only, median)" << Qt::endl;

  return 0;
}
