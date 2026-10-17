// 圧縮したまま持ち、要るフレームだけを背景で作る APNG (ApngFrameStream)。
//
// 一番強い確かめは、全部を展開した結果と画素まで一致すること。disposal と blend が入り混じった
// 複雑なファイルを、どの順で取り出しても同じになるかを見る。

#include "MultiFrameFixtures.hpp"
#include "heiran/infra/ApngDecoder.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QDir>
#include <QImage>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

using heiran::infra::ApngResult;
using heiran::infra::decodeApng;
using heiran::infra::FrameStream;
using heiran::infra::openApngStream;
using heiran::infra::probeApng;
using heiran::test::ApngFixtureFrame;
using heiran::test::makeApng;
using heiran::test::writeFile;

namespace {

constexpr qint64 kNoLimit = 1LL << 40;
constexpr int kCanvasWidth = 16;
constexpr int kCanvasHeight = 12;

// 位置、大きさ、色 (半透明を含む)、disposal、blend が入り混じったフレーム列。
// 種を固定して、いつも同じものができる。
QVector<ApngFixtureFrame> makeComplexFrames(int frameCount, unsigned seed) {
  QRandomGenerator random(seed);
  QVector<ApngFixtureFrame> frames;
  for (int index = 0; index < frameCount; ++index) {
    ApngFixtureFrame frame;
    // 先頭は全面。後ろは部分的な矩形も混ぜる。
    const bool full = index == 0 || random.bounded(4) == 0;
    const int width = full ? kCanvasWidth : 1 + static_cast<int>(random.bounded(kCanvasWidth - 1));
    const int height =
        full ? kCanvasHeight : 1 + static_cast<int>(random.bounded(kCanvasHeight - 1));
    frame.x = full ? 0 : static_cast<int>(random.bounded(kCanvasWidth - width + 1));
    frame.y = full ? 0 : static_cast<int>(random.bounded(kCanvasHeight - height + 1));
    frame.image = QImage(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        // アルファは 0、半透明、不透明を混ぜる。合成の分岐を全部通す。
        const int alphaKind = static_cast<int>(random.bounded(3));
        const int alpha =
            alphaKind == 0 ? 0 : (alphaKind == 1 ? 1 + static_cast<int>(random.bounded(254)) : 255);
        frame.image.setPixelColor(x, y,
                                  QColor(static_cast<int>(random.bounded(256)),
                                         static_cast<int>(random.bounded(256)),
                                         static_cast<int>(random.bounded(256)), alpha));
      }
    }
    frame.delayNumerator = static_cast<quint16>(1 + random.bounded(20));
    frame.delayDenominator = 100;
    frame.disposeOp = static_cast<quint8>(random.bounded(3));
    frame.blendOp = static_cast<quint8>(random.bounded(2));
    frames.push_back(frame);
  }
  return frames;
}

QImage solid(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  return image;
}

}  // namespace

class ApngStreamTests : public QObject {
  Q_OBJECT

private:
  // 作らせて、作り終えるまで待つ。
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

  QString complexApng(const QString &name, int frameCount, unsigned seed) {
    return writeFile(QDir(directory_.path()), name,
                     makeApng(kCanvasWidth, kCanvasHeight, makeComplexFrames(frameCount, seed)));
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // ---- 見積り ----

  // 展開後の大きさを、画素を読まずに、ヘッダーから求められること。
  void estimatesTheDecodedSizeFromTheHeaderAlone() {
    const QString path = writeFile(
        QDir(directory_.path()), "probe.png",
        makeApng(
            20, 10,
            {{solid(20, 10, Qt::red)}, {solid(20, 10, Qt::green)}, {solid(20, 10, Qt::blue)}}));

    const auto info = probeApng(path);

    QVERIFY(info.animated);
    QCOMPARE(info.width, 20);
    QCOMPARE(info.height, 10);
    QCOMPARE(info.frameCount, 3);
    // RGBA8 で 20 x 10 x 4 が 3 コマ。
    QCOMPARE(info.decodedBytes(), qint64{20 * 10 * 4 * 3});
  }

  void estimatesWhatItCannotOpenAsNotAnimated() {
    const QString plain = QDir(directory_.path()).filePath("plain.png");
    QVERIFY(solid(4, 4, Qt::red).save(plain));
    QVERIFY(!probeApng(plain).animated);
    QVERIFY(!probeApng(QDir(directory_.path()).filePath("missing.png")).animated);
    const QString garbage = writeFile(QDir(directory_.path()), "garbage.png", QByteArray("nope"));
    QVERIFY(!probeApng(garbage).animated);
  }

  // ---- 全展開との一致 ----

  void presentsTheFirstFrameWithoutWaiting() {
    const QString path = complexApng("first.png", 8, 1);
    const ApngResult eager = decodeApng(path, kNoLimit);
    QVERIFY(eager.ok);

    QString error;
    const auto stream = openApngStream(path, &error, 3);

    QVERIFY2(stream != nullptr, qPrintable(error));
    QCOMPARE(stream->frameCount(), 8);
    // 開いた時点で先頭は作り終えている。
    QVERIFY(!stream->frameIfReady(0).isNull());
    QCOMPARE(stream->frameIfReady(0), eager.frames.at(0).image);
  }

  // 前へ進める、後ろへ戻る、飛ぶ、巻き戻る。どの順でも全展開と同じになること。
  // 写しの間隔を 3 にして、写しからの作り直しを何度も通す。
  void matchesTheFullDecodeInEveryOrder() {
    constexpr int kFrames = 14;
    const QString path = complexApng("orders.png", kFrames, 42);
    const ApngResult eager = decodeApng(path, kNoLimit);
    QVERIFY(eager.ok);
    QCOMPARE(eager.frames.size(), kFrames);
    const auto stream = openApngStream(path, nullptr, 3);
    QVERIFY(stream != nullptr);

    const auto check = [&](int index) {
      const QImage frame = waitForFrame(stream, index);
      QVERIFY2(!frame.isNull(), qPrintable(QString("frame %1 was not made").arg(index)));
      QVERIFY2(frame == eager.frames.at(index).image,
               qPrintable(QString("frame %1 differs from the full decode").arg(index)));
    };

    // 前へ。
    for (int index = 0; index < kFrames; ++index) {
      check(index);
    }
    // 後ろへ (コマ戻し)。
    for (int index = kFrames - 1; index >= 0; --index) {
      check(index);
    }
    // 飛ぶ。
    QRandomGenerator random(7);
    for (int step = 0; step < 40; ++step) {
      check(static_cast<int>(random.bounded(kFrames)));
    }
  }

  // 最後のフレームの次は 0 番へ折り返すこと。再生は輪になる。何周しても同じ。
  void loopsAroundTheEnd() {
    constexpr int kFrames = 9;
    const QString path = complexApng("loop.png", kFrames, 5);
    const ApngResult eager = decodeApng(path, kNoLimit);
    QVERIFY(eager.ok);
    const auto stream = openApngStream(path, nullptr, 4);
    QVERIFY(stream != nullptr);

    for (int step = 0; step < kFrames * 3; ++step) {
      const QImage frame = waitForFrame(stream, step);
      QVERIFY2(!frame.isNull(), qPrintable(QString("step %1").arg(step)));
      QVERIFY2(frame == eager.frames.at(step % kFrames).image,
               qPrintable(QString("step %1 differs").arg(step)));
    }
  }

  // 範囲を超えた番号は巻き戻ること。負の値も同じ範囲へ収まる。
  void wrapsIndicesLikeTheEagerFrames() {
    const QString path = complexApng("wrap.png", 6, 9);
    const ApngResult eager = decodeApng(path, kNoLimit);
    const auto stream = openApngStream(path, nullptr, 3);
    QVERIFY(stream != nullptr);

    QCOMPARE(waitForFrame(stream, 6), eager.frames.at(0).image);
    QCOMPARE(waitForFrame(stream, 8), eager.frames.at(2).image);
    QCOMPARE(waitForFrame(stream, -1), eager.frames.at(5).image);
  }

  // 表示時間は全展開と同じで、0 や短い値は 100 ms へ直すこと。
  void keepsTheNormalizedDelays() {
    ApngFixtureFrame first{solid(4, 4, Qt::red)};
    first.delayNumerator = 0;
    first.delayDenominator = 1;
    ApngFixtureFrame second{solid(4, 4, Qt::green)};
    second.delayNumerator = 250;
    second.delayDenominator = 1000;
    const QString path =
        writeFile(QDir(directory_.path()), "delays.png", makeApng(4, 4, {first, second}));

    const auto stream = openApngStream(path);

    QVERIFY(stream != nullptr);
    QCOMPARE(stream->delayMs(0), 100);
    QCOMPARE(stream->delayMs(1), 250);
    QCOMPARE(stream->delayMs(2), 100);  // 巻き戻り
  }

  // ---- メモリ ----

  // 展開済みのフレームは窓の分だけで、全部を抱えないこと。これが逐次展開の目的。
  void keepsOnlyAWindowOfDecodedFrames() {
    constexpr int kFrames = 40;
    const QString path = complexApng("window.png", kFrames, 3);
    const auto stream = openApngStream(path, nullptr, 5);
    QVERIFY(stream != nullptr);

    for (int index = 0; index < kFrames; ++index) {
      QVERIFY(!waitForFrame(stream, index).isNull());
    }

    // 先の 6 と後ろの 2 まで。40 コマ全部ではない。
    QVERIFY2(stream->residentFrameCountForTesting() <= 8,
             qPrintable(QString::number(stream->residentFrameCountForTesting())));
    QVERIFY(stream->residentFrameCountForTesting() > 0);
  }

  // 抱え得るメモリの上限が、全部を展開した大きさより小さいこと (十分に長いアニメーション)。
  void boundsTheResidentMemoryBelowAFullDecode() {
    constexpr int kFrames = 300;
    QVector<ApngFixtureFrame> frames;
    for (int index = 0; index < kFrames; ++index) {
      frames.push_back({solid(64, 64, QColor(index % 256, 0, 0))});
    }
    const QString path = writeFile(QDir(directory_.path()), "long.png", makeApng(64, 64, frames));
    const auto info = probeApng(path);
    QVERIFY(info.animated);

    const auto stream = openApngStream(path);

    QVERIFY(stream != nullptr);
    QVERIFY2(stream->residentBytes() < info.decodedBytes() / 4,
             qPrintable(QString("%1 vs %2").arg(stream->residentBytes()).arg(info.decodedBytes())));
  }

  // ---- 通知 ----

  void notifiesTheListenerWhenFramesAreMade() {
    const QString path = complexApng("notify.png", 10, 2);
    const auto stream = openApngStream(path, nullptr, 3);
    QVERIFY(stream != nullptr);
    std::atomic_int notifications{0};
    stream->setListener(this, [&notifications]() { ++notifications; });

    stream->request(5);

    QTRY_VERIFY_WITH_TIMEOUT(notifications.load() > 0, 5000);
    stream->removeListener(this);
  }

  // 外した後は呼ばれないこと。呼び側が消えた後に呼ばれると、破棄済みのものを触る。
  void doesNotNotifyAfterTheListenerIsRemoved() {
    const QString path = complexApng("removed.png", 10, 4);
    const auto stream = openApngStream(path, nullptr, 3);
    QVERIFY(stream != nullptr);
    std::atomic_int notifications{0};
    stream->setListener(this, [&notifications]() { ++notifications; });
    QVERIFY(!waitForFrame(stream, 3).isNull());
    stream->removeListener(this);
    const int before = notifications.load();

    QVERIFY(!waitForFrame(stream, 8).isNull());
    QTest::qWait(100);

    QCOMPARE(notifications.load(), before);
  }

  // ---- 壊れたファイル ----

  void failsToOpenAGarbageFile() {
    const QString garbage = writeFile(QDir(directory_.path()), "garbage2.png", QByteArray("nope"));
    QString error;
    QVERIFY(openApngStream(garbage, &error) == nullptr);
    QVERIFY(!error.isEmpty());
    QVERIFY(openApngStream(QDir(directory_.path()).filePath("missing.png"), &error) == nullptr);
  }

  // ---- ImageDecoder を通した結果 ----

  // 境目を超えるものだけを、後から作る形にすること。境目は展開後の大きさで決める。
  void decodesLargeAnimationsAsAStream() {
    const QString path = complexApng("decoder.png", 10, 6);
    const ApngResult eager = decodeApng(path, kNoLimit);
    QVERIFY(eager.ok);

    // 境目を 0 にして、後から作る形を通す。
    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto result = heiran::infra::ImageDecoder().decode(path, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const auto &decoded = result.decodedImage;
    QVERIFY(decoded.frameStream != nullptr);
    QVERIFY(decoded.frameSequence == nullptr);
    QCOMPARE(decoded.subImageKind, heiran::infra::SubImageKind::Frames);
    QCOMPARE(decoded.subImageCount, 10);
    QVERIFY(decoded.isAnimation());
    QCOMPARE(decoded.decoderBackend, QString("apng"));
    QVERIFY(decoded.colorSpace.isValid());
    // 結果そのものが先頭のフレーム。
    QCOMPARE(decoded.displayImage, eager.frames.at(0).image);

    // ほかのフレームは、作らせて待てば取り出せて、全展開と同じ。
    decoded.frameStream->request(4);
    std::optional<heiran::infra::DecodedImage> frame;
    QTRY_VERIFY_WITH_TIMEOUT((frame = heiran::infra::tryFrameOf(decoded, 4)).has_value(), 5000);
    QCOMPARE(frame->displayImage, eager.frames.at(4).image);
    QCOMPARE(frame->subImageIndex, 4);
    QCOMPARE(frame->frameDelayMs, heiran::infra::frameDelayMsOf(decoded, 4));
  }

  // 小さなものは今までどおり全部を展開すること。再生の 1 コマごとの費用が要らず、
  // コマ戻しも即座。
  void decodesSmallAnimationsInFull() {
    const QString path = complexApng("small.png", 6, 8);

    const auto result = heiran::infra::ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.frameStream == nullptr);
    QVERIFY(result.decodedImage.frameSequence != nullptr);
  }

  // 1 コマだけの APNG は、大きくても後から作る形にしないこと。動かないため。
  void doesNotStreamASingleFrame() {
    const QString path = writeFile(QDir(directory_.path()), "single.png",
                                   makeApng(20, 20, {{solid(20, 20, Qt::red)}}));

    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto result = heiran::infra::ImageDecoder().decode(path, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);

    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.frameStream == nullptr);
  }

  // サムネイル用の先頭だけの読みは、大きくても後から作る形にしないこと。
  void doesNotStreamWhenOnlyTheFirstFrameIsWanted() {
    const QString path = complexApng("thumb.png", 6, 12);

    heiran::infra::setAnimationStreamingThresholdBytes(0);
    const auto result = heiran::infra::ImageDecoder().decode(path, true, 0, true);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);

    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.frameStream == nullptr);
    QCOMPARE(result.decodedImage.subImageKind, heiran::infra::SubImageKind::None);
  }

private:
  QTemporaryDir directory_;
};

QTEST_MAIN(ApngStreamTests)

#include "ApngStreamTests.moc"
