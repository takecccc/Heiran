#include "heiran/domain/ColorSpace.hpp"
#include "heiran/ui/AnalysisSampler.hpp"
#include "heiran/ui/PixelInfoText.hpp"

#include <QtTest>

namespace {

// RGBA を固定で返す取得関数。
std::function<float(int)> constantSample(float red, float green, float blue, float alpha) {
  return [red, green, blue, alpha](int channel) {
    const float values[4] = {red, green, blue, alpha};
    return values[channel];
  };
}

QStringList labelsOf(const QVector<heiran::ui::PixelChannelValue> &values) {
  QStringList labels;
  for (const auto &value : values) {
    labels.push_back(value.label);
  }
  return labels;
}

// 輝度を要求しない経路で呼ばれたら失敗させる。輝度の算出は場所によって重みが
// 違うため、要らない場面で呼ばれていないことを確かめておく。
float noLuma() {
  QTest::qFail("the luma provider was called for a mode that does not need it", __FILE__, __LINE__);
  return 0.0F;
}

}  // namespace

class PixelInfoTextTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ステータスバー用の 1 行: 位置と、各セルの値。桁揃えの 0 は落とす。
  void summarizesThePixelValuesForTheStatusBar() {
    const QStringList lines = {
        "page=1",
        "x=87 y=33",
        "[ 1] (  87,   33): R= 242.000 G= 244.000 B= 247.000",
        "[ 2] (  87,   33): R=  52.500 G= 130.000 B=  72.000",
    };
    QCOMPARE(heiran::ui::buildPixelStatusSummary(87, 33, lines),
             QString("x=87 y=33  [1] 242 244 247  [2] 52.5 130 72"));
  }

  // 1 チャネルの表示 (Gray など) と、0-1 の単位。
  void summarizesSingleChannelAndNormalizedValues() {
    const QStringList lines = {"[ 1] (   1,    2): Gray=0.5000", "[ 2] (   1,    2): R= 0.250 G= 1.000 B= 0.000"};
    QCOMPARE(heiran::ui::buildPixelStatusSummary(1, 2, lines),
             QString("x=1 y=2  [1] 0.5  [2] 0.25 1 0"));
  }

  // 値の無いセルは `-`、値の行でないもの (読込中の表示) は飛ばす。多いときは、先頭の数個と、残りの数。
  void marksCellsWithoutAValueAndLimitsTheNumberShown() {
    const QStringList lines = {"[ 1] (  5,  6): out-of-bounds", "[ 2]: loading",
                               "[ 3] (  5,  6): R= 1 G= 2 B= 3", "[ 4] (  5,  6): R= 4 G= 5 B= 6",
                               "[ 5] (  5,  6): R= 7 G= 8 B= 9"};
    QCOMPARE(heiran::ui::buildPixelStatusSummary(5, 6, lines, 2),
             QString("x=5 y=6  [1] -  [3] 1 2 3  +2"));
    // 行が無ければ、位置だけ。
    QCOMPARE(heiran::ui::buildPixelStatusSummary(0, 0, {}), QString("x=0 y=0"));
  }

  // 色表示では、色モデルによって出すチャネルが変わること。
  void selectsChannelsForTheColorMode() {
    const auto sample = constantSample(0.1F, 0.2F, 0.3F, 0.4F);

    const auto rgb = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Color, false,
                                                          false, sample, noLuma);
    QCOMPARE(labelsOf(rgb), QStringList({"R", "G", "B"}));

    const auto rgba = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Color, false,
                                                           true, sample, noLuma);
    QCOMPARE(labelsOf(rgba), QStringList({"R", "G", "B", "A"}));

    const auto gray = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Color, true,
                                                           false, sample, noLuma);
    QCOMPARE(labelsOf(gray), QStringList({"Gray"}));
    QCOMPARE(gray.first().value, 0.1F);
  }

  // 単一チャネル表示では、そのチャネルだけを出すこと。
  void selectsASingleChannelForTheOtherModes() {
    const auto sample = constantSample(0.1F, 0.2F, 0.3F, 0.4F);

    const auto red = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Red, false, true,
                                                          sample, noLuma);
    QCOMPARE(labelsOf(red), QStringList({"Red"}));
    QCOMPARE(red.first().value, 0.1F);

    const auto alpha = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Alpha, false,
                                                            true, sample, noLuma);
    QCOMPARE(alpha.first().value, 0.4F);

    // 輝度表示は呼び出し側から渡された値をそのまま使う。求め方は場所によって違う。
    const auto luma = heiran::ui::selectPixelChannelValues(heiran::ui::ChannelMode::Gray, false,
                                                           true, sample, []() { return 0.75F; });
    QCOMPARE(labelsOf(luma), QStringList({"Gray"}));
    QCOMPARE(luma.first().value, 0.75F);
  }

  // 輝度は渡された重みで求めること。色空間から導いた重みがそのまま効く。
  void appliesTheGivenLumaWeights() {
    const auto srgb = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QVERIFY(std::abs(heiran::ui::luminanceOf(1.0F, 0.0F, 0.0F, srgb) - 0.2126F) < 0.001F);
    QVERIFY(std::abs(heiran::ui::luminanceOf(0.0F, 1.0F, 0.0F, srgb) - 0.7152F) < 0.001F);
    QVERIFY(std::abs(heiran::ui::luminanceOf(0.0F, 0.0F, 1.0F, srgb) - 0.0722F) < 0.001F);

    const auto p3 = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::DisplayP3);
    QVERIFY(std::abs(heiran::ui::luminanceOf(1.0F, 0.0F, 0.0F, p3) - 0.2290F) < 0.001F);
  }

  // 解析入力の既定の重みが sRGB の導出と一致すること。
  // 既定値を直に書いているため、色度からの導出とずれていないか確かめる。
  void defaultsAnalysisWeightsToSrgb() {
    const heiran::ui::AnalysisSampler sampler;
    const auto expected = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::SRgb);
    QVERIFY(std::abs(sampler.lumaWeights.red - expected.red) < 0.0001);
    QVERIFY(std::abs(sampler.lumaWeights.green - expected.green) < 0.0001);
    QVERIFY(std::abs(sampler.lumaWeights.blue - expected.blue) < 0.0001);
  }

  // 画素値表示とルーペが同じ輝度を出すこと。
  //
  // 画素値表示は `luminanceOf()`、ルーペと差分は
  // `AnalysisSampler::lumaAtDisplayPixel()` を通る。以前はこの 2 つで重みが
  // 違っており、同じ画素でも値が食い違っていた。
  void reportsTheSameLumaThroughBothPaths() {
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(200, 120, 40));

    heiran::ui::AnalysisSampler sampler;
    sampler.decodedImage = {.displayImage = image};
    sampler.sourceSize = image.size();
    sampler.lumaWeights = heiran::domain::lumaWeights(heiran::domain::ColorPrimaries::DisplayP3);

    const double fromSampler = sampler.lumaAtDisplayPixel(1, 1);
    const float fromText = heiran::ui::luminanceOf(200.0F / 255.0F, 120.0F / 255.0F, 40.0F / 255.0F,
                                                   sampler.lumaWeights);
    QVERIFY2(std::abs(fromSampler / 255.0 - static_cast<double>(fromText)) < 0.005,
             qPrintable(QString("sampler=%1 text=%2").arg(fromSampler / 255.0).arg(fromText)));
  }

  // 1 チャネルのときはラベルの桁揃えを入れないこと。ラベルは 1 つしか出ないため、
  // 揃える相手がいない。値の右詰めは formatPixelSample が行う。
  void formatsASingleChannelWithoutLabelPadding() {
    const QVector<heiran::ui::PixelChannelValue> values = {{.label = "Gray", .value = 0.5F}};
    const auto format = heiran::ui::PixelValueFormat::Normalized255;
    QCOMPARE(heiran::ui::formatPixelChannelValues(values, format),
             QString("Gray=") + heiran::ui::formatPixelSample(0.5F, format));
  }

  // 複数チャネルのときはラベルを 2 桁へ揃えること。1 文字のラベルでも位置がずれない。
  void alignsMultipleChannelLabels() {
    const QVector<heiran::ui::PixelChannelValue> values = {{.label = "R", .value = 0.5F},
                                                           {.label = "G", .value = 0.5F},
                                                           {.label = "B", .value = 0.5F}};
    const auto format = heiran::ui::PixelValueFormat::Normalized255;
    const QString sample = heiran::ui::formatPixelSample(0.5F, format);
    // ラベルは "R=" で 2 桁ちょうど。その後に区切りの空白が 1 つ入る。
    QCOMPARE(heiran::ui::formatPixelChannelValues(values, format),
             QString("R= %1 G= %1 B= %1").arg(sample));
  }

  // 位置は 4 桁で右詰めにすること。行が縦に揃う。
  void padsThePosition() {
    QCOMPARE(heiran::ui::formatPixelPosition(0, 7), QString("(   0,    7)"));
    QCOMPARE(heiran::ui::formatPixelPosition(1234, 5678), QString("(1234, 5678)"));
  }

  void formatsTheImageIndexLabel() {
    QCOMPARE(heiran::ui::formatImageIndexLabel(0), QString("[ 1]"));
    QCOMPARE(heiran::ui::formatImageIndexLabel(9), QString("[10]"));
    QCOMPARE(heiran::ui::formatImageIndexLabel(-1), QString("[? ]"));
  }

  // 画像を持たないセルは状態を出すこと。
  void buildsTheStatusLine() {
    heiran::ui::CellStatusInfo statusInfo = heiran::ui::CellStatusInfo::loading();
    const QString withoutReason = heiran::ui::buildPixelInfoStatusLine(0, statusInfo);
    QVERIFY2(withoutReason.startsWith("[ 1]: "), qPrintable(withoutReason));
    QVERIFY2(!withoutReason.contains('('), qPrintable(withoutReason));

    statusInfo.reason = "file not found";
    const QString withReason = heiran::ui::buildPixelInfoStatusLine(0, statusInfo);
    QVERIFY2(withReason.endsWith("(file not found)"), qPrintable(withReason));
  }

  void buildsTheOutOfBoundsLine() {
    const QString text = heiran::ui::buildPixelInfoOutOfBoundsLine(1, QPoint(-3, 42));
    QCOMPARE(text, QString("[ 2] (  -3,   42): out-of-bounds"));
  }

  void buildsTheValueLine() {
    const QVector<heiran::ui::PixelChannelValue> values = {{.label = "Gray", .value = 0.0F}};
    const QString text = heiran::ui::buildPixelInfoValueLine(
        0, QPoint(5, 6), values, heiran::ui::PixelValueFormat::Normalized255);
    QVERIFY2(text.startsWith("[ 1] (   5,    6): Gray="), qPrintable(text));
  }
};

QTEST_MAIN(PixelInfoTextTests)

#include "PixelInfoTextTests.moc"
