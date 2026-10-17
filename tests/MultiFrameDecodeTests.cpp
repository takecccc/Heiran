#include "MultiFrameFixtures.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#if defined(HEIRAN_HAVE_LIBWEBP)
#include <webp/encode.h>
#include <webp/mux.h>
#endif

using heiran::infra::DecodedImage;
using heiran::infra::ImageDecoder;
using heiran::infra::SubImageKind;
using heiran::test::makeGif;
using heiran::test::makeTiff;
using heiran::test::writeFile;

namespace {

QColor pixelOf(const DecodedImage &decoded, int x = 0, int y = 0) {
  return decoded.displayImage.pixelColor(x, y);
}

}  // namespace

class MultiFrameDecodeTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // ---- GIF (フレーム) ----

  void decodesEveryFrameOfAnAnimatedGif() {
    const QString path =
        writeFile(QDir(directory_.path()), "anim.gif", makeGif(8, 6, {0, 1, 2}, {3, 5, 20}));
    QVERIFY(!path.isEmpty());

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const DecodedImage &decoded = result.decodedImage;
    QCOMPARE(decoded.subImageKind, SubImageKind::Frames);
    QCOMPARE(decoded.subImageCount, 3);
    QVERIFY(decoded.isAnimation());
    QCOMPARE(decoded.displayImage.size(), QSize(8, 6));

    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 0)), QColor(255, 0, 0));
    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 1)), QColor(0, 255, 0));
    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 2)), QColor(0, 0, 255));
  }

  // 表示時間はフレームごとに持つこと。ミリ秒へ直して渡す。
  void keepsTheDelayOfEachFrame() {
    const QString path =
        writeFile(QDir(directory_.path()), "delay.gif", makeGif(4, 4, {0, 1, 2}, {30, 50, 200}));

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 0).frameDelayMs, 300);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 1).frameDelayMs, 500);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 2).frameDelayMs, 2000);
  }

  // 0 や極端に短い時間は 100 ms として扱うこと。ツールが 0 を「既定」の意味で書く。
  // そのまま従うと、目で追えない速さで回る。
  void treatsAZeroDelayAsTheBrowsersDo() {
    const QString path =
        writeFile(QDir(directory_.path()), "zero.gif", makeGif(4, 4, {0, 1}, {0, 1}));

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 0).frameDelayMs, 100);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 1).frameDelayMs, 100);
  }

  // 結果そのものが先頭のフレームと同じ内容であること。フレームを意識しない側が
  // そのまま先頭を表示できる。
  void makesTheResultItselfTheFirstFrame() {
    const QString path =
        writeFile(QDir(directory_.path()), "first.gif", makeGif(4, 4, {2, 0}, {10, 10}));

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(pixelOf(result.decodedImage), QColor(0, 0, 255));
    QCOMPARE(result.decodedImage.subImageIndex, 0);
  }

  // フレームが 1 枚の GIF は普通の画像として扱うこと。アニメーションとして再生しない。
  void treatsASingleFrameGifAsAnOrdinaryImage() {
    const QString path = writeFile(QDir(directory_.path()), "still.gif", makeGif(4, 4, {1}, {10}));

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
    QCOMPARE(result.decodedImage.subImageCount, 1);
    QVERIFY(!result.decodedImage.isAnimation());
    QVERIFY(result.decodedImage.frameSequence == nullptr);
  }

  // 範囲を超えた番号は先頭へ巻き戻ること。負の値も同じ範囲へ収まる。
  void wrapsFrameIndices() {
    const QString path =
        writeFile(QDir(directory_.path()), "wrap.gif", makeGif(4, 4, {0, 1, 2}, {10, 10, 10}));
    const auto result = ImageDecoder().decode(path, true);
    QVERIFY(result.ok);

    QCOMPARE(pixelOf(heiran::infra::frameOf(result.decodedImage, 3)), QColor(255, 0, 0));
    QCOMPARE(pixelOf(heiran::infra::frameOf(result.decodedImage, 5)), QColor(0, 0, 255));
    QCOMPARE(pixelOf(heiran::infra::frameOf(result.decodedImage, -1)), QColor(0, 0, 255));
  }

  // アニメーションでない画像では、そのまま返すこと。
  void returnsTheImageItselfForANonAnimation() {
    QImage image(6, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString path = QDir(directory_.path()).filePath("plain.png");
    QVERIFY(image.save(path));
    const auto result = ImageDecoder().decode(path, true);
    QVERIFY(result.ok);

    const DecodedImage frame = heiran::infra::frameOf(result.decodedImage, 7);

    QCOMPARE(frame.displayImage.size(), QSize(6, 4));
  }

  // 色や向きはファイル単位で、全フレームへ写ること。フレームを取り出した側が
  // 別に読まなくてよい。
  void sharesFileLevelInformationWithEveryFrame() {
    const QString path =
        writeFile(QDir(directory_.path()), "meta.gif", makeGif(4, 4, {0, 1}, {10, 10}));
    const auto result = ImageDecoder().decode(path, true);
    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.colorSpace.isValid());

    for (int index = 0; index < 2; ++index) {
      const DecodedImage frame = heiran::infra::frameOf(result.decodedImage, index);
      QVERIFY(frame.colorSpace.isValid());
      QCOMPARE(frame.colorSpace, result.decodedImage.colorSpace);
      QCOMPARE(frame.subImageIndex, index);
      QCOMPARE(frame.subImageCount, 2);
    }
  }

  // ---- TIFF (ページ) ----

  void decodesTheRequestedPageOfAMultiPageTiff() {
    const QString path = writeFile(QDir(directory_.path()), "pages.tif",
                                   makeTiff({{QSize(10, 8), QColor(255, 0, 0)},
                                             {QSize(20, 10), QColor(0, 255, 0)},
                                             {QSize(6, 6), QColor(0, 0, 255)}}));
    QVERIFY(!path.isEmpty());

    const auto first = ImageDecoder().decode(path, true, 0);
    const auto second = ImageDecoder().decode(path, true, 1);
    const auto third = ImageDecoder().decode(path, true, 2);

    QVERIFY2(first.ok, qPrintable(first.errorMessage));
    QVERIFY2(second.ok, qPrintable(second.errorMessage));
    QVERIFY2(third.ok, qPrintable(third.errorMessage));
    // ページごとに大きさも色も違う。
    QCOMPARE(first.decodedImage.displayImage.size(), QSize(10, 8));
    QCOMPARE(second.decodedImage.displayImage.size(), QSize(20, 10));
    QCOMPARE(third.decodedImage.displayImage.size(), QSize(6, 6));
    QCOMPARE(pixelOf(first.decodedImage), QColor(255, 0, 0));
    QCOMPARE(pixelOf(second.decodedImage), QColor(0, 255, 0));
    QCOMPARE(pixelOf(third.decodedImage), QColor(0, 0, 255));
  }

  // 何ページ中の何ページ目かを結果が持つこと。ページはアニメーションではない。
  void reportsThePageCountAndIndex() {
    const QString path = writeFile(
        QDir(directory_.path()), "count.tif",
        makeTiff({{QSize(4, 4), Qt::red}, {QSize(4, 4), Qt::green}, {QSize(4, 4), Qt::blue}}));

    const auto result = ImageDecoder().decode(path, true, 1);

    QVERIFY(result.ok);
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::Pages);
    QCOMPARE(result.decodedImage.subImageCount, 3);
    QCOMPARE(result.decodedImage.subImageIndex, 1);
    QVERIFY(!result.decodedImage.isAnimation());
    QVERIFY(result.decodedImage.frameSequence == nullptr);
  }

  void failsForAPageBeyondTheEnd() {
    const QString path = writeFile(QDir(directory_.path()), "beyond.tif",
                                   makeTiff({{QSize(4, 4), Qt::red}, {QSize(4, 4), Qt::green}}));

    QVERIFY(!ImageDecoder().decode(path, true, 2).ok);
    QVERIFY(!ImageDecoder().decode(path, true, 99).ok);
    QVERIFY(!ImageDecoder().decode(path, true, -1).ok);
  }

  // ページが 1 つの TIFF は普通の画像として扱うこと。
  void treatsASinglePageTiffAsAnOrdinaryImage() {
    const QString path =
        writeFile(QDir(directory_.path()), "single.tif", makeTiff({{QSize(5, 5), Qt::red}}));

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
    QCOMPARE(result.decodedImage.subImageCount, 1);
  }

  // ページを持たない形式では、番号を無視すること。呼び側は形式を知らずに番号を渡せる。
  void ignoresThePageIndexForFormatsWithoutPages() {
    QImage image(6, 4, QImage::Format_RGB32);
    image.fill(Qt::green);
    const QString path = QDir(directory_.path()).filePath("nopages.png");
    QVERIFY(image.save(path));

    const auto result = ImageDecoder().decode(path, true, 5);

    QVERIFY(result.ok);
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
  }

#if defined(HEIRAN_HAVE_LIBWEBP)
  // ---- WebP (静止画とアニメーション) ----

  void decodesAStillWebp() {
    QImage image(12, 8, QImage::Format_RGBA8888);
    image.fill(QColor(10, 200, 30, 255));
    uint8_t *encoded = nullptr;
    const size_t size = WebPEncodeLosslessRGBA(image.constBits(), image.width(), image.height(),
                                               static_cast<int>(image.bytesPerLine()), &encoded);
    QVERIFY(size > 0);
    const QString path = writeFile(
        QDir(directory_.path()), "still.webp",
        QByteArray(reinterpret_cast<const char *>(encoded), static_cast<qsizetype>(size)));
    WebPFree(encoded);

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.displayImage.size(), QSize(12, 8));
    QCOMPARE(pixelOf(result.decodedImage), QColor(10, 200, 30));
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
  }

  void decodesEveryFrameOfAnAnimatedWebp() {
    const QString path =
        writeAnimatedWebp("anim.webp", {Qt::red, Qt::green, Qt::blue}, {0, 100, 300}, 600);
    QVERIFY(!path.isEmpty());

    const auto result = ImageDecoder().decode(path, true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const DecodedImage &decoded = result.decodedImage;
    QCOMPARE(decoded.subImageKind, SubImageKind::Frames);
    QCOMPARE(decoded.subImageCount, 3);
    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 0)), QColor(255, 0, 0));
    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 1)), QColor(0, 255, 0));
    QCOMPARE(pixelOf(heiran::infra::frameOf(decoded, 2)), QColor(0, 0, 255));
    // 表示時間は、次のフレームまでの間隔。
    QCOMPARE(heiran::infra::frameOf(decoded, 0).frameDelayMs, 100);
    QCOMPARE(heiran::infra::frameOf(decoded, 1).frameDelayMs, 200);
    QCOMPARE(heiran::infra::frameOf(decoded, 2).frameDelayMs, 300);
  }

  // WebP は EXIF の向きを適用しないため、適用したと記録しないこと。
  // 記録すると、画素値の位置を元ファイルの座標へ戻す計算が誤る。
  void doesNotClaimToHaveAppliedAnExifOrientationToWebp() {
    const QString path = writeAnimatedWebp("orientation.webp", {Qt::red, Qt::green}, {0, 100}, 200);
    const auto result = ImageDecoder().decode(path, true);
    QVERIFY(result.ok);

    QCOMPARE(result.decodedImage.appliedExifOrientation, heiran::infra::ExifOrientation::Normal);
  }
#endif

private:
#if defined(HEIRAN_HAVE_LIBWEBP)
  // 色ごとのフレームをタイムスタンプ (ミリ秒) で並べたアニメーション WebP。
  QString writeAnimatedWebp(const QString &name, const QVector<QColor> &colors,
                            const QVector<int> &timestampsMs, int endTimestampMs) {
    constexpr int kWidth = 16;
    constexpr int kHeight = 12;
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

    for (int index = 0; index < colors.size(); ++index) {
      QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
      image.fill(colors.at(index));
      WebPPicture picture;
      WebPPictureInit(&picture);
      picture.use_argb = 1;
      picture.width = kWidth;
      picture.height = kHeight;
      WebPPictureImportRGBA(&picture, image.constBits(), static_cast<int>(image.bytesPerLine()));
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
    const QString path = writeFile(
        QDir(directory_.path()), name,
        QByteArray(reinterpret_cast<const char *>(data.bytes), static_cast<qsizetype>(data.size)));
    WebPDataClear(&data);
    return path;
  }
#endif

  QTemporaryDir directory_;
};

QTEST_APPLESS_MAIN(MultiFrameDecodeTests)

#include "MultiFrameDecodeTests.moc"
