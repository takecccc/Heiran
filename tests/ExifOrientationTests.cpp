#include "heiran/infra/ExifOrientation.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// orientation だけを持つ最小の APP1 ブロックを組み立てる。
// Qt には EXIF を書く手段が無いため、テスト用の画像は自分で作る。
QByteArray makeExifApp1Segment(int orientationValue) {
  QByteArray tiff;
  tiff.append("II", 2);                                     // リトルエンディアン
  tiff.append(QByteArray::fromHex("2a00"));                 // 42
  tiff.append(QByteArray::fromHex("08000000"));             // IFD0 は 8 バイト目から
  tiff.append(QByteArray::fromHex("0100"));                 // 項目は 1 つ
  tiff.append(QByteArray::fromHex("1201"));                 // タグ 0x0112
  tiff.append(QByteArray::fromHex("0300"));                 // 型 SHORT
  tiff.append(QByteArray::fromHex("01000000"));             // 個数 1
  tiff.append(static_cast<char>(orientationValue & 0xFF));  // 値
  tiff.append('\0');
  tiff.append(QByteArray::fromHex("0000"));      // 残りの詰め物
  tiff.append(QByteArray::fromHex("00000000"));  // 次の IFD は無い

  QByteArray payload;
  payload.append("Exif", 4);
  payload.append('\0');
  payload.append('\0');
  payload.append(tiff);

  const int segmentLength = payload.size() + 2;
  QByteArray segment;
  segment.append(static_cast<char>(0xFF));
  segment.append(static_cast<char>(0xE1));
  segment.append(static_cast<char>((segmentLength >> 8) & 0xFF));
  segment.append(static_cast<char>(segmentLength & 0xFF));
  segment.append(payload);
  return segment;
}

// SOI の直後へ APP1 を差し込んだ JPEG を作る。
QByteArray makeJpegWithOrientation(const QImage &image, int orientationValue) {
  QByteArray jpeg;
  QBuffer buffer(&jpeg);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "JPEG", 95)) {
    return {};
  }
  buffer.close();

  if (jpeg.size() < 2) {
    return {};
  }
  return jpeg.left(2) + makeExifApp1Segment(orientationValue) + jpeg.mid(2);
}

}  // namespace

class ExifOrientationTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void returnsNormalWhenThereIsNoExif() {
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "JPEG", 95));
    buffer.close();

    QCOMPARE(heiran::infra::readExifOrientation(jpeg), heiran::infra::ExifOrientation::Normal);
  }

  void readsEveryOrientationFromJpeg() {
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(Qt::green);

    for (int orientationValue = 1; orientationValue <= 8; ++orientationValue) {
      const QByteArray jpeg = makeJpegWithOrientation(image, orientationValue);
      QVERIFY2(!jpeg.isEmpty(), "the JPEG fixture could not be built");
      QCOMPARE(static_cast<int>(heiran::infra::readExifOrientation(jpeg)), orientationValue);
    }
  }

  void ignoresValuesOutsideTheDefinedRange() {
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QByteArray jpeg = makeJpegWithOrientation(image, 9);
    QVERIFY(!jpeg.isEmpty());

    QCOMPARE(heiran::infra::readExifOrientation(jpeg), heiran::infra::ExifOrientation::Normal);
  }

  void mapsOrientationToRotationAndFlip() {
    const auto transform = [](int value) {
      return heiran::infra::toExifTransform(static_cast<heiran::infra::ExifOrientation>(value));
    };

    QCOMPARE(transform(1).rotationQuarterTurns, 0);
    QVERIFY(!transform(1).flipX && !transform(1).flipY);

    QCOMPARE(transform(3).rotationQuarterTurns, 2);
    QCOMPARE(transform(6).rotationQuarterTurns, 1);
    QCOMPARE(transform(8).rotationQuarterTurns, 3);

    QVERIFY(transform(2).flipX);
    QVERIFY(transform(4).flipY);
  }

  // 復号が実際に orientation を適用しているかを、縦横が入れ替わるかで確かめる。
  // Qt は適用した値を報告しないため、外から見て分かるのはこの形になる。
  void decoderAppliesTheOrientation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QImage image(16, 8, QImage::Format_RGB32);
    image.fill(Qt::yellow);
    const QByteArray jpeg = makeJpegWithOrientation(image, 6);  // 90 度回転
    QVERIFY(!jpeg.isEmpty());

    const QString filePath = QDir(directory.path()).filePath("rotated.jpg");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(jpeg);
    file.close();

    const heiran::infra::ImageDecoder decoder;
    const auto honored = decoder.decode(filePath, true);
    QVERIFY(honored.ok);
    const auto ignored = decoder.decode(filePath, false);
    QVERIFY(ignored.ok);

    QCOMPARE(ignored.decodedImage.size(), QSize(16, 8));
    QCOMPARE(honored.decodedImage.size(), QSize(8, 16));
    // 適用した値が復号結果へ残っていること。
    QCOMPARE(honored.decodedImage.appliedExifOrientation, heiran::infra::ExifOrientation::Rotate90);
    QCOMPARE(ignored.decodedImage.appliedExifOrientation, heiran::infra::ExifOrientation::Normal);
  }

  // 画素値表示の位置は元ファイルの座標で出す。復号結果は EXIF を適用した後の
  // 並びのため、適用した分を戻さないと回転後の座標を答えてしまう。
  void reportsPixelPositionInOriginalFileCoordinates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 16x8 を 90 度回転で表示する。復号結果は 8x16 になる。
    QImage image(16, 8, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    const QByteArray jpeg = makeJpegWithOrientation(image, 6);
    QVERIFY(!jpeg.isEmpty());

    const QString filePath = QDir(directory.path()).filePath("0000.jpg");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(jpeg);
    file.close();

    heiran::domain::ImageSet imageSet;
    imageSet.name = "rotated";
    imageSet.label = "rotated";
    imageSet.path = QDir(directory.path()).filePath("*.jpg");
    imageSet.resolvedPaths.push_back(filePath);

    heiran::domain::SessionModel session;
    session.viewer.layout.rows = 1;
    session.viewer.layout.cols = 1;
    session.images.push_back(imageSet);
    session.cells.push_back({.imageIndex = 0});

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(session);
    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 1);
    QVERIFY(QTest::qWaitFor(
        [&cells]() { return cells.at(0)->cellStatus() != heiran::ui::CellStatus::Loading; }, 5000));

    // 表示は 8x16。左上は元ファイルでは左下にあたる。
    const QString topLeft = cells.at(0)->pixelInfoAtImagePosition(QPointF(0.5, 0.5));
    QVERIFY2(topLeft.contains("(   0,    7)"), qPrintable(topLeft));

    // 表示の右下は元ファイルでは右上にあたる。
    const QString bottomRight = cells.at(0)->pixelInfoAtImagePosition(QPointF(7.5, 15.5));
    QVERIFY2(bottomRight.contains("(  15,    0)"), qPrintable(bottomRight));
  }
};

QTEST_MAIN(ExifOrientationTests)

#include "ExifOrientationTests.moc"
