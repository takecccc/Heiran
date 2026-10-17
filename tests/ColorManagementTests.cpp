#include "heiran/domain/ColorSpace.hpp"
#include "heiran/ui/ColorManagement.hpp"

#include <QtTest>
#include <cmath>

class ColorManagementTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 曲線の種類を取り出せること。
  void resolvesTheTransferFunctions() {
    const auto params = heiran::ui::makeColorTransformParams(QColorSpace(QColorSpace::SRgbLinear),
                                                             QColorSpace(QColorSpace::SRgb));
    QCOMPARE(params.sourceTransfer, heiran::ui::ShaderTransfer::Linear);
    QCOMPARE(params.displayTransfer, heiran::ui::ShaderTransfer::SRgb);
    // 原色は同じなので行列は要らない。
    QVERIFY(params.primariesMatch);
  }

  // 指数で持つ曲線は指数の値も持ち出すこと。
  void carriesTheGammaValue() {
    const QColorSpace gammaSpace(QColorSpace::Primaries::SRgb, QColorSpace::TransferFunction::Gamma,
                                 2.2F);
    const auto params =
        heiran::ui::makeColorTransformParams(gammaSpace, QColorSpace(QColorSpace::SRgb));
    QCOMPARE(params.sourceTransfer, heiran::ui::ShaderTransfer::Gamma);
    QVERIFY2(std::abs(params.sourceGamma - 2.2F) < 0.001F,
             qPrintable(QString::number(params.sourceGamma)));
  }

  // 原色が違えば行列が出ること。
  void producesAMatrixForDifferentPrimaries() {
    const auto params = heiran::ui::makeColorTransformParams(QColorSpace(QColorSpace::DisplayP3),
                                                             QColorSpace(QColorSpace::SRgb));
    QVERIFY(!params.primariesMatch);
  }

  // Qt から導いた行列が、自前の色度からの導出と一致すること。
  //
  // 片方だけでは正しさを確かめられない。`primaryPoints()` が信用できないため
  // Qt 側は基底ベクトルを通して求めており、その結果を独立に検算する。
  void agreesWithTheChromaticityDerivation() {
    struct Case {
      QColorSpace::NamedColorSpace qtSpace;
      heiran::domain::ColorPrimaries primaries;
      const char *name;
    };
    const Case cases[] = {
        {QColorSpace::DisplayP3, heiran::domain::ColorPrimaries::DisplayP3, "Display P3"},
        {QColorSpace::Bt2020, heiran::domain::ColorPrimaries::Rec2020, "Rec.2020"},
        {QColorSpace::AdobeRgb, heiran::domain::ColorPrimaries::AdobeRgb, "Adobe RGB"},
    };

    for (const auto &testCase : cases) {
      const auto params = heiran::ui::makeColorTransformParams(QColorSpace(testCase.qtSpace),
                                                               QColorSpace(QColorSpace::SRgb));
      const auto expected = heiran::domain::primariesConversionMatrix(
          testCase.primaries, heiran::domain::ColorPrimaries::SRgb);

      for (int index = 0; index < 9; ++index) {
        const double difference = std::abs(params.primaryMatrix[index] - expected[index]);
        QVERIFY2(difference < 0.005, qPrintable(QString("%1 [%2]: qt=%3 derived=%4")
                                                    .arg(testCase.name)
                                                    .arg(index)
                                                    .arg(params.primaryMatrix[index])
                                                    .arg(expected[index])));
      }
    }
  }

  // 広い色域から狭い色域へ移すと、負の値が出ること。
  // 値が刈られていると差が見えなくなるため、経路が値を保つことを確かめる。
  void keepsOutOfGamutValuesNegative() {
    const auto params = heiran::ui::makeColorTransformParams(QColorSpace(QColorSpace::Bt2020),
                                                             QColorSpace(QColorSpace::SRgb));
    bool hasNegative = false;
    for (const float value : params.primaryMatrix) {
      hasNegative = hasNegative || value < -0.001F;
    }
    QVERIFY2(hasNegative, "the conversion matrix was clamped to non-negative values");
  }

  // 同じ色空間どうしなら単位行列になること。
  void producesAnIdentityForTheSameSpace() {
    const auto params = heiran::ui::makeColorTransformParams(QColorSpace(QColorSpace::DisplayP3),
                                                             QColorSpace(QColorSpace::DisplayP3));
    QVERIFY(params.primariesMatch);
  }

  // 輝度の重みが色空間から出ること。値は domain 側の色度からの導出と一致する。
  // 片方だけでは正しさを確かめられないため、独立な 2 つの導出を突き合わせる。
  void derivesLumaWeightsFromTheColorSpace() {
    struct Case {
      QColorSpace::NamedColorSpace qtSpace;
      heiran::domain::ColorPrimaries primaries;
      const char *name;
    };
    const Case cases[] = {
        {QColorSpace::SRgb, heiran::domain::ColorPrimaries::SRgb, "sRGB"},
        {QColorSpace::DisplayP3, heiran::domain::ColorPrimaries::DisplayP3, "Display P3"},
        {QColorSpace::Bt2020, heiran::domain::ColorPrimaries::Rec2020, "Rec.2020"},
        {QColorSpace::AdobeRgb, heiran::domain::ColorPrimaries::AdobeRgb, "Adobe RGB"},
    };

    for (const auto &testCase : cases) {
      const auto derived = heiran::ui::lumaWeightsFor(QColorSpace(testCase.qtSpace));
      const auto expected = heiran::domain::lumaWeights(testCase.primaries);
      QVERIFY2(
          std::abs(derived.red - expected.red) < 0.005,
          qPrintable(
              QString("%1 red: %2 vs %3").arg(testCase.name).arg(derived.red).arg(expected.red)));
      QVERIFY2(std::abs(derived.green - expected.green) < 0.005,
               qPrintable(QString("%1 green: %2 vs %3")
                              .arg(testCase.name)
                              .arg(derived.green)
                              .arg(expected.green)));
      QVERIFY2(std::abs(derived.blue - expected.blue) < 0.005,
               qPrintable(QString("%1 blue: %2 vs %3")
                              .arg(testCase.name)
                              .arg(derived.blue)
                              .arg(expected.blue)));
    }
  }

  // 無効な色空間では sRGB の重みへ退避すること。
  void fallsBackToSrgbLumaWeights() {
    const auto derived = heiran::ui::lumaWeightsFor(QColorSpace());
    const auto expected = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QCOMPARE(derived.red, expected.red);
  }

  // 対応表が組めること。行列と曲線で表せる組み合わせでも、同じ結果になること。
  // 表の引き方そのものが正しいかを、計算で出せる場合と突き合わせて確かめる。
  void bakesATransformIntoALookupTable() {
    const QColorSpace source(QColorSpace::DisplayP3);
    const QColorSpace display(QColorSpace::SRgb);
    const auto lut = heiran::ui::buildColorLut(source, display);
    QVERIFY(lut != nullptr);
    QVERIFY(lut->isValid());
    QCOMPARE(lut->size, 33);

    // 表の出力は表示用の線形光。計算で出した結果と突き合わせる。
    const auto params = heiran::ui::makeColorTransformParams(source, display);
    for (const float value : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
      const auto sampled = lut->sample(value, value * 0.5F, value * 0.25F);

      const float linear[3] = {
          heiran::ui::decodeShaderTransfer(value, params.sourceTransfer, params.sourceGamma),
          heiran::ui::decodeShaderTransfer(value * 0.5F, params.sourceTransfer, params.sourceGamma),
          heiran::ui::decodeShaderTransfer(value * 0.25F, params.sourceTransfer,
                                           params.sourceGamma)};
      for (int row = 0; row < 3; ++row) {
        const float expected = params.primaryMatrix[row * 3] * linear[0] +
                               params.primaryMatrix[row * 3 + 1] * linear[1] +
                               params.primaryMatrix[row * 3 + 2] * linear[2];
        QVERIFY2(std::abs(sampled[row] - expected) < 0.01F,
                 qPrintable(QString("value=%1 row=%2 lut=%3 computed=%4")
                                .arg(value)
                                .arg(row)
                                .arg(sampled[row])
                                .arg(expected)));
      }
    }
  }

  // インテントを指定できる構成であること。lcms2 を外した場合はここが落ちる。
  void supportsRenderingIntentsInThisBuild() {
    QVERIFY2(heiran::ui::supportsRenderingIntents(),
             "this build has no Little-CMS, so intents cannot be selected");
  }

  void convertsRenderingIntentToAndFromStrings() {
    QCOMPARE(heiran::ui::renderingIntentFromString("perceptual"),
             heiran::ui::RenderingIntent::Perceptual);
    QCOMPARE(heiran::ui::renderingIntentFromString(" Absolute "),
             heiran::ui::RenderingIntent::AbsoluteColorimetric);
    // 知らない値は相対比色として扱う。
    QCOMPARE(heiran::ui::renderingIntentFromString("nonsense"),
             heiran::ui::RenderingIntent::RelativeColorimetric);
    QCOMPARE(heiran::ui::renderingIntentToString(heiran::ui::RenderingIntent::Saturation),
             QString("saturation"));
  }

  // 白色点が違う組み合わせでは、絶対比色と相対比色で結果が変わること。
  // 相対比色は白を表示先の白へ合わせ、絶対比色は合わせない。
  // sRGB は D65、ProPhoto RGB は D50 のため差が出る。
  void distinguishesAbsoluteFromRelativeColorimetric() {
    const QColorSpace source(QColorSpace::SRgb);
    const QColorSpace display(QColorSpace::ProPhotoRgb);

    const auto relative = heiran::ui::buildColorLut(
        source, display, {.intent = heiran::ui::RenderingIntent::RelativeColorimetric});
    const auto absolute = heiran::ui::buildColorLut(
        source, display, {.intent = heiran::ui::RenderingIntent::AbsoluteColorimetric});
    QVERIFY(relative != nullptr && absolute != nullptr);

    const auto relativeWhite = relative->sample(1.0F, 1.0F, 1.0F);
    const auto absoluteWhite = absolute->sample(1.0F, 1.0F, 1.0F);

    float largestDifference = 0.0F;
    for (int index = 0; index < 3; ++index) {
      largestDifference =
          std::max(largestDifference, std::abs(relativeWhite[index] - absoluteWhite[index]));
    }
    QVERIFY2(largestDifference > 0.01F,
             qPrintable(QString("relative=(%1,%2,%3) absolute=(%4,%5,%6)")
                            .arg(relativeWhite[0])
                            .arg(relativeWhite[1])
                            .arg(relativeWhite[2])
                            .arg(absoluteWhite[0])
                            .arg(absoluteWhite[1])
                            .arg(absoluteWhite[2])));
  }

  // 同じ指定なら同じ表が出ること。取っておく判断がこれに依る。
  void buildsTheSameTableForTheSameOptions() {
    const QColorSpace source(QColorSpace::DisplayP3);
    const QColorSpace display(QColorSpace::SRgb);
    const heiran::ui::ColorLutOptions options{.intent = heiran::ui::RenderingIntent::Perceptual,
                                              .blackPointCompensation = true};

    const auto first = heiran::ui::buildColorLut(source, display, options);
    const auto second = heiran::ui::buildColorLut(source, display, options);
    QVERIFY(first != nullptr && second != nullptr);
    QCOMPARE(first->values, second->values);
  }

  // 表の端が元の値をそのまま返すこと。半テクセルずれていると端がにじむ。
  void keepsTheLookupTableEndpoints() {
    const auto lut = heiran::ui::buildColorLut(QColorSpace(QColorSpace::SRgbLinear),
                                               QColorSpace(QColorSpace::SRgbLinear));
    QVERIFY(lut != nullptr);

    const auto black = lut->sample(0.0F, 0.0F, 0.0F);
    const auto white = lut->sample(1.0F, 1.0F, 1.0F);
    for (int index = 0; index < 3; ++index) {
      QVERIFY2(std::abs(black[index]) < 0.001F, qPrintable(QString::number(black[index])));
      QVERIFY2(std::abs(white[index] - 1.0F) < 0.001F, qPrintable(QString::number(white[index])));
    }
  }

  // 範囲外は端で止めること。
  void clampsLookupTableInput() {
    const auto lut = heiran::ui::buildColorLut(QColorSpace(QColorSpace::SRgbLinear),
                                               QColorSpace(QColorSpace::SRgbLinear));
    QVERIFY(lut != nullptr);
    const auto below = lut->sample(-1.0F, -1.0F, -1.0F);
    const auto above = lut->sample(2.0F, 2.0F, 2.0F);
    QVERIFY(std::abs(below[0]) < 0.001F);
    QVERIFY(std::abs(above[0] - 1.0F) < 0.001F);
  }

  // 行列と曲線では表せない色空間でも表を組めること。これが表を使う理由。
  void bakesTransformsThatCannotBeExpressedAsAMatrix() {
    const QColorSpace pq(QColorSpace::Bt2100Pq);
    QVERIFY(!heiran::ui::isShaderRepresentable(pq));

    const auto lut = heiran::ui::buildColorLut(pq, QColorSpace(QColorSpace::SRgb));
    QVERIFY(lut != nullptr);
    QVERIFY(lut->isValid());

    // PQ は明るさが上へ大きく伸びる曲線のため、符号化された値の中ほどでも
    // 線形光では小さい値になる。
    const auto middle = lut->sample(0.5F, 0.5F, 0.5F);
    const auto top = lut->sample(1.0F, 1.0F, 1.0F);
    QVERIFY2(top[0] > middle[0],
             qPrintable(QString("middle=%1 top=%2").arg(middle[0]).arg(top[0])));
  }

  // シェーダで扱えるかどうかを判定できること。
  void detectsShaderRepresentableSpaces() {
    QVERIFY(heiran::ui::isShaderRepresentable(QColorSpace(QColorSpace::SRgb)));
    QVERIFY(heiran::ui::isShaderRepresentable(QColorSpace(QColorSpace::SRgbLinear)));
    QVERIFY(heiran::ui::isShaderRepresentable(QColorSpace(QColorSpace::DisplayP3)));
    QVERIFY(heiran::ui::isShaderRepresentable(
        QColorSpace(QColorSpace::Primaries::SRgb, QColorSpace::TransferFunction::Gamma, 2.2F)));

    QVERIFY(!heiran::ui::isShaderRepresentable(QColorSpace()));
    // PQ と HLG の曲線はシェーダに載せない。
    QVERIFY(!heiran::ui::isShaderRepresentable(QColorSpace(QColorSpace::Bt2100Pq)));
    QVERIFY(!heiran::ui::isShaderRepresentable(QColorSpace(QColorSpace::Bt2100Hlg)));
  }
};

QTEST_MAIN(ColorManagementTests)

#include "ColorManagementTests.moc"
