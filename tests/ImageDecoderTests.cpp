#include "heiran/infra/ImageDecoder.hpp"

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using heiran::infra::PixelBuffer;
using heiran::infra::PixelColorModel;
using heiran::infra::PixelSampleType;

namespace {

PixelBuffer makeBuffer(PixelSampleType sampleType, PixelColorModel colorModel, int channelCount,
                       const QByteArray &pixelBytes, int width, int height) {
  auto storage = QSharedPointer<QByteArray>::create(pixelBytes);
  return {
      .storage = storage,
      .bytesPerRow =
          static_cast<qsizetype>(width) * channelCount * heiran::infra::bytesPerSample(sampleType),
      .width = width,
      .height = height,
      .channelCount = channelCount,
      .sampleType = sampleType,
      .colorModel = colorModel,
  };
}

QByteArray bytesOfUInt16(const QVector<quint16> &values) {
  QByteArray bytes(static_cast<qsizetype>(values.size()) * sizeof(quint16), '\0');
  std::memcpy(bytes.data(), values.constData(), static_cast<size_t>(bytes.size()));
  return bytes;
}

QByteArray bytesOfFloat32(const QVector<float> &values) {
  QByteArray bytes(static_cast<qsizetype>(values.size()) * sizeof(float), '\0');
  std::memcpy(bytes.data(), values.constData(), static_cast<size_t>(bytes.size()));
  return bytes;
}

}  // namespace

class ImageDecoderTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void reportsBytesPerSample() {
    QCOMPARE(heiran::infra::bytesPerSample(PixelSampleType::UInt8), 1);
    QCOMPARE(heiran::infra::bytesPerSample(PixelSampleType::UInt16), 2);
    QCOMPARE(heiran::infra::bytesPerSample(PixelSampleType::Float16), 2);
    QCOMPARE(heiran::infra::bytesPerSample(PixelSampleType::Float32), 4);
    QCOMPARE(heiran::infra::bytesPerSample(PixelSampleType::Unknown), 0);
  }

  void normalizesUInt8Samples() {
    const QByteArray pixels("\x00\x80\xFF", 3);
    const PixelBuffer buffer =
        makeBuffer(PixelSampleType::UInt8, PixelColorModel::RGB, 3, pixels, 1, 1);

    QCOMPARE(buffer.sample(0, 0, 0), 0.0F);
    QVERIFY(qAbs(buffer.sample(0, 0, 1) - 128.0F / 255.0F) < 0.0001F);
    QCOMPARE(buffer.sample(0, 0, 2), 1.0F);
  }

  void normalizesUInt16Samples() {
    const PixelBuffer buffer = makeBuffer(PixelSampleType::UInt16, PixelColorModel::RGB, 3,
                                          bytesOfUInt16({0, 32768, 65535}), 1, 1);

    QCOMPARE(buffer.sample(0, 0, 0), 0.0F);
    QVERIFY(qAbs(buffer.sample(0, 0, 1) - 32768.0F / 65535.0F) < 0.0001F);
    QCOMPARE(buffer.sample(0, 0, 2), 1.0F);
  }

  void keepsFloatSamplesAboveOne() {
    const PixelBuffer buffer = makeBuffer(PixelSampleType::Float32, PixelColorModel::RGB, 3,
                                          bytesOfFloat32({-0.5F, 1.0F, 4.25F}), 1, 1);

    QCOMPARE(buffer.sample(0, 0, 0), -0.5F);
    QCOMPARE(buffer.sample(0, 0, 1), 1.0F);
    QCOMPARE(buffer.sample(0, 0, 2), 4.25F);
  }

  void returnsZeroOutsideBounds() {
    const QByteArray pixels("\xFF", 1);
    const PixelBuffer buffer =
        makeBuffer(PixelSampleType::UInt8, PixelColorModel::Gray, 1, pixels, 1, 1);

    QCOMPARE(buffer.sample(1, 0, 0), 0.0F);
    QCOMPARE(buffer.sample(0, 1, 0), 0.0F);
    QCOMPARE(buffer.sample(-1, 0, 0), 0.0F);
    QCOMPARE(buffer.sample(0, 0, 1), 0.0F);
  }

  void expandsGrayToRgbaWithOpaqueAlpha() {
    const QByteArray pixels("\x80", 1);
    const PixelBuffer buffer =
        makeBuffer(PixelSampleType::UInt8, PixelColorModel::Gray, 1, pixels, 1, 1);

    const float gray = buffer.sample(0, 0, 0);
    QCOMPARE(buffer.sampleRgba(0, 0, 0), gray);
    QCOMPARE(buffer.sampleRgba(0, 0, 1), gray);
    QCOMPARE(buffer.sampleRgba(0, 0, 2), gray);
    QCOMPARE(buffer.sampleRgba(0, 0, 3), 1.0F);
  }

  void treatsMissingAlphaAsOpaque() {
    const QByteArray pixels("\x10\x20\x30", 3);
    const PixelBuffer buffer =
        makeBuffer(PixelSampleType::UInt8, PixelColorModel::RGB, 3, pixels, 1, 1);

    QCOMPARE(buffer.sampleRgba(0, 0, 3), 1.0F);
  }

  void fallsBackToDisplayImageWithoutAnalysisBuffer() {
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));

    const heiran::infra::DecodedImage decodedImage{.displayImage = image};
    QCOMPARE(decodedImage.hasAnalysisBuffer(), false);
    QCOMPARE(decodedImage.sampleRgba(0, 0, 0), 1.0F);
    QCOMPARE(decodedImage.sampleRgba(0, 0, 1), 0.0F);
    QCOMPARE(decodedImage.sampleRgba(0, 0, 3), 1.0F);
  }

  void keepsEightBitPngWithoutExtraMemory() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    const QString filePath = QDir(tempDir.path()).filePath("eight-bit.png");
    QVERIFY(image.save(filePath));

    const auto result = heiran::infra::ImageDecoder().decode(filePath, true);
    QVERIFY(result.ok);
    // 8bit 画像では解析用バッファのために追加のメモリを使わない。
    // 表示用画像から読むか、記憶域を共有するかのどちらかになる。
    if (result.decodedImage.hasAnalysisBuffer()) {
      QCOMPARE(static_cast<const void *>(result.decodedImage.analysisBuffer.data()),
               static_cast<const void *>(result.decodedImage.displayImage.constBits()));
    }
    QVERIFY(qAbs(result.decodedImage.sampleRgba(0, 0, 0) - 10.0F / 255.0F) < 0.0001F);
  }

  void readsSixteenBitPngThroughAnalysisBuffer() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(2, 2, QImage::Format_RGBA64);
    image.fill(QColor::fromRgba64(4096, 8192, 16384, 65535));
    const QString filePath = QDir(tempDir.path()).filePath("sixteen-bit.png");
    QVERIFY(image.save(filePath));

    const auto result = heiran::infra::ImageDecoder().decode(filePath, true);
    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.hasAnalysisBuffer());
    QCOMPARE(result.decodedImage.analysisBuffer.sampleType, PixelSampleType::UInt16);

    // 8bit へ量子化すると 4096/65535 は 16/255 へ丸められ、精度が落ちる。
    const float red = result.decodedImage.sampleRgba(0, 0, 0);
    QVERIFY(qAbs(red - 4096.0F / 65535.0F) < 0.0001F);
    QVERIFY(qAbs(red - 16.0F / 255.0F) > 0.0001F);
  }
};

QTEST_MAIN(ImageDecoderTests)

#include "ImageDecoderTests.moc"
