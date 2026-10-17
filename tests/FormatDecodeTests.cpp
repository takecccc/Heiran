// AVIF、JPEG XL、PSD の復号。
//
// 見本は tools/generate_format_samples.py が examples/formats へ作る。無ければ飛ばす。
// どれも 4 象限 (左上が赤、右上が緑、左下が青、右下が白) の 32x32。

#if defined(HEIRAN_HAVE_LIBAVIF)
#include "heiran/infra/AvifDecoder.hpp"
#endif
#include "heiran/infra/ImageDecoder.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using heiran::infra::DecodedImage;
using heiran::infra::ImageDecoder;
using heiran::infra::PixelColorModel;
using heiran::infra::PixelSampleType;

#ifndef HEIRAN_SOURCE_DIR
#error "HEIRAN_SOURCE_DIR must be defined"
#endif

namespace {

QString samplePath(const QString &name) {
  return QDir(HEIRAN_SOURCE_DIR).filePath("examples/formats/" + name);
}

bool hasSample(const QString &name) {
  return QFile::exists(samplePath(name));
}

// 4 象限の、各象限の中心。
struct Quadrant {
  int x;
  int y;
  QColor color;
};

const Quadrant kQuadrants[] = {
    {8, 8, QColor(255, 0, 0)},
    {24, 8, QColor(0, 255, 0)},
    {8, 24, QColor(0, 0, 255)},
    {24, 24, QColor(255, 255, 255)},
};

// 正規化値 (0.0-1.0) の差が、許容 (255 段で何段か) に収まるか。
bool nearColor(const DecodedImage &image, const Quadrant &quadrant, int toleranceLevels) {
  const float expected[3] = {static_cast<float>(quadrant.color.redF()),
                             static_cast<float>(quadrant.color.greenF()),
                             static_cast<float>(quadrant.color.blueF())};
  for (int channel = 0; channel < 3; ++channel) {
    const float actual = image.sampleRgba(quadrant.x, quadrant.y, channel);
    if (std::abs(actual - expected[channel]) * 255.0F > static_cast<float>(toleranceLevels)) {
      return false;
    }
  }
  return true;
}

}  // namespace

class FormatDecodeTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ---- AVIF ----

  void decodesAnAvif() {
    if (!hasSample("quadrants.avif")) {
      QSKIP("examples/formats/quadrants.avif is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants.avif"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.size(), QSize(32, 32));
    QCOMPARE(result.decodedImage.decoderBackend, QString("avif"));
    QCOMPARE(result.decodedImage.sourceSampleType, PixelSampleType::UInt8);
    // アルファの無い AVIF は、アルファの無い画像として持つ。
    QCOMPARE(result.decodedImage.sourceColorModel, PixelColorModel::RGB);
    // AVIF は不可逆。色は少しずれるため、数段の誤差を許す。
    for (const auto &quadrant : kQuadrants) {
      QVERIFY2(nearColor(result.decodedImage, quadrant, 6),
               qPrintable(QString("quadrant at %1,%2").arg(quadrant.x).arg(quadrant.y)));
    }
  }

  void decodesAnAvifWithAlpha() {
    if (!hasSample("quadrants-alpha.avif")) {
      QSKIP("examples/formats/quadrants-alpha.avif is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants-alpha.avif"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.sourceColorModel, PixelColorModel::RGBA);
    // 白の象限だけ、半透明 (128 / 255)。他は不透明。
    const float whiteAlpha = result.decodedImage.sampleRgba(24, 24, 3);
    const float redAlpha = result.decodedImage.sampleRgba(8, 8, 3);
    QVERIFY2(std::abs(whiteAlpha - 128.0F / 255.0F) * 255.0F < 4.0F,
             qPrintable(QString::number(whiteAlpha)));
    QVERIFY(redAlpha > 0.99F);
  }

  // 日本語を含むパスでも開けること。libavif のファイル入力は、狭い文字のパスを取るため、
  // ファイルを全部読んでから渡している。
  void decodesAnAvifFromANonAsciiPath() {
    if (!hasSample("quadrants.avif")) {
      QSKIP("examples/formats/quadrants.avif is not generated");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString copy = QDir(directory.path()).filePath(QString::fromUtf8("画像 テスト.avif"));
    QVERIFY(QFile::copy(samplePath("quadrants.avif"), copy));

    const auto result = ImageDecoder().decode(copy, true);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.size(), QSize(32, 32));
  }

  // 壊れたファイルは、落ちずに失敗を返す。
  void failsOnACorruptAvif() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("broken.avif");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not an avif file at all");
    file.close();

    const auto result = ImageDecoder().decode(path, true);
    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
  }

  void failsOnATruncatedAvif() {
    if (!hasSample("quadrants.avif")) {
      QSKIP("examples/formats/quadrants.avif is not generated");
    }
    QFile source(samplePath("quadrants.avif"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray whole = source.readAll();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("cut.avif");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(whole.left(whole.size() / 2));
    file.close();

    QVERIFY(!ImageDecoder().decode(path, true).ok);
  }

#if defined(HEIRAN_HAVE_LIBAVIF)
  // CICP の値から、色空間を求められること。
  void mapsCicpToAColorSpace() {
    using heiran::infra::colorSpaceFromCicp;
    const auto srgb = colorSpaceFromCicp(1, 13);
    QVERIFY(srgb.isValid());
    QCOMPARE(srgb.primaries(), QColorSpace::Primaries::SRgb);
    QCOMPARE(srgb.transferFunction(), QColorSpace::TransferFunction::SRgb);

    const auto p3 = colorSpaceFromCicp(12, 13);
    QCOMPARE(p3.primaries(), QColorSpace::Primaries::DciP3D65);

    const auto hdr = colorSpaceFromCicp(9, 16);
    QCOMPARE(hdr.primaries(), QColorSpace::Primaries::Bt2020);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QCOMPARE(hdr.transferFunction(), QColorSpace::TransferFunction::St2084);
#endif

    const auto linear = colorSpaceFromCicp(1, 8);
    QCOMPARE(linear.transferFunction(), QColorSpace::TransferFunction::Linear);

    // 対応しない値は、無効 (呼び側が既定へ退避する)。
    QVERIFY(!colorSpaceFromCicp(2, 2).isValid());
    QVERIFY(!colorSpaceFromCicp(1, 99).isValid());
  }
#endif

  // ---- JPEG XL ----

  void decodesAJpegXlLosslessly() {
    if (!hasSample("quadrants.jxl")) {
      QSKIP("examples/formats/quadrants.jxl is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants.jxl"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.size(), QSize(32, 32));
    QCOMPARE(result.decodedImage.decoderBackend, QString("jxl"));
    // 可逆に符号化した見本。画素は一致する。
    for (const auto &quadrant : kQuadrants) {
      QVERIFY2(nearColor(result.decodedImage, quadrant, 0),
               qPrintable(QString("quadrant at %1,%2").arg(quadrant.x).arg(quadrant.y)));
    }
  }

  void decodesAJpegXlWithAlpha() {
    if (!hasSample("quadrants-alpha.jxl")) {
      QSKIP("examples/formats/quadrants-alpha.jxl is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants-alpha.jxl"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.sourceColorModel, PixelColorModel::RGBA);
    QVERIFY(std::abs(result.decodedImage.sampleRgba(24, 24, 3) - 128.0F / 255.0F) * 255.0F < 1.5F);
  }

  // ---- PSD ----

  void decodesAnEightBitPsd() {
    if (!hasSample("quadrants-8bit.psd")) {
      QSKIP("examples/formats/quadrants-8bit.psd is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants-8bit.psd"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.size(), QSize(32, 32));
    QCOMPARE(result.decodedImage.decoderBackend, QString("psd"));
    for (const auto &quadrant : kQuadrants) {
      QVERIFY2(nearColor(result.decodedImage, quadrant, 0),
               qPrintable(QString("quadrant at %1,%2").arg(quadrant.x).arg(quadrant.y)));
    }
  }

  // 16bit の PSD は、精度を落とさずに持つこと。
  void keepsTheSixteenBitPrecisionOfAPsd() {
    if (!hasSample("quadrants-16bit.psd")) {
      QSKIP("examples/formats/quadrants-16bit.psd is not generated");
    }
    const auto result = ImageDecoder().decode(samplePath("quadrants-16bit.psd"), true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.sourceSampleType, PixelSampleType::UInt16);
    for (const auto &quadrant : kQuadrants) {
      QVERIFY2(nearColor(result.decodedImage, quadrant, 0),
               qPrintable(QString("quadrant at %1,%2").arg(quadrant.x).arg(quadrant.y)));
    }
  }
};

QTEST_MAIN(FormatDecodeTests)

#include "FormatDecodeTests.moc"
