// examples/animation の見本 (tools/generate_animation_samples.py が Pillow で書いたもの) を読む。
//
// ほかのアニメーションのテストは、自分でエンコードして読み戻す。ここは、別の書き手のファイル
// (LZW 圧縮、差分の矩形、破棄の方法、透明) を、絵の中身が正しく出るかで確かめる。
// 絵はスクリプトが決めた式で描かれているため、球の位置と色を、同じ式から求めて照らす。

#include "heiran/infra/ImageDecoder.hpp"

#include <QDir>
#include <QImage>
#include <QtTest>

#include <cmath>
#include <optional>

namespace {

constexpr int kFrameCount = 12;
constexpr int kWidth = 160;
constexpr int kHeight = 120;

struct Sample {
  const char *file;
  bool transparentBackground;
  int tolerance;  // GIF はパレットへ減色されるため、色の幅を許す。
};

const Sample kSamples[] = {
    {"bounce-transparent.gif", true, 24}, {"bounce-optimized.gif", false, 24},
    {"bounce-previous.gif", false, 24},   {"bounce.png", true, 0},
    {"bounce.webp", true, 0},
};

QString samplePath(const QString &file) {
  return QDir(HEIRAN_ANIMATION_SAMPLE_DIR).filePath(file);
}

// 生成スクリプトの ball_frame と同じ式。
QPoint ballCenter(int index) {
  const int x = 20 + (kWidth - 40) * index / (kFrameCount - 1);
  const int y = 30 + static_cast<int>(
                         50 * std::abs(((index * 2) % kFrameCount) / double(kFrameCount) - 0.5) * 2);
  return {x, y};
}

QColor ballColor(int index) { return QColor(230, 70 + index * 12, 60); }

bool near(const QColor &actual, const QColor &expected, int tolerance) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

}  // namespace

class AnimationSamplesTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void init() {
    if (!QFileInfo::exists(samplePath("bounce.png"))) {
      QSKIP("No samples (py tools/generate_animation_samples.py).");
    }
  }

  // どの形式でも、全部を展開した各コマで、球が式の位置と色にあり、背景が正しいこと。
  void drawsTheBallWhereTheScriptPutIt_data() {
    QTest::addColumn<int>("sampleIndex");
    for (int index = 0; index < static_cast<int>(std::size(kSamples)); ++index) {
      QTest::newRow(kSamples[index].file) << index;
    }
  }

  void drawsTheBallWhereTheScriptPutIt() {
    QFETCH(int, sampleIndex);
    const Sample &sample = kSamples[sampleIndex];

    heiran::infra::setAnimationStreamingThresholdBytes(1LL << 40);
    const auto result = heiran::infra::ImageDecoder().decode(samplePath(sample.file), true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.subImageCount, kFrameCount);

    for (int index = 0; index < kFrameCount; ++index) {
      const QImage frame = heiran::infra::frameOf(result.decodedImage, index)
                               .displayImage.convertToFormat(QImage::Format_ARGB32);
      QCOMPARE(frame.size(), QSize(kWidth, kHeight));
      const QPoint center = ballCenter(index);
      const QColor inside = frame.pixelColor(center);
      QVERIFY2(inside.alpha() == 255 && near(inside, ballColor(index), sample.tolerance),
               qPrintable(QString("%1 frame %2 ball: %3")
                              .arg(sample.file)
                              .arg(index)
                              .arg(inside.name(QColor::HexArgb))));
      // 前のコマの球は、消えていること (破棄と差分の矩形が正しくないと、残る)。
      if (index > 0) {
        const QPoint previous = ballCenter(index - 1);
        if (std::abs(previous.x() - center.x()) > 30 || std::abs(previous.y() - center.y()) > 30) {
          const QColor old = frame.pixelColor(previous);
          QVERIFY2(!near(old, ballColor(index - 1), 4) || old.alpha() == 0,
                   qPrintable(QString("%1 frame %2 keeps the previous ball").arg(sample.file).arg(index)));
        }
      }
      const QColor corner = frame.pixelColor(0, 0);
      if (sample.transparentBackground) {
        QCOMPARE(corner.alpha(), 0);
      } else {
        QCOMPARE(corner.alpha(), 255);
      }
    }
  }

  // 逐次展開 (後から作る形) の結果が、全部を展開した結果と画素まで一致すること。
  void streamedFramesMatchTheFullyExpandedFrames_data() { drawsTheBallWhereTheScriptPutIt_data(); }

  void streamedFramesMatchTheFullyExpandedFrames() {
    QFETCH(int, sampleIndex);
    const QString path = samplePath(kSamples[sampleIndex].file);

    heiran::infra::setAnimationStreamingThresholdBytes(1LL << 40);
    const auto eager = heiran::infra::ImageDecoder().decode(path, true);
    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto streamed = heiran::infra::ImageDecoder().decode(path, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);
    QVERIFY(eager.ok);
    QVERIFY(streamed.ok);
    QVERIFY2(streamed.decodedImage.frameStream != nullptr, "the file was not streamed");

    // 飛び飛びの順で求めても、同じになる。
    const int order[] = {0, 5, 11, 3, 4, 1, 10, 2, 9, 6, 8, 7};
    for (const int index : order) {
      streamed.decodedImage.frameStream->request(index);
      std::optional<heiran::infra::DecodedImage> frame;
      QTRY_VERIFY_WITH_TIMEOUT(
          (frame = heiran::infra::tryFrameOf(streamed.decodedImage, index)).has_value(), 5000);
      const QImage expected = heiran::infra::frameOf(eager.decodedImage, index).displayImage;
      QVERIFY2(frame->displayImage.convertToFormat(QImage::Format_ARGB32) ==
                   expected.convertToFormat(QImage::Format_ARGB32),
               qPrintable(QString("%1 frame %2").arg(kSamples[sampleIndex].file).arg(index)));
    }
  }
};

QTEST_MAIN(AnimationSamplesTests)
#include "AnimationSamplesTests.moc"
