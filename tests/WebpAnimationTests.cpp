// WebP のアニメーション。APNG と同じ合成処理 (AnimationComposer) を通す。
//
// 一番強い確かめは、元の画像が画素まで再現されること。ロスレスで符号化すれば、デコードした結果は
// 入力したフレームと一致するはず。エンコーダは、フレームの差分を部分的な矩形と重ね方の指定へ
// 直して書くため、その合成が正しくなければ一致しない。

#include "heiran/infra/AnimationComposer.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/infra/WebpDecoder.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <webp/encode.h>
#include <webp/mux.h>

using heiran::infra::composeAllFrames;
using heiran::infra::FrameStream;
using heiran::infra::openAnimationStream;
using heiran::infra::openWebpSource;

namespace {

constexpr qint64 kNoLimit = 1LL << 40;
constexpr int kWidth = 24;
constexpr int kHeight = 18;

// 前のフレームから、一部の矩形だけを変えていく。エンコーダが部分的なフレームと重ね方を選べる。
// 色は、アルファが 0、半透明、不透明のものを混ぜる。
QVector<QImage> makeFrames(int count, unsigned seed) {
  QRandomGenerator random(seed);
  const auto randomColor = [&random]() {
    const int kind = static_cast<int>(random.bounded(3));
    const int alpha = kind == 0 ? 0 : (kind == 1 ? 1 + static_cast<int>(random.bounded(254)) : 255);
    return QColor(static_cast<int>(random.bounded(256)), static_cast<int>(random.bounded(256)),
                  static_cast<int>(random.bounded(256)), alpha);
  };

  QVector<QImage> frames;
  QImage current(kWidth, kHeight, QImage::Format_RGBA8888);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      current.setPixelColor(x, y, randomColor());
    }
  }
  frames.push_back(current);
  for (int index = 1; index < count; ++index) {
    current = current.copy();
    const int width = 1 + static_cast<int>(random.bounded(kWidth / 2));
    const int height = 1 + static_cast<int>(random.bounded(kHeight / 2));
    const int left = static_cast<int>(random.bounded(kWidth - width + 1));
    const int top = static_cast<int>(random.bounded(kHeight - height + 1));
    for (int y = top; y < top + height; ++y) {
      for (int x = left; x < left + width; ++x) {
        current.setPixelColor(x, y, randomColor());
      }
    }
    frames.push_back(current);
  }
  return frames;
}

// ロスレスのアニメーション WebP。透明な画素の色も保つ (exact)。
QByteArray encodeAnimation(const QVector<QImage> &frames, const QVector<int> &timestampsMs,
                           int endTimestampMs) {
  WebPAnimEncoderOptions encoderOptions;
  if (!WebPAnimEncoderOptionsInit(&encoderOptions)) {
    return {};
  }
  WebPAnimEncoder *encoder = WebPAnimEncoderNew(kWidth, kHeight, &encoderOptions);
  if (encoder == nullptr) {
    return {};
  }

  WebPConfig config;
  WebPConfigInit(&config);
  config.lossless = 1;
  config.exact = 1;

  for (int index = 0; index < frames.size(); ++index) {
    WebPPicture picture;
    WebPPictureInit(&picture);
    picture.use_argb = 1;
    picture.width = kWidth;
    picture.height = kHeight;
    WebPPictureImportRGBA(&picture, frames.at(index).constBits(),
                          static_cast<int>(frames.at(index).bytesPerLine()));
    WebPAnimEncoderAdd(encoder, &picture, timestampsMs.at(index), &config);
    WebPPictureFree(&picture);
  }
  WebPAnimEncoderAdd(encoder, nullptr, endTimestampMs, nullptr);

  WebPData data;
  WebPDataInit(&data);
  const bool assembled = WebPAnimEncoderAssemble(encoder, &data) != 0;
  WebPAnimEncoderDelete(encoder);
  if (!assembled) {
    return {};
  }
  const QByteArray bytes(reinterpret_cast<const char *>(data.bytes),
                         static_cast<qsizetype>(data.size));
  WebPDataClear(&data);
  return bytes;
}

QString writeFile(const QDir &directory, const QString &name, const QByteArray &bytes) {
  const QString path = directory.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write(bytes);
  return path;
}

}  // namespace

class WebpAnimationTests : public QObject {
  Q_OBJECT

private:
  static QImage waitForFrame(const std::shared_ptr<FrameStream> &stream, int index) {
    stream->request(index);
    QImage frame;
    QTest::qWaitFor(
        [&]() {
          frame = stream->frameIfReady(index);
          return !frame.isNull();
        },
        10000);
    return frame;
  }

  // 元の画像とタイムスタンプから WebP を書き、元の画像を返す。
  QString writeAnimation(const QString &name, int frameCount, unsigned seed,
                         QVector<QImage> *originals) {
    *originals = makeFrames(frameCount, seed);
    QVector<int> timestamps;
    for (int index = 0; index < frameCount; ++index) {
      timestamps.push_back(index * 100);
    }
    return writeFile(QDir(directory_.path()), name,
                     encodeAnimation(*originals, timestamps, frameCount * 100));
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // 全部を展開した結果が、元の画像と画素まで一致すること。
  void reproducesTheOriginalFramesWhenDecodedInFull() {
    QVector<QImage> originals;
    const QString path = writeAnimation("full.webp", 12, 1, &originals);
    QVERIFY(!path.isEmpty());

    QString error;
    const auto source = openWebpSource(path, &error);
    QVERIFY2(source != nullptr, qPrintable(error));
    QCOMPARE(source->frameCount(), 12);
    QCOMPARE(source->canvasWidth(), kWidth);
    QCOMPARE(source->canvasHeight(), kHeight);

    const auto result = composeAllFrames(*source, kNoLimit);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.frames.size(), 12);
    for (int index = 0; index < 12; ++index) {
      QVERIFY2(result.frames.at(index).image == originals.at(index),
               qPrintable(QString("frame %1 differs from the original").arg(index)));
    }
  }

  // 逐次展開の結果も、元の画像と一致すること。前進、後退、飛び越し、折り返しのどの順でも。
  void reproducesTheOriginalFramesWhenStreamedInAnyOrder() {
    constexpr int kFrames = 14;
    QVector<QImage> originals;
    const QString path = writeAnimation("stream.webp", kFrames, 2, &originals);
    const auto source = openWebpSource(path);
    QVERIFY(source != nullptr);
    const auto stream = openAnimationStream(source, nullptr, 3);
    QVERIFY(stream != nullptr);

    const auto check = [&](int index) {
      const QImage frame = waitForFrame(stream, index);
      QVERIFY2(!frame.isNull(), qPrintable(QString("frame %1 was not made").arg(index)));
      QVERIFY2(frame == originals.at(((index % kFrames) + kFrames) % kFrames),
               qPrintable(QString("frame %1 differs").arg(index)));
    };
    for (int index = 0; index < kFrames; ++index) {
      check(index);
    }
    for (int index = kFrames - 1; index >= 0; --index) {
      check(index);
    }
    QRandomGenerator random(11);
    for (int step = 0; step < 40; ++step) {
      check(static_cast<int>(random.bounded(kFrames)));
    }
    // 折り返し。
    for (int index = kFrames - 2; index < kFrames * 2 + 2; ++index) {
      check(index);
    }
  }

  // 表示時間は、次のフレームまでの間隔になること。
  void readsTheFrameDelays() {
    const QVector<QImage> frames = makeFrames(3, 3);
    const QString path = writeFile(QDir(directory_.path()), "delays.webp",
                                   encodeAnimation(frames, {0, 100, 350}, 700));
    const auto source = openWebpSource(path);
    QVERIFY(source != nullptr);

    QCOMPARE(source->control(0).delayMs, 100);
    QCOMPARE(source->control(1).delayMs, 250);
    QCOMPARE(source->control(2).delayMs, 350);
  }

  // 静止画は 1 フレームとして読めること。
  void readsAStillImage() {
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    image.fill(QColor(10, 200, 30, 255));
    uint8_t *encoded = nullptr;
    const size_t size = WebPEncodeLosslessRGBA(image.constBits(), image.width(), image.height(),
                                               static_cast<int>(image.bytesPerLine()), &encoded);
    QVERIFY(size > 0);
    const QString path = writeFile(
        QDir(directory_.path()), "still.webp",
        QByteArray(reinterpret_cast<const char *>(encoded), static_cast<qsizetype>(size)));
    WebPFree(encoded);

    const auto source = openWebpSource(path);

    QVERIFY(source != nullptr);
    QCOMPARE(source->frameCount(), 1);
    const auto result = composeAllFrames(*source, kNoLimit);
    QVERIFY(result.ok);
    QCOMPARE(result.frames.at(0).image.pixelColor(0, 0), QColor(10, 200, 30));
  }

  void failsToOpenAGarbageFile() {
    QString error;
    const QString path = writeFile(QDir(directory_.path()), "garbage.webp", QByteArray("nope"));
    QVERIFY(openWebpSource(path, &error) == nullptr);
    QVERIFY(!error.isEmpty());
    QVERIFY(openWebpSource(QDir(directory_.path()).filePath("missing.webp")) == nullptr);
  }

  // 展開後の大きさは、画素を復号せずに求まること。
  void estimatesTheDecodedSizeWithoutDecoding() {
    QVector<QImage> originals;
    const QString path = writeAnimation("estimate.webp", 5, 4, &originals);

    const auto source = openWebpSource(path);

    QVERIFY(source != nullptr);
    QCOMPARE(source->decodedBytes(), qint64{kWidth} * kHeight * 4 * 5);
  }

  // ---- ImageDecoder を通した結果 ----

  // 境目を超えるものだけを、後から作る形にすること。APNG と同じ扱い。
  void streamsLargeAnimationsThroughTheImageDecoder() {
    QVector<QImage> originals;
    const QString path = writeAnimation("decoder.webp", 10, 5, &originals);

    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto result = heiran::infra::ImageDecoder().decode(path, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const auto &decoded = result.decodedImage;
    QVERIFY(decoded.frameStream != nullptr);
    QCOMPARE(decoded.subImageKind, heiran::infra::SubImageKind::Frames);
    QCOMPARE(decoded.subImageCount, 10);
    QCOMPARE(decoded.decoderBackend, QString("webp"));
    QCOMPARE(decoded.displayImage, originals.at(0));

    decoded.frameStream->request(6);
    std::optional<heiran::infra::DecodedImage> frame;
    QTRY_VERIFY_WITH_TIMEOUT((frame = heiran::infra::tryFrameOf(decoded, 6)).has_value(), 5000);
    QCOMPARE(frame->displayImage, originals.at(6));
  }

  // 小さなものは全部を展開すること。
  void decodesSmallAnimationsInFull() {
    QVector<QImage> originals;
    const QString path = writeAnimation("small.webp", 6, 6, &originals);

    const auto result = heiran::infra::ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.frameStream == nullptr);
    QVERIFY(result.decodedImage.frameSequence != nullptr);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 4).displayImage, originals.at(4));
  }

  // 先頭だけを求められたら、大きくても後から作る形にしないこと。サムネイルのため。
  void doesNotStreamWhenOnlyTheFirstFrameIsWanted() {
    QVector<QImage> originals;
    const QString path = writeAnimation("thumb.webp", 6, 7, &originals);

    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto result = heiran::infra::ImageDecoder().decode(path, true, 0, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);

    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.frameStream == nullptr);
    QCOMPARE(result.decodedImage.subImageKind, heiran::infra::SubImageKind::None);
    QCOMPARE(result.decodedImage.displayImage, originals.at(0));
  }

private:
  QTemporaryDir directory_;
};

QTEST_MAIN(WebpAnimationTests)

#include "WebpAnimationTests.moc"
