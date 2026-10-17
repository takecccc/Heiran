#include "heiran/domain/ColorSpace.hpp"

#include <QtTest>
#include <cmath>

namespace {

bool nearlyEqual(double left, double right, double tolerance = 1e-4) {
  return std::abs(left - right) < tolerance;
}

}  // namespace

class ColorSpaceTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 輝度の重みを色度から導出できること。定数表を持たずに済む根拠になる。
  // 期待値は各規格が公表している値。
  void derivesLumaWeightsFromThePrimaries() {
    const auto srgb = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QVERIFY2(nearlyEqual(srgb.red, 0.2126), qPrintable(QString::number(srgb.red, 'f', 6)));
    QVERIFY2(nearlyEqual(srgb.green, 0.7152), qPrintable(QString::number(srgb.green, 'f', 6)));
    QVERIFY2(nearlyEqual(srgb.blue, 0.0722), qPrintable(QString::number(srgb.blue, 'f', 6)));

    const auto p3 = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::DisplayP3);
    QVERIFY2(nearlyEqual(p3.red, 0.2290), qPrintable(QString::number(p3.red, 'f', 6)));
    QVERIFY2(nearlyEqual(p3.green, 0.6917), qPrintable(QString::number(p3.green, 'f', 6)));
    QVERIFY2(nearlyEqual(p3.blue, 0.0793), qPrintable(QString::number(p3.blue, 'f', 6)));

    const auto rec2020 = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::Rec2020);
    QVERIFY2(nearlyEqual(rec2020.red, 0.2627), qPrintable(QString::number(rec2020.red, 'f', 6)));
    QVERIFY2(nearlyEqual(rec2020.green, 0.6780),
             qPrintable(QString::number(rec2020.green, 'f', 6)));
    QVERIFY2(nearlyEqual(rec2020.blue, 0.0593), qPrintable(QString::number(rec2020.blue, 'f', 6)));

    const auto adobe = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::AdobeRgb);
    QVERIFY2(nearlyEqual(adobe.red, 0.2974), qPrintable(QString::number(adobe.red, 'f', 6)));
    QVERIFY2(nearlyEqual(adobe.green, 0.6273), qPrintable(QString::number(adobe.green, 'f', 6)));
    QVERIFY2(nearlyEqual(adobe.blue, 0.0753), qPrintable(QString::number(adobe.blue, 'f', 6)));
  }

  // 重みの合計が 1.0 になること。白が輝度 1.0 になる条件にあたる。
  void lumaWeightsSumToOne() {
    for (const auto primaries :
         {heiran::domain::ColorPrimaries::SRgb, heiran::domain::ColorPrimaries::DisplayP3,
          heiran::domain::ColorPrimaries::Rec2020, heiran::domain::ColorPrimaries::AdobeRgb}) {
      const auto weights = heiran::domain::lumaWeights(primaries);
      QVERIFY(nearlyEqual(weights.red + weights.green + weights.blue, 1.0, 1e-9));
    }
  }

  // Auto は sRGB として扱うこと。指定が無い場合の既定になる。
  void treatsAutoAsSrgb() {
    const auto autoWeights = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::Auto);
    const auto srgb = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QCOMPARE(autoWeights.red, srgb.red);
    QCOMPARE(autoWeights.green, srgb.green);
    QCOMPARE(autoWeights.blue, srgb.blue);
  }

  // RGB->XYZ 行列の Y 行が輝度の重みと一致すること。導出が同じ根拠から来ている。
  void matchesTheLumaRowOfTheMatrix() {
    const auto matrix = heiran::domain::rgbToXyzMatrix(heiran::domain::ColorPrimaries::SRgb);
    const auto weights = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QVERIFY(nearlyEqual(matrix[3], weights.red, 1e-9));
    QVERIFY(nearlyEqual(matrix[4], weights.green, 1e-9));
    QVERIFY(nearlyEqual(matrix[5], weights.blue, 1e-9));
  }

  // 白 (1,1,1) が白色点の XYZ になること。行列の組み方が正しい根拠になる。
  void mapsWhiteToTheWhitePoint() {
    const auto matrix = heiran::domain::rgbToXyzMatrix(heiran::domain::ColorPrimaries::SRgb);
    const double x = matrix[0] + matrix[1] + matrix[2];
    const double y = matrix[3] + matrix[4] + matrix[5];
    const double z = matrix[6] + matrix[7] + matrix[8];
    // D65 を Y = 1 に正規化した値。
    QVERIFY2(nearlyEqual(x, 0.3127 / 0.3290), qPrintable(QString::number(x, 'f', 6)));
    QVERIFY(nearlyEqual(y, 1.0, 1e-9));
    QVERIFY2(nearlyEqual(z, (1.0 - 0.3127 - 0.3290) / 0.3290),
             qPrintable(QString::number(z, 'f', 6)));
  }

  // 同じ原色どうしの変換が単位行列になること。
  void convertsBetweenTheSamePrimariesWithoutChange() {
    const auto matrix = heiran::domain::primariesConversionMatrix(
        heiran::domain::ColorPrimaries::DisplayP3, heiran::domain::ColorPrimaries::DisplayP3);
    const heiran::domain::Matrix3 identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int index = 0; index < 9; ++index) {
      QVERIFY(nearlyEqual(matrix[index], identity[index], 1e-9));
    }
  }

  // 往復すると元へ戻ること。
  void roundTripsBetweenPrimaries() {
    const auto forward = heiran::domain::primariesConversionMatrix(
        heiran::domain::ColorPrimaries::SRgb, heiran::domain::ColorPrimaries::Rec2020);
    const auto backward = heiran::domain::primariesConversionMatrix(
        heiran::domain::ColorPrimaries::Rec2020, heiran::domain::ColorPrimaries::SRgb);
    const auto combined = heiran::domain::multiply(backward, forward);
    const heiran::domain::Matrix3 identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int index = 0; index < 9; ++index) {
      QVERIFY(nearlyEqual(combined[index], identity[index], 1e-9));
    }
  }

  // 狭い色域から広い色域へ移すと、原色の彩度は下がる方向へ動くこと。
  // sRGB の赤は Rec.2020 では原色そのものではなくなる。
  void mapsSrgbRedInsideRec2020() {
    const auto matrix = heiran::domain::primariesConversionMatrix(
        heiran::domain::ColorPrimaries::SRgb, heiran::domain::ColorPrimaries::Rec2020);
    // sRGB の (1, 0, 0) を Rec.2020 の RGB へ写す。
    const double red = matrix[0];
    const double green = matrix[3];
    const double blue = matrix[6];
    QVERIFY2(red < 1.0 && red > 0.5, qPrintable(QString::number(red, 'f', 6)));
    QVERIFY2(green > 0.0, qPrintable(QString::number(green, 'f', 6)));
    QVERIFY2(blue >= 0.0, qPrintable(QString::number(blue, 'f', 6)));
  }

  // 伝達関数が往復すること。
  void roundTripsTheTransferFunctions() {
    for (const auto transfer :
         {heiran::domain::TransferFunction::SRgb, heiran::domain::TransferFunction::Rec709,
          heiran::domain::TransferFunction::Linear, heiran::domain::TransferFunction::Gamma22,
          heiran::domain::TransferFunction::Gamma24}) {
      for (const double value : {0.0, 0.001, 0.05, 0.18, 0.5, 1.0, 4.0}) {
        const double linear = heiran::domain::decodeToLinear(value, transfer);
        const double encoded = heiran::domain::encodeFromLinear(linear, transfer);
        QVERIFY2(nearlyEqual(encoded, value, 1e-6),
                 qPrintable(QString("%1 -> %2 -> %3").arg(value).arg(linear).arg(encoded)));
      }
    }
  }

  // sRGB は 0 と 1 を保つこと。中間は線形より暗い値へ戻ること。
  void decodesSrgbAgainstKnownPoints() {
    const auto srgb = heiran::domain::TransferFunction::SRgb;
    QVERIFY(nearlyEqual(heiran::domain::decodeToLinear(0.0, srgb), 0.0, 1e-9));
    QVERIFY(nearlyEqual(heiran::domain::decodeToLinear(1.0, srgb), 1.0, 1e-9));
    // 0.5 の符号化値はおよそ 0.2140 の線形光にあたる。
    QVERIFY2(nearlyEqual(heiran::domain::decodeToLinear(0.5, srgb), 0.2140),
             qPrintable(QString::number(heiran::domain::decodeToLinear(0.5, srgb), 'f', 6)));
    // 0.18 の線形光はおよそ 0.46136 の符号化値になる。
    // よく引かれる 0.4620 は 2.2 乗の近似で、sRGB の区分関数の値とは少しずれる。
    QVERIFY2(nearlyEqual(heiran::domain::encodeFromLinear(0.18, srgb), 0.46136),
             qPrintable(QString::number(heiran::domain::encodeFromLinear(0.18, srgb), 'f', 6)));
  }

  // 浮動小数画像は負の値を持ちうる。符号を保ったまま同じ曲線を当てること。
  void keepsTheSignForNegativeValues() {
    const auto srgb = heiran::domain::TransferFunction::SRgb;
    QVERIFY(nearlyEqual(heiran::domain::decodeToLinear(-0.5, srgb),
                        -heiran::domain::decodeToLinear(0.5, srgb), 1e-9));
    QVERIFY(nearlyEqual(heiran::domain::encodeFromLinear(-0.18, srgb),
                        -heiran::domain::encodeFromLinear(0.18, srgb), 1e-9));
  }

  // 1.0 を超える値をそのまま通すこと。HDR の値を切り落とさない。
  void keepsValuesAboveOne() {
    const auto srgb = heiran::domain::TransferFunction::SRgb;
    QVERIFY(heiran::domain::decodeToLinear(2.0, srgb) > 1.0);
    QVERIFY(heiran::domain::encodeFromLinear(4.0, srgb) > 1.0);
    QCOMPARE(heiran::domain::decodeToLinear(8.0, heiran::domain::TransferFunction::Linear), 8.0);
  }

  void convertsToAndFromStrings() {
    QCOMPARE(heiran::domain::colorPrimariesFromString("display-p3"),
             heiran::domain::ColorPrimaries::DisplayP3);
    QCOMPARE(heiran::domain::colorPrimariesFromString("BT709"),
             heiran::domain::ColorPrimaries::SRgb);
    QCOMPARE(heiran::domain::colorPrimariesFromString("unknown"),
             heiran::domain::ColorPrimaries::Auto);
    QCOMPARE(heiran::domain::toColorPrimariesString(heiran::domain::ColorPrimaries::Rec2020),
             QString("rec2020"));

    QCOMPARE(heiran::domain::transferFunctionFromString(" Linear "),
             heiran::domain::TransferFunction::Linear);
    QCOMPARE(heiran::domain::transferFunctionFromString("gamma2.2"),
             heiran::domain::TransferFunction::Gamma22);
    QCOMPARE(heiran::domain::toTransferFunctionString(heiran::domain::TransferFunction::SRgb),
             QString("srgb"));
  }
};

QTEST_MAIN(ColorSpaceTests)

#include "ColorSpaceTests.moc"
