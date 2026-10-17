// セルの見出しに出す、画像の情報 (幅、高さ、ファイルサイズ)。

#include "heiran/ui/ImageInfoText.hpp"

#include <QtTest>

using heiran::ui::formatFileSizeText;
using heiran::ui::formatImageInfoText;
using heiran::ui::pixelFormatText;

class ImageInfoTextTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void formatsBytesWithoutADecimal() {
    QCOMPARE(formatFileSizeText(0), QString("0 B"));
    QCOMPARE(formatFileSizeText(1023), QString("1023 B"));
  }

  // 1024 進で、単位が上がる境目を正しく扱うこと。
  void switchesUnitsAt1024() {
    QCOMPARE(formatFileSizeText(1024), QString("1.0 KB"));
    QCOMPARE(formatFileSizeText(1536), QString("1.5 KB"));
    QCOMPARE(formatFileSizeText(1024 * 1024), QString("1.0 MB"));
    QCOMPARE(formatFileSizeText(12'900'000), QString("12.3 MB"));
    QCOMPARE(formatFileSizeText(qint64{3} * 1024 * 1024 * 1024), QString("3.0 GB"));
  }

  void hasNoTextForAnUnknownSize() { QVERIFY(formatFileSizeText(-1).isEmpty()); }

  void joinsSizeAndFileSize() {
    QCOMPARE(formatImageInfoText("RGB8", QSize(4000, 3000), 12'900'000),
             QString("RGB8 · 4000×3000 · 12.3 MB"));
  }

  // ファイルの大きさが不明でも、幅と高さは出す。
  void showsOnlyTheDimensionsWhenTheFileSizeIsUnknown() {
    QCOMPARE(formatImageInfoText("RGBA16", QSize(640, 480), -1), QString("RGBA16 · 640×480"));
  }

  // 形式が不明 (拡張子が無い) なら、その項目を省く。
  void omitsTheFormatWhenItIsUnknown() {
    QCOMPARE(formatImageInfoText(QString(), QSize(640, 480), 2048), QString("640×480 · 2.0 KB"));
  }

  // 画素の形式は、色のモデルとビット数。float は F を添える。
  void namesThePixelFormat() {
    using heiran::infra::PixelColorModel;
    using heiran::infra::PixelSampleType;
    const auto make = [](PixelSampleType type, PixelColorModel model) {
      heiran::infra::DecodedImage image;
      image.sourceSampleType = type;
      image.sourceColorModel = model;
      return image;
    };

    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt8, PixelColorModel::RGB)), QString("RGB8"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt8, PixelColorModel::RGBA)),
             QString("RGBA8"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt16, PixelColorModel::RGB)),
             QString("RGB16"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt16, PixelColorModel::RGBA)),
             QString("RGBA16"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::Float16, PixelColorModel::RGBA)),
             QString("RGBA16F"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::Float32, PixelColorModel::RGB)),
             QString("RGB32F"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt8, PixelColorModel::Gray)),
             QString("Gray8"));
    QCOMPARE(pixelFormatText(make(PixelSampleType::UInt16, PixelColorModel::GrayAlpha)),
             QString("GrayA16"));
  }

  // 不明な形式は出さない。
  void hasNoTextForAnUnknownPixelFormat() {
    QVERIFY(pixelFormatText(heiran::infra::DecodedImage()).isEmpty());
  }

  // Qt 経由で読んだグレースケール画像は、RGB ではなくグレーと言うこと。
  void callsAGrayscaleDisplayImageGray() {
    heiran::infra::DecodedImage image;
    image.sourceSampleType = heiran::infra::PixelSampleType::UInt8;
    image.sourceColorModel = heiran::infra::PixelColorModel::RGB;
    image.displayImage = QImage(2, 2, QImage::Format_Grayscale8);
    QCOMPARE(pixelFormatText(image), QString("Gray8"));
  }

  void showsNothingForAnEmptyImage() {
    QVERIFY(formatImageInfoText("RGB8", QSize(), 100).isEmpty());
  }
};

QTEST_MAIN(ImageInfoTextTests)

#include "ImageInfoTextTests.moc"
