#include "heiran/ui/PixelValueFormat.hpp"

#include <QtTest>

using heiran::ui::PixelValueFormat;

class PixelValueFormatTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void roundTripsFormatNames() {
    QCOMPARE(heiran::ui::pixelValueFormatFromString(
                 heiran::ui::pixelValueFormatToString(PixelValueFormat::Normalized255)),
             PixelValueFormat::Normalized255);
    QCOMPARE(heiran::ui::pixelValueFormatFromString(
                 heiran::ui::pixelValueFormatToString(PixelValueFormat::Normalized01)),
             PixelValueFormat::Normalized01);
  }

  void fallsBackToNormalized255ForUnknownName() {
    QCOMPARE(heiran::ui::pixelValueFormatFromString(QString()), PixelValueFormat::Normalized255);
    QCOMPARE(heiran::ui::pixelValueFormatFromString("unknown"), PixelValueFormat::Normalized255);
  }

  void scalesToByteRange() {
    QCOMPARE(heiran::ui::formatPixelSample(0.0F, PixelValueFormat::Normalized255).trimmed(),
             QString("0.000"));
    QCOMPARE(heiran::ui::formatPixelSample(1.0F, PixelValueFormat::Normalized255).trimmed(),
             QString("255.000"));
    QCOMPARE(
        heiran::ui::formatPixelSample(128.0F / 255.0F, PixelValueFormat::Normalized255).trimmed(),
        QString("128.000"));
  }

  void keepsNormalizedRange() {
    QCOMPARE(heiran::ui::formatPixelSample(0.0F, PixelValueFormat::Normalized01).trimmed(),
             QString("0.00000"));
    QCOMPARE(heiran::ui::formatPixelSample(1.0F, PixelValueFormat::Normalized01).trimmed(),
             QString("1.00000"));
  }

  void formatsEightBitSamplesAsIntegersInByteRange() {
    QCOMPARE(heiran::ui::formatPixelSample(0.0F, PixelValueFormat::Normalized255, true),
             QString("  0"));
    QCOMPARE(heiran::ui::formatPixelSample(128.0F / 255.0F, PixelValueFormat::Normalized255, true),
             QString("128"));
    QCOMPARE(heiran::ui::formatPixelSample(1.0F, PixelValueFormat::Normalized255, true),
             QString("255"));
    QCOMPARE(heiran::ui::formatPixelSample(1.0F, PixelValueFormat::Normalized01, true).trimmed(),
             QString("1.00000"));
  }

  void keeps16BitStepsDistinguishable() {
    // 16bit の隣り合う階調が同じ文字列にならないことを確認する。
    const float lower = 32896.0F / 65535.0F;
    const float upper = 32897.0F / 65535.0F;

    QVERIFY(heiran::ui::formatPixelSample(lower, PixelValueFormat::Normalized255) !=
            heiran::ui::formatPixelSample(upper, PixelValueFormat::Normalized255));
    QVERIFY(heiran::ui::formatPixelSample(lower, PixelValueFormat::Normalized01) !=
            heiran::ui::formatPixelSample(upper, PixelValueFormat::Normalized01));
  }

  void mapsEightBitOriginatedSixteenBitValuesToIntegers() {
    // 8bit 由来の 16bit 値 (257 の倍数) は 0-255 換算で整数になる。
    QCOMPARE(heiran::ui::formatPixelSample(32896.0F / 65535.0F, PixelValueFormat::Normalized255)
                 .trimmed(),
             QString("128.000"));
  }

  void keepsValuesAboveOneForFloatImages() {
    QCOMPARE(heiran::ui::formatPixelSample(4.25F, PixelValueFormat::Normalized01).trimmed(),
             QString("4.25000"));
    QCOMPARE(heiran::ui::formatPixelSample(4.25F, PixelValueFormat::Normalized255).trimmed(),
             QString("1083.750"));
  }

  void keepsNegativeValues() {
    QCOMPARE(heiran::ui::formatPixelSample(-0.5F, PixelValueFormat::Normalized01).trimmed(),
             QString("-0.50000"));
    QCOMPARE(heiran::ui::formatPixelSample(-0.5F, PixelValueFormat::Normalized255).trimmed(),
             QString("-127.500"));
  }

  void padsToFixedWidthForColumnAlignment() {
    const QString small = heiran::ui::formatPixelSample(0.0F, PixelValueFormat::Normalized255);
    const QString large = heiran::ui::formatPixelSample(1.0F, PixelValueFormat::Normalized255);
    QCOMPARE(small.size(), large.size());
  }
};

QTEST_MAIN(PixelValueFormatTests)

#include "PixelValueFormatTests.moc"
