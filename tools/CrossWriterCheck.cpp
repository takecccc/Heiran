// 別の書き手 (ImageMagick、ffmpeg) が作ったファイルを復号して、期待値と比べる。
//
//   heiran_cross_writer_check <manifest.tsv> [orient]
//
// manifest は `tools/generate_cross_writer_samples.py` が作る。1 行に、タブ区切りで
// 名前 / ファイル / 幅 / 高さ / 許容差 / 期待値 (float32 RGBA、行は上から) / ページ。
// 許容差を超えた画素があるケースを数え、1 つでもあれば終了コードを 1 にする。
// `orient` を付けると、EXIF の向きを適用して読む。

#include "heiran/infra/ImageDecoder.hpp"

#include <QCoreApplication>
#include <QFile>
#include <cmath>
#include <cstdio>

using namespace heiran;

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    std::fprintf(stderr, "usage: heiran_cross_writer_check <manifest.tsv> [orient]\n");
    return 2;
  }
  QFile manifest(QString::fromLocal8Bit(argv[1]));
  if (!manifest.open(QIODevice::ReadOnly)) {
    std::fprintf(stderr, "cannot open the manifest\n");
    return 2;
  }
  const bool applyOrientation = argc > 2;

  int problems = 0;
  int total = 0;
  while (!manifest.atEnd()) {
    const QStringList fields = QString::fromUtf8(manifest.readLine()).trimmed().split('\t');
    if (fields.size() < 7) {
      continue;
    }
    ++total;
    const QString name = fields[0];
    const int width = fields[2].toInt();
    const int height = fields[3].toInt();
    const double tolerance = fields[4].toDouble();

    QByteArray expectedBytes;
    if (QFile reference(fields[5]); reference.open(QIODevice::ReadOnly)) {
      expectedBytes = reference.readAll();
    }
    const auto result =
        infra::ImageDecoder().decode(fields[1], applyOrientation, fields[6].toInt());
    const auto &image = result.decodedImage;
    if (!result.ok || image.size().isEmpty()) {
      std::printf("FAIL %-28s decode failed: %s\n", qPrintable(name),
                  qPrintable(result.errorMessage));
      ++problems;
      continue;
    }
    if (image.size() != QSize(width, height) ||
        expectedBytes.size() != static_cast<qsizetype>(width) * height * 16) {
      std::printf("FAIL %-28s size %dx%d (expected %dx%d)\n", qPrintable(name),
                  image.size().width(), image.size().height(), width, height);
      ++problems;
      continue;
    }

    const auto *expected = reinterpret_cast<const float *>(expectedBytes.constData());
    double maxRgb = 0.0;
    double maxAlpha = 0.0;
    double sum = 0.0;
    int worstX = 0;
    int worstY = 0;
    int worstChannel = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        for (int channel = 0; channel < 4; ++channel) {
          const double diff =
              std::abs(static_cast<double>(image.sampleRgba(x, y, channel)) -
                       expected[(static_cast<size_t>(y) * width + x) * 4 + channel]);
          if (channel < 3) {
            if (diff > maxRgb) {
              worstX = x;
              worstY = y;
              worstChannel = channel;
            }
            maxRgb = std::max(maxRgb, diff);
            sum += diff;
          } else {
            maxAlpha = std::max(maxAlpha, diff);
          }
        }
      }
    }
    const bool ok = maxRgb <= tolerance && maxAlpha <= tolerance;
    std::printf("%s %-28s %-5s type=%d ch=%d maxRGB=%.5f maxA=%.5f mean=%.6f (tol %.5f)\n",
                ok ? "ok  " : "DIFF", qPrintable(name), qPrintable(image.decoderBackend),
                static_cast<int>(image.sourceSampleType), image.sourceChannelCount, maxRgb,
                maxAlpha, sum / (static_cast<double>(width) * height * 3), tolerance);
    if (!ok) {
      // 一番ずれた画素。原因の手掛かりにする。
      std::printf("       worst at (%d, %d) channel %d: decoded %.5f, expected %.5f\n", worstX, worstY,
                  worstChannel, static_cast<double>(image.sampleRgba(worstX, worstY, worstChannel)),
                  static_cast<double>(
                      expected[(static_cast<size_t>(worstY) * width + worstX) * 4 + worstChannel]));
    }
    problems += ok ? 0 : 1;
  }
  std::printf("%d case(s), %d problem(s)\n", total, problems);
  return problems == 0 ? 0 : 1;
}
