#include "heiran/ui/ColorManagement.hpp"
#include "heiran/ui/DisplayTransform.hpp"
#include "heiran/ui/GpuImageRenderer.hpp"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QtTest>

using heiran::ui::DisplayTransformParams;

namespace {

heiran::infra::DecodedImage makeDecodedImage(int width, int height) {
  QImage image(width, height, QImage::Format_RGBA8888);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.setPixelColor(x, y,
                          QColor((x * 255) / std::max(1, width - 1),
                                 (y * 255) / std::max(1, height - 1), 128, 255));
    }
  }
  return {.displayImage = image};
}

// 1.0 を超える値を持つ浮動小数画像。表示用画像へ丸めると白へ潰れる領域を作る。
heiran::infra::DecodedImage makeFloatDecodedImage(int width, int height) {
  heiran::infra::PixelBuffer buffer;
  buffer.width = width;
  buffer.height = height;
  buffer.channelCount = 3;
  buffer.sampleType = heiran::infra::PixelSampleType::Float32;
  buffer.colorModel = heiran::infra::PixelColorModel::RGB;
  buffer.bytesPerRow = static_cast<qsizetype>(width) * 3 * static_cast<qsizetype>(sizeof(float));

  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(buffer.bytesPerRow * height);
  auto *values = reinterpret_cast<float *>(storage->data());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float base = 4.0F * static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
      float *pixel = values + (static_cast<qsizetype>(y) * width + x) * 3;
      pixel[0] = base;
      pixel[1] = base * 0.5F;
      pixel[2] = base * 0.25F;
    }
  }
  buffer.storage = storage;

  heiran::infra::DecodedImage decodedImage;
  decodedImage.analysisBuffer = buffer;
  decodedImage.displayImage = heiran::infra::renderDisplayImage(buffer, 1.0F);
  decodedImage.sourceSampleType = heiran::infra::PixelSampleType::Float32;
  decodedImage.sourceColorModel = heiran::infra::PixelColorModel::RGB;
  decodedImage.sourceChannelCount = 3;
  return decodedImage;
}

// 16bit 整数の画像。正規化整数のテクスチャとして載る経路を通る。
heiran::infra::DecodedImage makeUInt16DecodedImage(int width, int height, int channelCount) {
  heiran::infra::PixelBuffer buffer;
  buffer.width = width;
  buffer.height = height;
  buffer.channelCount = channelCount;
  buffer.sampleType = heiran::infra::PixelSampleType::UInt16;
  buffer.colorModel = channelCount >= 4 ? heiran::infra::PixelColorModel::RGBA
                                        : heiran::infra::PixelColorModel::RGB;
  buffer.bytesPerRow =
      static_cast<qsizetype>(width) * channelCount * static_cast<qsizetype>(sizeof(quint16));

  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(buffer.bytesPerRow * height);
  auto *values = reinterpret_cast<quint16 *>(storage->data());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      quint16 *pixel = values + (static_cast<qsizetype>(y) * width + x) * channelCount;
      pixel[0] = static_cast<quint16>((x * 65535) / std::max(1, width - 1));
      pixel[1] = static_cast<quint16>((y * 65535) / std::max(1, height - 1));
      pixel[2] = 32768;
      if (channelCount >= 4) {
        pixel[3] = 65535;
      }
    }
  }
  buffer.storage = storage;

  heiran::infra::DecodedImage decodedImage;
  decodedImage.analysisBuffer = buffer;
  decodedImage.displayImage = heiran::infra::renderDisplayImage(buffer, 1.0F);
  decodedImage.sourceSampleType = heiran::infra::PixelSampleType::UInt16;
  decodedImage.sourceColorModel = buffer.colorModel;
  decodedImage.sourceChannelCount = channelCount;
  return decodedImage;
}

// half float の画像。型を変えずに `RGBA16F` へ載る経路を通る。
// 1.0 を超える値を持たせ、丸めや詰め直しで潰れないことを確かめられるようにする。
heiran::infra::DecodedImage makeFloat16DecodedImage(int width, int height, int channelCount) {
  heiran::infra::PixelBuffer buffer;
  buffer.width = width;
  buffer.height = height;
  buffer.channelCount = channelCount;
  buffer.sampleType = heiran::infra::PixelSampleType::Float16;
  buffer.colorModel = channelCount >= 4 ? heiran::infra::PixelColorModel::RGBA
                                        : heiran::infra::PixelColorModel::RGB;
  buffer.bytesPerRow =
      static_cast<qsizetype>(width) * channelCount * static_cast<qsizetype>(sizeof(qfloat16));

  auto storage = QSharedPointer<QByteArray>::create();
  storage->resize(buffer.bytesPerRow * height);
  auto *values = reinterpret_cast<qfloat16 *>(storage->data());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float base = 4.0F * static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
      qfloat16 *pixel = values + (static_cast<qsizetype>(y) * width + x) * channelCount;
      pixel[0] = qfloat16(base);
      pixel[1] = qfloat16(base * 0.5F);
      pixel[2] = qfloat16(static_cast<float>(y) / static_cast<float>(std::max(1, height - 1)));
      if (channelCount >= 4) {
        pixel[3] = qfloat16(1.0F);
      }
    }
  }
  buffer.storage = storage;

  heiran::infra::DecodedImage decodedImage;
  decodedImage.analysisBuffer = buffer;
  decodedImage.displayImage = heiran::infra::renderDisplayImage(buffer, 1.0F);
  decodedImage.sourceSampleType = heiran::infra::PixelSampleType::Float16;
  decodedImage.sourceColorModel = buffer.colorModel;
  decodedImage.sourceChannelCount = channelCount;
  return decodedImage;
}

// GPU の出力と CPU の出力を比べる。丸めの差は許容する。
int maxChannelDifference(const QImage &left, const QImage &right) {
  if (left.size() != right.size()) {
    return 255;
  }

  int maximum = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = right.pixelColor(x, y);
      maximum = std::max({maximum, std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                          std::abs(a.blue() - b.blue())});
    }
  }
  return maximum;
}

}  // namespace

class GpuImageRendererTests : public QObject {
  Q_OBJECT

private:
  QOffscreenSurface surface_;
  QOpenGLContext context_;
  bool contextReady_ = false;

  // 変換後の画像を GPU で作る。表示先はフレームバッファ全体とする。
  QImage renderOnGpu(const heiran::infra::DecodedImage &decodedImage,
                     const DisplayTransformParams &params) {
    heiran::ui::GpuImageRenderer renderer;
    if (!renderer.initialize()) {
      return {};
    }
    renderer.setImage(decodedImage);

    const QSize size = renderer.displaySize(params);
    QOpenGLFramebufferObject frameBuffer(size);
    if (!frameBuffer.bind()) {
      return {};
    }

    renderer.draw(params, QRectF(QPointF(0, 0), QSizeF(size)), size, 1.0, false,
                  {.solidColor = Qt::white});
    frameBuffer.release();
    return frameBuffer.toImage();
  }

private Q_SLOTS:
  void initTestCase() {
    surface_.create();
    QVERIFY(surface_.isValid());
    contextReady_ = context_.create() && context_.makeCurrent(&surface_);
    if (!contextReady_) {
      QSKIP("An OpenGL context is not available in this environment.");
    }
  }

  void cleanupTestCase() {
    if (contextReady_) {
      context_.doneCurrent();
    }
  }

  void reportsDisplaySizeWithRotation() {
    heiran::ui::GpuImageRenderer renderer;
    renderer.setImage(makeDecodedImage(8, 4));

    QCOMPARE(renderer.displaySize({}), QSize(8, 4));
    QCOMPARE(renderer.displaySize({.rotationQuarterTurns = 1}), QSize(4, 8));
    QCOMPARE(renderer.displaySize({.rotationQuarterTurns = 2}), QSize(8, 4));
  }

  void compositesCustomBackgroundsAtEveryDeviceScale() {
    for (const double ratio : {1.0, 1.25, 1.5, 2.0}) {
      for (const auto pattern : {heiran::ui::TransparencyPattern::Solid,
                                 heiran::ui::TransparencyPattern::Checkerboard}) {
        for (const int alpha : {0, 128, 255}) {
          QImage image(33, 25, QImage::Format_RGBA8888);
          const QColor foreground(200, 80, 40, alpha);
          image.fill(foreground);
          heiran::ui::GpuImageRenderer renderer;
          QVERIFY(renderer.initialize());
          renderer.setImage({.displayImage = image});
          const heiran::ui::TransparencyBackground background{.pattern = pattern,
                                                              .solidColor = QColor(20, 40, 60),
                                                              .checkerColor1 = QColor(30, 60, 90),
                                                              .checkerColor2 =
                                                                  QColor(180, 200, 220)};
          const QSize physicalSize(qRound(image.width() * ratio), qRound(image.height() * ratio));
          QOpenGLFramebufferObject buffer(physicalSize);
          QVERIFY(buffer.bind());
          renderer.draw({}, QRectF(QPointF(0, 0), QSizeF(image.size())), image.size(), ratio, false,
                        background);
          buffer.release();
          const QImage actual = buffer.toImage();
          QVERIFY(!actual.isNull());
          for (int y = 0; y < actual.height(); ++y) {
            for (int x = 0; x < actual.width(); ++x) {
              const QColor base =
                  background.colorAt(QPoint(qFloor((x + 0.5) / ratio), qFloor((y + 0.5) / ratio)));
              const QColor pixel = actual.pixelColor(x, y);
              const double a = alpha / 255.0;
              QVERIFY(std::abs(pixel.red() - (foreground.red() * a + base.red() * (1.0 - a))) <= 2);
              QVERIFY(std::abs(pixel.green() -
                               (foreground.green() * a + base.green() * (1.0 - a))) <= 2);
              QVERIFY(std::abs(pixel.blue() - (foreground.blue() * a + base.blue() * (1.0 - a))) <=
                      2);
            }
          }
        }
      }
    }
  }

  // VRAM の使用量を把握するための集計。ミップマップ分を含む。
  void reportsTextureBytes() {
    heiran::ui::GpuImageRenderer renderer;
    QVERIFY(renderer.initialize());
    QCOMPARE(renderer.textureBytes(), 0);

    // 8bit 画像は RGBA8。16x16 のミップマップ列は 256+64+16+4+1 テクセル。
    renderer.setImage(makeDecodedImage(16, 16));
    renderer.draw({}, QRectF(0, 0, 16, 16), QSize(16, 16), 1.0, false, {.solidColor = Qt::white});
    QCOMPARE(renderer.textureBytes(), (256 + 64 + 16 + 4 + 1) * 4);

    // 解析用バッファを持つ画像は RGBA16F になり、1 テクセルが 2 倍になる。
    renderer.setImage(makeFloatDecodedImage(16, 16));
    renderer.draw({}, QRectF(0, 0, 16, 16), QSize(16, 16), 1.0, false, {.solidColor = Qt::white});
    QCOMPARE(renderer.textureBytes(), (256 + 64 + 16 + 4 + 1) * 8);

    renderer.release();
    QCOMPARE(renderer.textureBytes(), 0);
  }

  void matchesCpuForIdentity() {
    const auto decodedImage = makeDecodedImage(16, 12);
    const DisplayTransformParams params;

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QCOMPARE(gpuImage.size(), cpuImage.size());
    QVERIFY2(maxChannelDifference(gpuImage, cpuImage) <= 2,
             qPrintable(QString("max diff %1").arg(maxChannelDifference(gpuImage, cpuImage))));
  }

  void matchesCpuForGamma() {
    const auto decodedImage = makeDecodedImage(16, 12);
    const DisplayTransformParams params{.gamma = 2.2};

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QVERIFY2(maxChannelDifference(gpuImage, cpuImage) <= 2,
             qPrintable(QString("max diff %1").arg(maxChannelDifference(gpuImage, cpuImage))));
  }

  void matchesCpuForInvert() {
    const auto decodedImage = makeDecodedImage(16, 12);
    const DisplayTransformParams params{.invert = true};

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QVERIFY2(maxChannelDifference(gpuImage, cpuImage) <= 2,
             qPrintable(QString("max diff %1").arg(maxChannelDifference(gpuImage, cpuImage))));
  }

  void matchesCpuForChannelModes() {
    const auto decodedImage = makeDecodedImage(16, 12);
    for (const auto channelMode : {heiran::ui::ChannelMode::Gray, heiran::ui::ChannelMode::Red,
                                   heiran::ui::ChannelMode::Green, heiran::ui::ChannelMode::Blue}) {
      const DisplayTransformParams params{.channelMode = channelMode};

      const QImage gpuImage = renderOnGpu(decodedImage, params);
      QVERIFY(!gpuImage.isNull());
      const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

      const int difference = maxChannelDifference(gpuImage, cpuImage);
      QVERIFY2(difference <= 2, qPrintable(QString("channel %1 max diff %2")
                                               .arg(static_cast<int>(channelMode))
                                               .arg(difference)));
    }
  }

  // 色管理を当てたときも GPU と CPU が一致すること。式を片方だけ直すと、
  // ルーペと差分表示だけ色が違うことになる。
  void matchesCpuForColorManagedTransfer() {
    const auto decodedImage = makeDecodedImage(16, 12);

    // 線形で持っている値を sRGB へ符号化して出す場合。
    DisplayTransformParams params;
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(
        QColorSpace(QColorSpace::SRgbLinear), QColorSpace(QColorSpace::SRgb));

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QCOMPARE(gpuImage.size(), cpuImage.size());
    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    // 変換が実際に効いていること。線形を sRGB へ符号化すると中間は明るくなる。
    const QImage plain =
        heiran::ui::renderDisplayImageOnCpu(decodedImage, DisplayTransformParams{});
    QVERIFY2(maxChannelDifference(cpuImage, plain) > 8, "the transfer function had no effect");
  }

  // 原色の変換が入る場合も一致すること。CPU 側は表で持てず画素ごとに当てるため、
  // 経路が分かれる。
  void matchesCpuForColorManagedPrimaries() {
    const auto decodedImage = makeDecodedImage(16, 12);

    DisplayTransformParams params;
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(
        QColorSpace(QColorSpace::DisplayP3), QColorSpace(QColorSpace::SRgb));
    QVERIFY(!params.colorTransform.primariesMatch);

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    // Display P3 を sRGB へ移すと彩度が上がる方向へ動く。
    const QImage plain =
        heiran::ui::renderDisplayImageOnCpu(decodedImage, DisplayTransformParams{});
    QVERIFY2(maxChannelDifference(cpuImage, plain) > 4, "the primaries matrix had no effect");
  }

  // チャネルを 1 つだけ出しているときは原色を移さないこと。
  // 値を並べ替えた偽色表示であって色ではない。GPU と CPU で判断をそろえる。
  void skipsThePrimariesMatrixForSingleChannelModes() {
    const auto decodedImage = makeDecodedImage(16, 12);

    DisplayTransformParams params;
    params.channelMode = heiran::ui::ChannelMode::Red;
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(
        QColorSpace(QColorSpace::DisplayP3), QColorSpace(QColorSpace::SRgb));

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    // 灰色のままであること。原色を移すと色が付く。
    const QRgb pixel = cpuImage.pixel(8, 6);
    QCOMPARE(qRed(pixel), qGreen(pixel));
    QCOMPARE(qGreen(pixel), qBlue(pixel));
  }

  // 対応表を使う経路でも GPU と CPU が一致すること。
  // GPU は 3D テクスチャの線形補間、CPU は自前の三重線形補間で引くため、
  // 同じ表を同じように引けているかはここでしか確かめられない。
  void matchesCpuForTheColorLookupTable() {
    const auto decodedImage = makeDecodedImage(16, 12);

    const QColorSpace source(QColorSpace::Bt2100Pq);
    const QColorSpace display(QColorSpace::SRgb);
    QVERIFY(!heiran::ui::isShaderRepresentable(source));

    DisplayTransformParams params;
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(source, display);
    params.colorLut = heiran::ui::buildColorLut(source, display);
    QVERIFY(params.colorLut != nullptr);

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QCOMPARE(gpuImage.size(), cpuImage.size());
    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    // 変換が実際に効いていること。
    const QImage plain =
        heiran::ui::renderDisplayImageOnCpu(decodedImage, DisplayTransformParams{});
    QVERIFY2(maxChannelDifference(cpuImage, plain) > 8, "the lookup table had no effect");
  }

  // 表を使う場合も露出は線形光で当てること。表の出力が線形光である前提を守る。
  void appliesExposureAfterTheColorLookupTable() {
    const auto decodedImage = makeDecodedImage(16, 12);

    const QColorSpace source(QColorSpace::Bt2100Pq);
    const QColorSpace display(QColorSpace::SRgb);

    DisplayTransformParams params;
    params.exposureStops = 1.0;
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(source, display);
    params.colorLut = heiran::ui::buildColorLut(source, display);

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);
    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));
  }

  // 露出は線形光で当てること。符号化された値へ掛けると倍率どおりにならない。
  void appliesExposureInLinearLight() {
    const auto decodedImage = makeDecodedImage(16, 12);

    DisplayTransformParams params;
    params.exposureStops = 1.0;  // 光量 2 倍
    params.colorManaged = true;
    params.colorTransform = heiran::ui::makeColorTransformParams(QColorSpace(QColorSpace::SRgb),
                                                                 QColorSpace(QColorSpace::SRgb));

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);
    QVERIFY2(maxChannelDifference(gpuImage, cpuImage) <= 2,
             qPrintable(QString("max diff %1").arg(maxChannelDifference(gpuImage, cpuImage))));

    // 符号化された値の 0.5 は線形では約 0.2140。2 倍して戻すと約 0.6858 になる。
    // 符号化された値をそのまま 2 倍すると 1.0 で頭打ちになるため、区別できる。
    const float managed = heiran::ui::encodeShaderTransfer(
        heiran::ui::decodeShaderTransfer(0.5F, heiran::ui::ShaderTransfer::SRgb, 1.0F) * 2.0F,
        heiran::ui::ShaderTransfer::SRgb, 1.0F);
    QVERIFY2(std::abs(managed - 0.6858F) < 0.005F, qPrintable(QString::number(managed)));
  }

  // 表示用画像の並びが違っても色が入れ替わらないこと。
  //
  // Qt の復号は形式によって並びが違い、JPEG などは Format_RGB32 (BGRA の並び) で
  // 返る。並びをそのまま GL へ渡しているため、指定を間違えると赤と青が入れ替わる。
  // 見た目が破綻するのに、階調は正しいままなので気づきにくい。
  void keepsChannelOrderForEveryImageFormat() {
    const QColor probeColor(200, 120, 40);

    for (const auto format : {QImage::Format_RGBA8888, QImage::Format_RGB32, QImage::Format_ARGB32,
                              QImage::Format_RGB888}) {
      QImage image(16, 12, format);
      image.fill(probeColor);
      const heiran::infra::DecodedImage decodedImage{.displayImage = image};

      const QImage gpuImage = renderOnGpu(decodedImage, DisplayTransformParams{});
      QVERIFY2(!gpuImage.isNull(), qPrintable(QString("format %1").arg(format)));

      const QColor rendered = gpuImage.pixelColor(gpuImage.width() / 2, gpuImage.height() / 2);
      QVERIFY2(std::abs(rendered.red() - probeColor.red()) <= 2 &&
                   std::abs(rendered.green() - probeColor.green()) <= 2 &&
                   std::abs(rendered.blue() - probeColor.blue()) <= 2,
               qPrintable(QString("format %1: got (%2,%3,%4)")
                              .arg(format)
                              .arg(rendered.red())
                              .arg(rendered.green())
                              .arg(rendered.blue())));
    }
  }

  // 16bit 整数の画像でも GPU と CPU が一致すること。
  //
  // この経路は正規化整数のテクスチャとして載せる。fp16 へ詰め直す経路とは
  // 別の形式を通るため、見た目が変わらないことを確かめる。
  void matchesCpuForUInt16Images() {
    for (const int channelCount : {3, 4}) {
      const auto decodedImage = makeUInt16DecodedImage(16, 12, channelCount);
      const DisplayTransformParams params;

      const QImage gpuImage = renderOnGpu(decodedImage, params);
      QVERIFY(!gpuImage.isNull());
      const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

      QCOMPARE(gpuImage.size(), cpuImage.size());
      const int difference = maxChannelDifference(gpuImage, cpuImage);
      QVERIFY2(difference <= 2,
               qPrintable(QString("channels %1 max diff %2").arg(channelCount).arg(difference)));
    }
  }

  // half float の画像でも GPU と CPU が一致すること。
  //
  // 型を合わせて `RGBA16F` へ直接載せる経路のため、3 チャネルを 4 チャネルへ広げる
  // 扱い (alpha は 1.0) と、行の詰め物が正しく届くことを確かめる。
  void matchesCpuForFloat16Images() {
    for (const int channelCount : {3, 4}) {
      const auto decodedImage = makeFloat16DecodedImage(16, 12, channelCount);
      const DisplayTransformParams params;

      const QImage gpuImage = renderOnGpu(decodedImage, params);
      QVERIFY(!gpuImage.isNull());
      const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

      QCOMPARE(gpuImage.size(), cpuImage.size());
      const int difference = maxChannelDifference(gpuImage, cpuImage);
      QVERIFY2(difference <= 2,
               qPrintable(QString("channels %1 max diff %2").arg(channelCount).arg(difference)));
    }
  }

  // half float は 1.0 を超える値を保つこと。正規化整数として載せていると 1.0 で
  // 頭打ちになり、露出を下げても階調が戻らない。
  void keepsHighlightsAboveOneForFloat16Images() {
    const auto decodedImage = makeFloat16DecodedImage(16, 12, 3);
    const DisplayTransformParams params{.exposureStops = -2.0};

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);
    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    const QColor left = gpuImage.pixelColor(gpuImage.width() / 2, gpuImage.height() / 2);
    const QColor right = gpuImage.pixelColor(gpuImage.width() - 1, gpuImage.height() / 2);
    QVERIFY2(right.red() - left.red() > 40,
             qPrintable(QString("left %1 right %2").arg(left.red()).arg(right.red())));
  }

  // 16bit 整数でも露出を上げ下げできること。正規化整数は 1.0 を超えられないが、
  // 元の値が 0.0-1.0 に収まるため表示に必要な階調は残る。
  void appliesExposureToUInt16Images() {
    const auto decodedImage = makeUInt16DecodedImage(16, 12, 3);
    const DisplayTransformParams params{.exposureStops = -1.0};

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);
    QVERIFY2(maxChannelDifference(gpuImage, cpuImage) <= 2,
             qPrintable(QString("max diff %1").arg(maxChannelDifference(gpuImage, cpuImage))));
  }

  // 表示用画像へ丸めてから載せていると、1.0 超が白へ潰れたまま暗くなるだけになる。
  // CPU 実装は解析用バッファへ露出を掛けるため、一致すれば元の精度で載っている。
  void recoversHighlightsWithNegativeExposure() {
    const auto decodedImage = makeFloatDecodedImage(16, 12);
    const DisplayTransformParams params{.exposureStops = -2.0};

    const QImage gpuImage = renderOnGpu(decodedImage, params);
    QVERIFY(!gpuImage.isNull());
    const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

    QCOMPARE(gpuImage.size(), cpuImage.size());
    const int difference = maxChannelDifference(gpuImage, cpuImage);
    QVERIFY2(difference <= 2, qPrintable(QString("max diff %1").arg(difference)));

    // 1.0 超の領域が階調を保っていることを直接確かめる。
    const QColor left = gpuImage.pixelColor(gpuImage.width() / 2, gpuImage.height() / 2);
    const QColor right = gpuImage.pixelColor(gpuImage.width() - 1, gpuImage.height() / 2);
    QVERIFY2(right.red() - left.red() > 40,
             qPrintable(QString("left %1 right %2").arg(left.red()).arg(right.red())));
  }

  // 下地は画像の四角形の中だけに出ること。下地を別の矩形として塗っていたときは、
  // 原点とサイズを別々に丸めた差の分だけ上端へ 1 画素はみ出し、パンでちらついていた。
  void doesNotLeakBackdropOutsideImage() {
    // 全画素を不透明な赤にする。覆われた画素は赤、覆われない画素は背景の青になる。
    // 白が 1 画素でも出れば下地がはみ出している。
    QImage red(8, 6, QImage::Format_RGBA8888);
    red.fill(QColor(255, 0, 0, 255));

    heiran::ui::GpuImageRenderer renderer;
    QVERIFY(renderer.initialize());
    renderer.setImage({.displayImage = red});

    const QSize frameSize(40, 30);
    QOpenGLFramebufferObject frameBuffer(frameSize);
    QVERIFY(frameBuffer.bind());
    auto *functions = QOpenGLContext::currentContext()->functions();

    // パンで端数が変わる状況を再現するため、描画先を 0.1 刻みでずらす。
    for (int step = 0; step < 10; ++step) {
      const double offset = 6.0 + static_cast<double>(step) * 0.1;
      functions->glClearColor(0.0F, 0.0F, 1.0F, 1.0F);
      functions->glClear(GL_COLOR_BUFFER_BIT);
      renderer.draw({}, QRectF(offset, offset, 17.3, 13.7), frameSize, 1.0, false,
                    {.solidColor = Qt::white});

      const QImage rendered = frameBuffer.toImage();
      for (int y = 0; y < frameSize.height(); ++y) {
        for (int x = 0; x < frameSize.width(); ++x) {
          const QColor pixel = rendered.pixelColor(x, y);
          QVERIFY2(pixel.red() < 200 || pixel.green() < 200,
                   qPrintable(
                       QString("backdrop leaked at (%1, %2) offset %3").arg(x).arg(y).arg(offset)));
        }
      }
    }
    frameBuffer.release();
  }

  void matchesCpuForRotationAndFlip() {
    const auto decodedImage = makeDecodedImage(16, 12);
    for (int quarterTurns = 0; quarterTurns < 4; ++quarterTurns) {
      for (const bool flipX : {false, true}) {
        for (const bool flipY : {false, true}) {
          const DisplayTransformParams params{
              .rotationQuarterTurns = quarterTurns, .flipX = flipX, .flipY = flipY};

          const QImage gpuImage = renderOnGpu(decodedImage, params);
          QVERIFY(!gpuImage.isNull());
          const QImage cpuImage = heiran::ui::renderDisplayImageOnCpu(decodedImage, params);

          const int difference = maxChannelDifference(gpuImage, cpuImage);
          QVERIFY2(difference <= 2, qPrintable(QString("rotation %1 flipX %2 flipY %3 max diff %4")
                                                   .arg(quarterTurns)
                                                   .arg(flipX)
                                                   .arg(flipY)
                                                   .arg(difference)));
        }
      }
    }
  }
};

QTEST_MAIN(GpuImageRendererTests)

#include "GpuImageRendererTests.moc"
