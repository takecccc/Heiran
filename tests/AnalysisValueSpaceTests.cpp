#include "heiran/ui/AnalysisSampler.hpp"

#include "heiran/infra/ImageDecoder.hpp"

#include <QImage>
#include <QtTest>
#include <cmath>

namespace {

heiran::ui::AnalysisSampler makeSampler(const QImage &image,
                                        heiran::ui::AnalysisValueSpace valueSpace) {
  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();
  sampler.valueSpace = valueSpace;
  sampler.sourceTransfer = heiran::ui::ShaderTransfer::SRgb;
  return sampler;
}

QImage makeFlatImage(QSize size, QColor color) {
  QImage image(size, QImage::Format_RGBA8888);
  image.fill(color);
  return image;
}

}  // namespace

class AnalysisValueSpaceTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void convertsToAndFromStrings() {
    QCOMPARE(heiran::ui::analysisValueSpaceFromString("linear"),
             heiran::ui::AnalysisValueSpace::Linear);
    QCOMPARE(heiran::ui::analysisValueSpaceFromString(" Source "),
             heiran::ui::AnalysisValueSpace::Source);
    // 知らない値はファイルのままとして扱う。
    QCOMPARE(heiran::ui::analysisValueSpaceFromString("nonsense"),
             heiran::ui::AnalysisValueSpace::Source);
    QCOMPARE(heiran::ui::analysisValueSpaceToString(heiran::ui::AnalysisValueSpace::Linear),
             QString("linear"));
  }

  // 既定ではファイルの値をそのまま返すこと。既存の数値が変わらない条件になる。
  void keepsSourceValuesByDefault() {
    const QImage image = makeFlatImage(QSize(4, 4), QColor(128, 128, 128));
    const auto sampler = makeSampler(image, heiran::ui::AnalysisValueSpace::Source);

    QVERIFY(std::abs(sampler.sampleRgbaAtDisplayPixel(1, 1, 0) - 128.0F / 255.0F) < 0.005F);
  }

  // 線形を選ぶと符号化を戻すこと。sRGB の 0.5 は線形では約 0.2140。
  void decodesToLinearLight() {
    const QImage image = makeFlatImage(QSize(4, 4), QColor(128, 128, 128));
    const auto sampler = makeSampler(image, heiran::ui::AnalysisValueSpace::Linear);

    const float value = sampler.sampleRgbaAtDisplayPixel(1, 1, 0);
    QVERIFY2(std::abs(value - 0.2158F) < 0.005F, qPrintable(QString::number(value)));
  }

  // まとめて作った平面も同じ空間で返すこと。
  // 画素ごとの取得とずれると、ヒストグラムと画素値表示が食い違う。
  void buildsChannelPlanesInTheSameSpace() {
    const QImage image = makeFlatImage(QSize(8, 6), QColor(128, 64, 32));
    const auto sampler = makeSampler(image, heiran::ui::AnalysisValueSpace::Linear);

    const QVector<float> plane = sampler.buildChannelPlane(image.rect(), 0);
    QCOMPARE(plane.size(), 48);
    QVERIFY2(std::abs(plane.at(0) - sampler.sampleRgbaAtDisplayPixel(0, 0, 0)) < 0.0001F,
             qPrintable(QString("plane=%1 pixel=%2")
                            .arg(plane.at(0))
                            .arg(sampler.sampleRgbaAtDisplayPixel(0, 0, 0))));
  }

  // 輝度は線形へ戻してから重みを掛けること。
  // 符号化された値へ重みを掛けてから戻したものとは別になる。
  void weightsLumaAfterDecoding() {
    const QImage image = makeFlatImage(QSize(4, 4), QColor(255, 0, 0));
    const auto sampler = makeSampler(image, heiran::ui::AnalysisValueSpace::Linear);

    // 純赤の線形光での輝度は sRGB の重みそのもの。
    const double luma = sampler.lumaAtDisplayPixel(1, 1) / 255.0;
    QVERIFY2(std::abs(luma - sampler.lumaWeights.red) < 0.005,
             qPrintable(QString("luma=%1 weight=%2").arg(luma).arg(sampler.lumaWeights.red)));
  }

  // 輝度の平面も画素ごとの取得と一致すること。
  void buildsLumaPlanesInTheSameSpace() {
    const QImage image = makeFlatImage(QSize(8, 6), QColor(200, 120, 40));
    for (const auto space :
         {heiran::ui::AnalysisValueSpace::Source, heiran::ui::AnalysisValueSpace::Linear}) {
      const auto sampler = makeSampler(image, space);
      const QVector<float> plane = sampler.buildLumaPlane(image.rect());
      QCOMPARE(plane.size(), 48);
      QVERIFY2(std::abs(plane.at(0) - static_cast<float>(sampler.lumaAtDisplayPixel(0, 0))) < 0.01F,
               qPrintable(QString("space=%1 plane=%2 pixel=%3")
                              .arg(static_cast<int>(space))
                              .arg(plane.at(0))
                              .arg(sampler.lumaAtDisplayPixel(0, 0))));
    }
  }

  // 3 チャネルを 1 回で作る経路が、チャネルごとの経路と同じ平面を返すこと。
  // 解析用バッファの型 (8bit 画像、16bit RGB、float の灰色)、値の空間、回転、切り出しの組で確かめる。
  void buildsRgbPlanesLikeThreeChannelPlanes_data() {
    QTest::addColumn<int>("source");  // 0: 表示用画像だけ / 1: uint16 RGB / 2: float32 Gray
    QTest::addColumn<bool>("linear");
    QTest::addColumn<int>("quarterTurns");
    QTest::addColumn<bool>("subRegion");
    for (int source = 0; source < 3; ++source) {
      for (const bool linear : {false, true}) {
        for (const int turns : {0, 1}) {
          for (const bool region : {false, true}) {
            QTest::newRow(qPrintable(QString("src%1 linear%2 turns%3 region%4")
                                         .arg(source)
                                         .arg(linear)
                                         .arg(turns)
                                         .arg(region)))
                << source << linear << turns << region;
          }
        }
      }
    }
  }

  void buildsRgbPlanesLikeThreeChannelPlanes() {
    QFETCH(int, source);
    QFETCH(bool, linear);
    QFETCH(int, quarterTurns);
    QFETCH(bool, subRegion);

    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        image.setPixelColor(x, y, QColor((x * 19) & 255, (y * 37) & 255, (x * y * 5) & 255));
      }
    }
    auto sampler = makeSampler(image, linear ? heiran::ui::AnalysisValueSpace::Linear
                                             : heiran::ui::AnalysisValueSpace::Source);
    if (source == 1) {
      heiran::infra::PixelBuffer buffer;
      buffer.width = kWidth;
      buffer.height = kHeight;
      buffer.channelCount = 3;
      buffer.sampleType = heiran::infra::PixelSampleType::UInt16;
      buffer.colorModel = heiran::infra::PixelColorModel::RGB;
      buffer.bytesPerRow = kWidth * 3 * 2;
      buffer.storage = QSharedPointer<QByteArray>::create(buffer.bytesPerRow * kHeight, '\0');
      auto *values = reinterpret_cast<quint16 *>(buffer.storage->data());
      for (int index = 0; index < kWidth * kHeight * 3; ++index) {
        values[index] = static_cast<quint16>((index * 977) & 0xFFFF);
      }
      sampler.decodedImage.analysisBuffer = buffer;
    } else if (source == 2) {
      heiran::infra::PixelBuffer buffer;
      buffer.width = kWidth;
      buffer.height = kHeight;
      buffer.channelCount = 1;
      buffer.sampleType = heiran::infra::PixelSampleType::Float32;
      buffer.colorModel = heiran::infra::PixelColorModel::Gray;
      buffer.bytesPerRow = kWidth * 4;
      buffer.storage = QSharedPointer<QByteArray>::create(buffer.bytesPerRow * kHeight, '\0');
      auto *values = reinterpret_cast<float *>(buffer.storage->data());
      for (int index = 0; index < kWidth * kHeight; ++index) {
        values[index] = static_cast<float>(index) / 40.0F;
      }
      sampler.decodedImage.analysisBuffer = buffer;
    }
    sampler.orientation.rotationQuarterTurns = quarterTurns;
    QSize displaySize = image.size();
    if (quarterTurns % 2 != 0) {
      displaySize.transpose();
    }
    const QRect region = subRegion ? QRect(2, 1, displaySize.width() - 3, displaySize.height() - 2)
                                   : QRect(QPoint(0, 0), displaySize);

    QVector<float> planes[3];
    QVERIFY(sampler.buildRgbPlanes(region, planes));
    for (int channel = 0; channel < 3; ++channel) {
      const QVector<float> expected = sampler.buildChannelPlane(region, channel);
      QCOMPARE(planes[channel].size(), expected.size());
      for (qsizetype index = 0; index < expected.size(); ++index) {
        QVERIFY2(planes[channel].at(index) == expected.at(index),
                 qPrintable(QString("channel %1 index %2").arg(channel).arg(index)));
      }
    }
  }
};

QTEST_MAIN(AnalysisValueSpaceTests)

#include "AnalysisValueSpaceTests.moc"
