// 再生しながら貯めるアニメーション GIF (GifStream)。
//
// GIF は先頭から順にしか復号できない。1 周目は順に復号して貯め、2 周目からは貯めたものを伸ばす。
// 一番強い確かめは、全部を展開した結果と画素まで一致すること。

#include "MultiFrameFixtures.hpp"
#include "heiran/infra/GifStream.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

using heiran::infra::FrameStream;
using heiran::infra::gifStoredFrameCountForTesting;
using heiran::infra::openGifStream;
using heiran::infra::probeGif;
using heiran::test::makeGif;
using heiran::test::writeFile;

namespace {

constexpr int kSize = 8;

// 色の番号 (0 赤、1 緑、2 青、3 白) のフレーム列を持つ GIF。
QString writeGif(const QDir &directory, const QString &name, const QVector<int> &colors,
                 const QVector<int> &delaysCs) {
  return writeFile(directory, name, makeGif(kSize, kSize, colors, delaysCs));
}

QColor paletteColor(int index) {
  static const QColor colors[] = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255),
                                  QColor(255, 255, 255)};
  return colors[index];
}

QColor centerColor(const QImage &image) {
  return QColor::fromRgb(image.pixel(image.width() / 2, image.height() / 2));
}

// 順にフレームを求め、届くまで待って集める。
QVector<QImage> collectAll(FrameStream *stream) {
  QVector<QImage> frames;
  for (int index = 0; index < stream->frameCount(); ++index) {
    QImage frame;
    QElapsedTimer timer;
    timer.start();
    while (frame.isNull() && timer.elapsed() < 5000) {
      stream->request(index);
      frame = stream->frameIfReady(index);
      if (frame.isNull()) {
        QTest::qWait(2);
      }
    }
    frames.push_back(frame);
  }
  return frames;
}

class GifStreamTests : public QObject {
  Q_OBJECT

private:
  QTemporaryDir directory_;

  QString gif(const QString &name, const QVector<int> &colors, const QVector<int> &delaysCs) {
    return writeGif(QDir(directory_.path()), name, colors, delaysCs);
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // ---- 走査 ----

  void probesTheFrameCountSizeAndDelays() {
    const auto info = probeGif(gif("probe.gif", {0, 1, 2}, {5, 20, 0}));

    QVERIFY(info.valid);
    QCOMPARE(info.width, kSize);
    QCOMPARE(info.height, kSize);
    QCOMPARE(info.frameCount, 3);
    QCOMPARE(info.delaysMs, (QVector<int>{50, 200, 0}));
    QCOMPARE(info.decodedBytes(), qint64{kSize} * kSize * 4 * 3);
  }

  void rejectsAFileThatIsNotAGif() {
    QVERIFY(!probeGif(writeFile(QDir(directory_.path()), "not.gif", "not a gif at all")).valid);
    QVERIFY(!probeGif(QDir(directory_.path()).filePath("missing.gif")).valid);
  }

  // 途中で切れていても、読めたところまでを数える。落ちない。
  void probesATruncatedFile() {
    const QByteArray whole = makeGif(kSize, kSize, {0, 1, 2}, {5, 5, 5});
    const auto info =
        probeGif(writeFile(QDir(directory_.path()), "cut.gif", whole.left(whole.size() - 30)));
    QVERIFY(info.frameCount < 3);
  }

  // ---- ストリーム ----

  void showsTheFirstFrameRightAfterOpening() {
    const QString path = gif("first.gif", {2, 0, 1}, {11, 11, 11});
    const auto stream = openGifStream(path);

    QVERIFY(stream != nullptr);
    QCOMPARE(stream->frameCount(), 3);
    const QImage first = stream->frameIfReady(0);
    QVERIFY(!first.isNull());
    QCOMPARE(centerColor(first), paletteColor(2));
  }

  void normalizesTheDelays() {
    const auto stream = openGifStream(gif("delay.gif", {0, 1}, {0, 25}));
    QVERIFY(stream != nullptr);
    QCOMPARE(stream->delayMs(0), 100);  // 0 は 100 ms。
    QCOMPARE(stream->delayMs(1), 250);
    QCOMPARE(stream->delayMs(2), 100);  // 巻き戻る。
  }

  void refusesASingleFrameGif() {
    QString error;
    QVERIFY(openGifStream(gif("single.gif", {0}, {10}), &error) == nullptr);
    QVERIFY(!error.isEmpty());
  }

  // 順に求めると、全フレームが正しい色で届くこと。
  void producesEveryFrameInOrder() {
    const QVector<int> colors = {0, 1, 2, 3, 2, 1, 0};
    const auto stream = openGifStream(gif("order.gif", colors, QVector<int>(colors.size(), 11)));
    QVERIFY(stream != nullptr);

    const auto frames = collectAll(stream.get());
    for (int index = 0; index < colors.size(); ++index) {
      QVERIFY2(!frames.at(index).isNull(), qPrintable(QString::number(index)));
      QCOMPARE(centerColor(frames.at(index)), paletteColor(colors.at(index)));
    }
  }

  // 先へ飛んでも、途中を貯めながら追いつくこと。戻れば、復号し直さずに伸ばすだけで済む。
  void storesFramesWhilePlayingAndServesThemAgain() {
    const QVector<int> colors = {0, 1, 2, 3, 0, 1};
    const auto stream = openGifStream(gif("store.gif", colors, QVector<int>(colors.size(), 11)));
    QVERIFY(stream != nullptr);
    QCOMPARE(gifStoredFrameCountForTesting(*stream), 1);  // 開いた直後は 1 コマ目だけ。

    stream->request(5);
    QTRY_VERIFY(!stream->frameIfReady(5).isNull());
    // 5 コマ目へ届くまでに通った 0〜5 が、すべて貯まっている。
    QCOMPARE(gifStoredFrameCountForTesting(*stream), 6);

    stream->request(2);
    QTRY_VERIFY(!stream->frameIfReady(2).isNull());
    QCOMPARE(centerColor(stream->frameIfReady(2)), paletteColor(2));
  }

  // 巻き戻りも同じ。末尾を越えたら先頭が来る。
  void wrapsTheFrameIndex() {
    const auto stream = openGifStream(gif("wrap.gif", {0, 1, 2}, {11, 11, 11}));
    QVERIFY(stream != nullptr);

    stream->request(3);
    QTRY_VERIFY(!stream->frameIfReady(3).isNull());
    QCOMPARE(centerColor(stream->frameIfReady(3)), paletteColor(0));

    // -1 は末尾 (2 コマ目)。
    stream->request(-1);
    QTRY_VERIFY(!stream->frameIfReady(-1).isNull());
    QCOMPARE(centerColor(stream->frameIfReady(-1)), paletteColor(2));
  }

  // 貯められない (上限が小さい) ときでも、結果は変わらないこと。先頭から復号し直して補う。
  void producesTheSameFramesWhenNothingCanBeStored() {
    const QVector<int> colors = {0, 1, 2, 3, 2, 1, 0, 3};
    const QString path = gif("nostore.gif", colors, QVector<int>(colors.size(), 11));
    const auto stream = openGifStream(path, nullptr, /*storeCapBytes=*/1);
    QVERIFY(stream != nullptr);

    // 進む、戻る、飛ぶ。どれも正しい色で届く。
    const int visits[] = {1, 2, 7, 3, 0, 6, 1, 5};
    for (const int index : visits) {
      stream->request(index);
      QTRY_VERIFY2(!stream->frameIfReady(index).isNull(), qPrintable(QString::number(index)));
      QCOMPARE(centerColor(stream->frameIfReady(index)), paletteColor(colors.at(index)));
    }
    QVERIFY(gifStoredFrameCountForTesting(*stream) < static_cast<int>(colors.size()));
  }

  // 全部を展開した結果と、画素まで一致すること。
  void matchesTheFullyExpandedFrames() {
    const QVector<int> colors = {0, 1, 2, 3, 0, 2, 1};
    const QString path = gif("same.gif", colors, QVector<int>(colors.size(), 11));

    heiran::infra::setAnimationStreamingThresholdBytes(1LL << 40);
    const auto eager = heiran::infra::ImageDecoder().decode(path, false);
    heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);
    QVERIFY(eager.ok);
    QVERIFY(eager.decodedImage.frameSequence != nullptr);

    const auto stream = openGifStream(path);
    QVERIFY(stream != nullptr);
    const auto frames = collectAll(stream.get());
    QCOMPARE(frames.size(), eager.decodedImage.frameSequence->frames.size());
    for (int index = 0; index < frames.size(); ++index) {
      const QImage expected =
          eager.decodedImage.frameSequence->frames.at(index).displayImage.convertToFormat(
              QImage::Format_ARGB32);
      QVERIFY2(frames.at(index) == expected, qPrintable(QString::number(index)));
    }
  }

  // Pillow が書いた GIF (LZW 圧縮、透明、差分の矩形、破棄の方法) でも、全部を展開した結果と一致すること。
  void matchesTheFullyExpandedFramesOfRealGifs() {
    const QDir examples(
        QDir(QCoreApplication::applicationDirPath()).filePath("../examples/animation"));
    const QStringList files = examples.entryList({"*.gif"}, QDir::Files);
    if (files.isEmpty()) {
      QSKIP("No sample GIFs under examples/animation (py tools/generate_animation_samples.py).");
    }

    for (const QString &name : files) {
      const QString path = examples.filePath(name);
      heiran::infra::setAnimationStreamingThresholdBytes(1LL << 40);
      const auto eager = heiran::infra::ImageDecoder().decode(path, false);
      heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);
      QVERIFY2(eager.ok, qPrintable(name));
      if (eager.decodedImage.frameSequence == nullptr) {
        continue;
      }

      const auto stream = openGifStream(path);
      QVERIFY2(stream != nullptr, qPrintable(name));
      const auto frames = collectAll(stream.get());
      QCOMPARE(frames.size(), eager.decodedImage.frameSequence->frames.size());
      for (int index = 0; index < frames.size(); ++index) {
        const QImage expected =
            eager.decodedImage.frameSequence->frames.at(index).displayImage.convertToFormat(
                QImage::Format_ARGB32);
        QVERIFY2(frames.at(index) == expected,
                 qPrintable(QString("%1 frame %2").arg(name).arg(index)));
      }
    }
  }

  // フレームを作るたびに、登録した側へ知らせること。外した後は呼ばれない。
  void notifiesListenersAndStopsAfterRemoval() {
    const auto stream = openGifStream(gif("listen.gif", {0, 1, 2, 3}, {11, 11, 11, 11}));
    QVERIFY(stream != nullptr);

    std::atomic<int> calls{0};
    int owner = 0;
    stream->setListener(&owner, [&calls]() { ++calls; });
    stream->request(3);
    QTRY_VERIFY(calls.load() > 0);

    stream->removeListener(&owner);
    const int before = calls.load();
    stream->request(0);
    QTest::qWait(100);
    QCOMPARE(calls.load(), before);
  }

  // 窓の外を捨てること。全部を抱え込まない。
  void keepsOnlyAWindowOfDecodedFrames() {
    const QVector<int> colors(40, 1);
    const auto stream = openGifStream(gif("window.gif", colors, QVector<int>(colors.size(), 11)));
    QVERIFY(stream != nullptr);

    stream->request(30);
    QTRY_VERIFY(!stream->frameIfReady(30).isNull());
    QTRY_VERIFY(stream->residentFrameCountForTesting() <= 10);
    // 貯めたものは残る (伸ばした絵ではなく、圧縮した形で)。
    QVERIFY(gifStoredFrameCountForTesting(*stream) >= 31);
  }
};

}  // namespace

QTEST_MAIN(GifStreamTests)

#include "GifStreamTests.moc"
