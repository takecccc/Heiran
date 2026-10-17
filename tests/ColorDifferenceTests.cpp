#include "heiran/domain/ColorDifference.hpp"

#include <QRandomGenerator>
#include <QtTest>
#include <cmath>
#include <vector>

using namespace heiran::domain;

class ColorDifferenceTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // Sharma らの論文の検証用データ (CIEDE2000 の実装の確かめに使われる組)。
  void matchesThePublishedReferencePairs_data() {
    QTest::addColumn<double>("l1");
    QTest::addColumn<double>("a1");
    QTest::addColumn<double>("b1");
    QTest::addColumn<double>("l2");
    QTest::addColumn<double>("a2");
    QTest::addColumn<double>("b2");
    QTest::addColumn<double>("expected");
    QTest::newRow("1") << 50.0 << 2.6772 << -79.7751 << 50.0 << 0.0 << -82.7485 << 2.0425;
    QTest::newRow("2") << 50.0 << 3.1571 << -77.2803 << 50.0 << 0.0 << -82.7485 << 2.8615;
    QTest::newRow("3") << 50.0 << 2.8361 << -74.0200 << 50.0 << 0.0 << -82.7485 << 3.4412;
    QTest::newRow("4") << 50.0 << -1.3802 << -84.2814 << 50.0 << 0.0 << -82.7485 << 1.0000;
    QTest::newRow("7") << 50.0 << 0.0 << 0.0 << 50.0 << -1.0 << 2.0 << 2.3669;
    QTest::newRow("17") << 50.0 << 2.5 << 0.0 << 50.0 << 0.0 << -2.5 << 4.3065;
    QTest::newRow("25") << 60.2574 << -34.0099 << 36.2677 << 60.4626 << -34.1751 << 39.4387
                        << 1.2644;
  }

  void matchesThePublishedReferencePairs() {
    QFETCH(double, l1);
    QFETCH(double, a1);
    QFETCH(double, b1);
    QFETCH(double, l2);
    QFETCH(double, a2);
    QFETCH(double, b2);
    QFETCH(double, expected);
    QVERIFY(std::abs(deltaE2000({l1, a1, b1}, {l2, a2, b2}) - expected) < 1e-4);
    // 順序を入れ替えても同じ。
    QVERIFY(std::abs(deltaE2000({l2, a2, b2}, {l1, a1, b1}) - expected) < 1e-4);
  }

  // 高速な版が、基準の実装と同じ値を返すこと。無彩色、色相の反対、境目をまたぐ色相も含む。
  void fastVersionAgreesWithTheReference() {
    QRandomGenerator random(7);
    const auto sample = [&]() {
      Lab lab{random.bounded(100.0), random.bounded(200.0) - 100.0, random.bounded(200.0) - 100.0};
      switch (random.bounded(8)) {
        case 0:
          lab.a = lab.b = 0.0;  // 無彩色
          break;
        case 1:
          lab.a *= 0.001;  // ほぼ無彩色
          lab.b *= 0.001;
          break;
        default:
          break;
      }
      return lab;
    };
    double worst = 0.0;
    QString worstPair;
    for (int index = 0; index < 200000; ++index) {
      const Lab first = sample();
      Lab second = sample();
      if (index % 5 == 0) {  // 近い色 (実際の差分の大半)
        second = {first.l + random.bounded(2.0) - 1.0, first.a + random.bounded(2.0) - 1.0,
                  first.b + random.bounded(2.0) - 1.0};
      } else if (index % 11 == 0) {  // 色相が反対
        // ちょうど反対だと、基準の実装も、丸めの誤差で向きが決まる。少しずらす。
        second = {first.l, -first.a * 1.01, -first.b * 0.99};
      }
      const double expected = deltaE2000(first, second);
      const double error = std::abs(deltaE2000Fast(first, second) - expected);
      if (error > worst) {
        worst = error;
        worstPair = QString("(%1,%2,%3) (%4,%5,%6) ref %7 fast %8")
                        .arg(first.l).arg(first.a).arg(first.b).arg(second.l).arg(second.a)
                        .arg(second.b).arg(expected).arg(deltaE2000Fast(first, second));
      }
    }
    QVERIFY2(worst < 1e-6, qPrintable(worstPair));
  }

  // 白は L=100、無彩色は a=b=0。
  void convertsWhiteAndGrayToLab() {
    const Matrix3 matrix = rgbToXyzMatrix(ColorPrimaries::SRgb);
    const Lab white = linearRgbToLab(matrix, 1.0, 1.0, 1.0);
    QVERIFY(std::abs(white.l - 100.0) < 1e-2);
    QVERIFY(std::abs(white.a) < 1e-2);
    QVERIFY(std::abs(white.b) < 1e-2);
    const Lab gray = linearRgbToLab(matrix, 0.18, 0.18, 0.18);
    QVERIFY(std::abs(gray.l - 49.5) < 0.5);
    QVERIFY(std::abs(gray.a) < 1e-2);
    const Lab black = linearRgbToLab(matrix, 0.0, 0.0, 0.0);
    QVERIFY(std::abs(black.l) < 1e-9);
  }

  // 同じ画素は 0。黒と白は 100 に近い。平面の処理が、1 画素ずつの計算と一致する。
  void computesThePlaneAndSkipsIdenticalPixels() {
    const Matrix3 matrix = rgbToXyzMatrix(ColorPrimaries::SRgb);
    const std::vector<float> baseR = {0.5F, 0.0F, 0.2F}, baseG = {0.5F, 0.0F, 0.4F},
                             baseB = {0.5F, 0.0F, 0.6F};
    const std::vector<float> targetR = {0.5F, 1.0F, 0.25F}, targetG = {0.5F, 1.0F, 0.4F},
                             targetB = {0.5F, 1.0F, 0.5F};
    std::vector<float> output(3, -1.0F);
    deltaE2000Plane(matrix, baseR.data(), baseG.data(), baseB.data(), targetR.data(),
                    targetG.data(), targetB.data(), 3, output.data());
    QCOMPARE(output[0], 0.0F);
    QVERIFY(output[1] > 99.0F && output[1] < 101.0F);
    const double expected = deltaE2000(linearRgbToLab(matrix, 0.2, 0.4, 0.6),
                                       linearRgbToLab(matrix, 0.25, 0.4, 0.5));
    QVERIFY(std::abs(output[2] - expected) < 1e-4);
  }

  void summarizesTheDeltaE() {
    const std::vector<float> values = {0.0F, 1.0F, 3.0F, 8.0F};
    const auto metrics = computeColorDifferenceMetrics(values.data(), values.size(), 2.0);
    QCOMPARE(metrics.pixelCount, 4);
    QCOMPARE(metrics.maxDeltaE, 8.0);
    QCOMPARE(metrics.meanDeltaE, 3.0);
    QCOMPARE(metrics.exceededPixelCount, 2);
    QCOMPARE(computeColorDifferenceMetrics(nullptr, 0, 1.0).pixelCount, 0);
  }
};

QTEST_APPLESS_MAIN(ColorDifferenceTests)
#include "ColorDifferenceTests.moc"
