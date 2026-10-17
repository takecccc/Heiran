// 解析用バッファから表示用の 8bit 画像への変換 (renderDisplayImage)。
//
// 4K の画像で 800 ms かかっていたため、標本ごとの計算を、表の引きと並列の走査へ置き換えた。
// 置き換えても結果が変わらないことを、素朴な計算 (1 標本ずつ、0-1 へ収めて丸める) との一致で守る。

#include "heiran/infra/ImageDecoder.hpp"

#include <QFloat16>
#include <QtTest>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace heiran::infra;

namespace {

// 素朴な計算。アルファは露出の対象にしない。NaN は 0。
int expectedByte(float value, float gain, bool isAlpha) {
  float scaled = isAlpha ? value : value * gain;
  if (!(scaled > 0.0F)) {
    scaled = 0.0F;
  }
  scaled = std::min(scaled, 1.0F);
  return static_cast<int>(std::lround(scaled * 255.0F));
}

// 決まった並びの疑似乱数 (0-1)。
float noise(quint32 *state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<float>(*state >> 8) / static_cast<float>(1u << 24);
}

PixelColorModel modelFor(int channels) {
  switch (channels) {
    case 1:
      return PixelColorModel::Gray;
    case 2:
      return PixelColorModel::GrayAlpha;
    case 3:
      return PixelColorModel::RGB;
    default:
      return PixelColorModel::RGBA;
  }
}

// 出力の RGBA の 4 つ目までの、取得元のチャンネル。-1 は定数 (アルファは不透明)。
int sourceChannel(int channels, int outputChannel) {
  switch (channels) {
    case 1:
      return outputChannel < 3 ? 0 : -1;
    case 2:
      return outputChannel < 3 ? 0 : 1;
    case 3:
      return outputChannel < 3 ? outputChannel : -1;
    default:
      return outputChannel;
  }
}

struct Sample {
  PixelSampleType type;
  int bytes;
};

// 値の列 (0-1 を中心に、はみ出す値、負の値、NaN、無限大を混ぜる) を作り、型に合わせた
// バイト列へ詰める。素朴な計算に渡す値は、型で表せる値 (丸めた後) にそろえて返す。
void buildBuffer(PixelSampleType type, int width, int height, int channels,
                 std::vector<float> *values, PixelBuffer *buffer) {
  const int sampleBytes = bytesPerSample(type);
  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(static_cast<qsizetype>(width) * height * channels * sampleBytes);
  values->assign(static_cast<size_t>(width) * height * channels, 0.0F);

  quint32 state = 12345u;
  for (size_t index = 0; index < values->size(); ++index) {
    float value = noise(&state) * 1.4F - 0.2F;  // -0.2 から 1.2
    switch (index % 37) {
      case 5:
        value = std::numeric_limits<float>::quiet_NaN();
        break;
      case 11:
        value = std::numeric_limits<float>::infinity();
        break;
      case 17:
        value = -std::numeric_limits<float>::infinity();
        break;
      case 23:
        value = 0.5F;
        break;
      default:
        break;
    }

    char *target = storage->data() + index * static_cast<size_t>(sampleBytes);
    switch (type) {
      case PixelSampleType::UInt8: {
        const float clamped = value > 0.0F ? std::min(value, 1.0F) : 0.0F;
        const uchar byte = static_cast<uchar>(std::lround(clamped * 255.0F));
        std::memcpy(target, &byte, 1);
        value = static_cast<float>(byte) / 255.0F;
        break;
      }
      case PixelSampleType::UInt16: {
        const float clamped = value > 0.0F ? std::min(value, 1.0F) : 0.0F;
        const quint16 word = static_cast<quint16>(std::lround(clamped * 65535.0F));
        std::memcpy(target, &word, 2);
        value = static_cast<float>(word) / 65535.0F;
        break;
      }
      case PixelSampleType::Float16: {
        const qfloat16 half(value);
        std::memcpy(target, &half, 2);
        value = static_cast<float>(half);
        break;
      }
      default:
        std::memcpy(target, &value, 4);
        break;
    }
    (*values)[index] = value;
  }

  *buffer = {.storage = storage,
             .bytesPerRow = static_cast<qsizetype>(width) * channels * sampleBytes,
             .width = width,
             .height = height,
             .channelCount = channels,
             .sampleType = type,
             .colorModel = modelFor(channels)};
}

}  // namespace

class DisplayImageConversionTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void matchesTheNaiveCalculationForEveryTypeAndLayout_data() {
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("channels");
    QTest::addColumn<float>("gain");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");

    const std::pair<const char *, PixelSampleType> types[] = {
        {"uint8", PixelSampleType::UInt8},
        {"uint16", PixelSampleType::UInt16},
        {"float16", PixelSampleType::Float16},
        {"float32", PixelSampleType::Float32}};
    for (const auto &[name, type] : types) {
      for (const int channels : {1, 2, 3, 4}) {
        for (const float gain : {1.0F, 2.5F, 0.25F}) {
          // 小さい画像 (帯が 1 つ) と、行がたくさんある画像 (帯に分かれる)。
          QTest::newRow(
              qPrintable(QString("%1 ch%2 gain%3 small").arg(name).arg(channels).arg(gain)))
              << static_cast<int>(type) << channels << gain << 7 << 3;
          QTest::newRow(
              qPrintable(QString("%1 ch%2 gain%3 tall").arg(name).arg(channels).arg(gain)))
              << static_cast<int>(type) << channels << gain << 33 << 211;
        }
      }
    }
  }

  void matchesTheNaiveCalculationForEveryTypeAndLayout() {
    QFETCH(int, type);
    QFETCH(int, channels);
    QFETCH(float, gain);
    QFETCH(int, width);
    QFETCH(int, height);

    std::vector<float> values;
    PixelBuffer buffer;
    buildBuffer(static_cast<PixelSampleType>(type), width, height, channels, &values, &buffer);

    const QImage image = renderDisplayImage(buffer, gain);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(width, height));

    int mismatches = 0;
    for (int y = 0; y < height; ++y) {
      const uchar *row = image.constScanLine(y);
      for (int x = 0; x < width; ++x) {
        for (int outputChannel = 0; outputChannel < 4; ++outputChannel) {
          const int source = sourceChannel(channels, outputChannel);
          const bool isAlpha = outputChannel == 3;
          const int expected =
              source < 0
                  ? 255
                  : expectedByte(values[(static_cast<size_t>(y) * width + x) * channels + source],
                                 gain, isAlpha);
          if (row[x * 4 + outputChannel] != expected) {
            ++mismatches;
          }
        }
      }
    }
    QCOMPARE(mismatches, 0);
  }

  // 無効なバッファは、空の画像を返す。
  void returnsNothingForAnInvalidBuffer() {
    QVERIFY(renderDisplayImage(PixelBuffer(), 1.0F).isNull());
  }
};

QTEST_MAIN(DisplayImageConversionTests)

#include "DisplayImageConversionTests.moc"
