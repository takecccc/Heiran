// EXR の復号 (OpenEXR を直接使う経路)。
//
// 入力は、OpenEXR 自身の書き出し API で、テストの中で作る。期待する画素を、手で計算できる形で
// 持たせて、圧縮、チャンネルの構成、データウィンドウ、タイル、パートの組み合わせを確かめる。

#include "heiran/infra/ExrDecoder.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfFloatAttribute.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfIntAttribute.h>
#include <ImfMultiPartOutputFile.h>
#include <ImfOutputFile.h>
#include <ImfOutputPart.h>
#include <ImfPartType.h>
#include <ImfStandardAttributes.h>
#include <ImfStringAttribute.h>
#include <ImfTileDescription.h>
#include <ImfTiledOutputFile.h>
#include <ImfVecAttribute.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <half.h>
#include <string>
#include <vector>

using namespace heiran::infra;

namespace {

constexpr int kWidth = 19;
constexpr int kHeight = 13;

// 画素 (x, y) のチャンネル c の値。1.0 を超える値と負の値、0 を含む。half で正確に表せる値だけ。
float expectedValue(int x, int y, int channel) {
  const int code = (x * 3 + y * 5 + channel * 7) % 16;
  switch (code) {
    case 0:
      return 0.0F;
    case 1:
      return 1.0F;
    case 2:
      return -0.5F;
    case 3:
      return 40.0F;
    case 4:
      return 0.25F;
    case 5:
      return 1024.0F;
    default:
      return static_cast<float>(code) / 8.0F;
  }
}

struct ChannelSpec {
  std::string name;
  Imf::PixelType type;
  int channelIndex;  // expectedValue に渡す番号
};

std::string pathFor(const QTemporaryDir &directory, const char *name) {
  return QDir(directory.path()).filePath(name).toStdString();
}

// 1 つのチャンネルの値を、型に合わせて並べる。
std::vector<char> channelBuffer(const ChannelSpec &spec, int width, int height) {
  const bool isHalf = spec.type == Imf::HALF;
  std::vector<char> bytes(static_cast<size_t>(width) * height * (isHalf ? 2 : 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = expectedValue(x, y, spec.channelIndex);
      const size_t index = static_cast<size_t>(y) * width + x;
      if (isHalf) {
        reinterpret_cast<half *>(bytes.data())[index] = half(value);
      } else {
        reinterpret_cast<float *>(bytes.data())[index] = value;
      }
    }
  }
  return bytes;
}

Imf::FrameBuffer frameBufferFor(const std::vector<ChannelSpec> &specs,
                                std::vector<std::vector<char>> *buffers, int width,
                                const Imath::V2i &origin) {
  Imf::FrameBuffer frameBuffer;
  for (size_t index = 0; index < specs.size(); ++index) {
    const size_t sampleBytes = specs[index].type == Imf::HALF ? 2 : 4;
    char *base = (*buffers)[index].data() - static_cast<ptrdiff_t>(origin.x) * sampleBytes -
                 static_cast<ptrdiff_t>(origin.y) * sampleBytes * width;
    frameBuffer.insert(specs[index].name.c_str(),
                       Imf::Slice(specs[index].type, base, sampleBytes, sampleBytes * width));
  }
  return frameBuffer;
}

Imf::Header makeHeader(int width, int height, Imf::Compression compression,
                       const std::vector<ChannelSpec> &specs,
                       const Imath::V2i &origin = Imath::V2i(0, 0)) {
  Imf::Header header(
      Imath::Box2i(Imath::V2i(0, 0), Imath::V2i(width + origin.x - 1, height + origin.y - 1)),
      Imath::Box2i(origin, Imath::V2i(origin.x + width - 1, origin.y + height - 1)));
  header.compression() = compression;
  for (const auto &spec : specs) {
    header.channels().insert(spec.name.c_str(), Imf::Channel(spec.type));
  }
  return header;
}

// スキャンラインの EXR を書く。
void writeScanline(const std::string &path, Imf::Compression compression,
                   const std::vector<ChannelSpec> &specs,
                   const Imath::V2i &origin = Imath::V2i(0, 0),
                   const Imf::Header *customHeader = nullptr) {
  Imf::Header header = customHeader != nullptr
                           ? *customHeader
                           : makeHeader(kWidth, kHeight, compression, specs, origin);
  std::vector<std::vector<char>> buffers;
  for (const auto &spec : specs) {
    buffers.push_back(channelBuffer(spec, kWidth, kHeight));
  }
  Imf::OutputFile file(path.c_str(), header);
  file.setFrameBuffer(frameBufferFor(specs, &buffers, kWidth, origin));
  file.writePixels(kHeight);
}

std::vector<ChannelSpec> rgb(Imf::PixelType type) {
  return {{"R", type, 0}, {"G", type, 1}, {"B", type, 2}};
}

std::vector<ChannelSpec> rgba(Imf::PixelType type) {
  return {{"R", type, 0}, {"G", type, 1}, {"B", type, 2}, {"A", type, 3}};
}

// 結果の画素が、期待する値と一致するか (出力のチャンネルの並びに対する番号)。
bool matches(const RawImage &raw, int originX, int originY,
             const std::vector<int> &channelIndexes) {
  if (raw.channels != static_cast<int>(channelIndexes.size())) {
    return false;
  }
  for (int y = 0; y < raw.height; ++y) {
    for (int x = 0; x < raw.width; ++x) {
      for (int c = 0; c < raw.channels; ++c) {
        const float expected = expectedValue(x, y, channelIndexes[c]);
        float actual = 0.0F;
        const size_t index = (static_cast<size_t>(y) * raw.width + x) * raw.channels + c;
        if (raw.sampleType == PixelSampleType::Float16) {
          actual =
              static_cast<float>(reinterpret_cast<const half *>(raw.pixels.constData())[index]);
        } else {
          actual = reinterpret_cast<const float *>(raw.pixels.constData())[index];
        }
        if (actual != expected) {
          return false;
        }
      }
    }
  }
  Q_UNUSED(originX);
  Q_UNUSED(originY);
  return true;
}

}  // namespace

class ExrDecodeTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 可逆な圧縮のすべてで、画素が一致すること。half は half のまま持つ。
  void decodesHalfRgbWithEveryLosslessCompression() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Imf::Compression compressions[] = {Imf::NO_COMPRESSION,   Imf::RLE_COMPRESSION,
                                             Imf::ZIPS_COMPRESSION, Imf::ZIP_COMPRESSION,
                                             Imf::PIZ_COMPRESSION,  Imf::PXR24_COMPRESSION};
    for (const auto compression : compressions) {
      const std::string path = pathFor(directory, "half.exr");
      writeScanline(path, compression, rgb(Imf::HALF));

      const auto result = decodeExr(QString::fromStdString(path), 0);
      QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
      QCOMPARE(result.raw.width, kWidth);
      QCOMPARE(result.raw.height, kHeight);
      QCOMPARE(result.raw.sampleType, PixelSampleType::Float16);
      QVERIFY2(matches(result.raw, 0, 0, {0, 1, 2}),
               qPrintable(QString("compression %1").arg(static_cast<int>(compression))));
    }
  }

  // float の RGBA。1.0 を超える値と負の値を、丸めずに保つ。
  void decodesFloatRgbaWithoutLosingRange() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "float.exr");
    writeScanline(path, Imf::ZIP_COMPRESSION, rgba(Imf::FLOAT));

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.sampleType, PixelSampleType::Float32);
    QVERIFY(matches(result.raw, 0, 0, {0, 1, 2, 3}));
  }

  // 灰色 (Y)、灰色 + アルファ。
  void decodesGrayAndGrayWithAlpha() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string grayPath = pathFor(directory, "gray.exr");
    writeScanline(grayPath, Imf::ZIP_COMPRESSION, {{"Y", Imf::HALF, 0}});
    const auto gray = decodeExr(QString::fromStdString(grayPath), 0);
    QVERIFY2(gray.raw.ok, qPrintable(gray.raw.errorMessage));
    QVERIFY(matches(gray.raw, 0, 0, {0}));

    const std::string alphaPath = pathFor(directory, "gray_alpha.exr");
    writeScanline(alphaPath, Imf::ZIP_COMPRESSION, {{"Y", Imf::HALF, 0}, {"A", Imf::HALF, 3}});
    const auto grayAlpha = decodeExr(QString::fromStdString(alphaPath), 0);
    QVERIFY2(grayAlpha.raw.ok, qPrintable(grayAlpha.raw.errorMessage));
    QVERIFY(matches(grayAlpha.raw, 0, 0, {0, 3}));
  }

  // 層 (層名.R など) と、余分なチャンネル。表示は、色のチャンネルだけ。
  void picksTheColorChannels() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const std::string layerPath = pathFor(directory, "layer.exr");
    writeScanline(
        layerPath, Imf::ZIP_COMPRESSION,
        {{"diffuse.B", Imf::HALF, 2}, {"diffuse.G", Imf::HALF, 1}, {"diffuse.R", Imf::HALF, 0}});
    const auto layer = decodeExr(QString::fromStdString(layerPath), 0);
    QVERIFY2(layer.raw.ok, qPrintable(layer.raw.errorMessage));
    QVERIFY(matches(layer.raw, 0, 0, {0, 1, 2}));

    // RGBA に、深度 (Z) などが加わる。
    const std::string extraPath = pathFor(directory, "extra.exr");
    auto specs = rgba(Imf::HALF);
    specs.push_back({"Z", Imf::FLOAT, 9});
    writeScanline(extraPath, Imf::ZIP_COMPRESSION, specs);
    const auto extra = decodeExr(QString::fromStdString(extraPath), 0);
    QVERIFY2(extra.raw.ok, qPrintable(extra.raw.errorMessage));
    QVERIFY(matches(extra.raw, 0, 0, {0, 1, 2, 3}));
  }

  // RGB が half、A が float なら、全部 float で持つ。
  void promotesMixedTypesToFloat() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "mixed.exr");
    writeScanline(
        path, Imf::ZIP_COMPRESSION,
        {{"R", Imf::HALF, 0}, {"G", Imf::HALF, 1}, {"B", Imf::HALF, 2}, {"A", Imf::FLOAT, 3}});

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.sampleType, PixelSampleType::Float32);
    QVERIFY(matches(result.raw, 0, 0, {0, 1, 2, 3}));
  }

  // データウィンドウが原点から離れていても、データの左上が (0, 0) になる。
  void honorsADataWindowThatDoesNotStartAtTheOrigin() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "offset.exr");
    writeScanline(path, Imf::ZIP_COMPRESSION, rgb(Imf::HALF), Imath::V2i(10, 20));

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.width, kWidth);
    QCOMPARE(result.raw.height, kHeight);
    QVERIFY(matches(result.raw, 0, 0, {0, 1, 2}));
  }

  // タイル。画像の端で、はみ出すタイルがある大きさ (19x13 に 8x8 のタイル)。
  void decodesATiledImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "tiled.exr");
    const auto specs = rgb(Imf::HALF);
    Imf::Header header = makeHeader(kWidth, kHeight, Imf::ZIP_COMPRESSION, specs);
    header.setType(Imf::TILEDIMAGE);
    header.setTileDescription(Imf::TileDescription(8, 8, Imf::ONE_LEVEL));
    std::vector<std::vector<char>> buffers;
    for (const auto &spec : specs) {
      buffers.push_back(channelBuffer(spec, kWidth, kHeight));
    }
    {
      Imf::TiledOutputFile file(path.c_str(), header);
      file.setFrameBuffer(frameBufferFor(specs, &buffers, kWidth, Imath::V2i(0, 0)));
      file.writeTiles(0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
    }

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, 0, 0, {0, 1, 2}));
  }

  // 複数のパート。パートごとに違う画像を読み、数を返す。範囲外のパートは失敗する。
  void decodesEachPartOfAMultipartFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "multi.exr");

    const auto firstSpecs = rgb(Imf::HALF);
    const std::vector<ChannelSpec> secondSpecs = {
        {"R", Imf::FLOAT, 4}, {"G", Imf::FLOAT, 5}, {"B", Imf::FLOAT, 6}, {"A", Imf::FLOAT, 7}};
    std::vector<Imf::Header> headers;
    headers.push_back(makeHeader(kWidth, kHeight, Imf::ZIP_COMPRESSION, firstSpecs));
    headers.push_back(makeHeader(kWidth, kHeight, Imf::PIZ_COMPRESSION, secondSpecs));
    headers[0].setName("first");
    headers[0].setType(Imf::SCANLINEIMAGE);
    headers[1].setName("second");
    headers[1].setType(Imf::SCANLINEIMAGE);
    {
      Imf::MultiPartOutputFile file(path.c_str(), headers.data(), static_cast<int>(headers.size()));
      std::vector<std::vector<char>> firstBuffers;
      for (const auto &spec : firstSpecs) {
        firstBuffers.push_back(channelBuffer(spec, kWidth, kHeight));
      }
      Imf::OutputPart first(file, 0);
      first.setFrameBuffer(frameBufferFor(firstSpecs, &firstBuffers, kWidth, Imath::V2i(0, 0)));
      first.writePixels(kHeight);

      std::vector<std::vector<char>> secondBuffers;
      for (const auto &spec : secondSpecs) {
        secondBuffers.push_back(channelBuffer(spec, kWidth, kHeight));
      }
      Imf::OutputPart second(file, 1);
      second.setFrameBuffer(frameBufferFor(secondSpecs, &secondBuffers, kWidth, Imath::V2i(0, 0)));
      second.writePixels(kHeight);
    }

    const auto first = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(first.raw.ok, qPrintable(first.raw.errorMessage));
    QCOMPARE(first.partCount, 2);
    QCOMPARE(first.raw.sampleType, PixelSampleType::Float16);
    QVERIFY(matches(first.raw, 0, 0, {0, 1, 2}));

    const auto second = decodeExr(QString::fromStdString(path), 1);
    QVERIFY2(second.raw.ok, qPrintable(second.raw.errorMessage));
    QCOMPARE(second.raw.sampleType, PixelSampleType::Float32);
    QVERIFY(matches(second.raw, 0, 0, {4, 5, 6, 7}));

    QVERIFY(!decodeExr(QString::fromStdString(path), 2).raw.ok);
    QVERIFY(!decodeExr(QString::fromStdString(path), -1).raw.ok);

    // 復号の経路では、パートがページになる。
    const auto decoded = ImageDecoder().decode(QString::fromStdString(path), false, 1);
    QVERIFY2(decoded.ok, qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.decodedImage.subImageKind, SubImageKind::Pages);
    QCOMPARE(decoded.decodedImage.subImageCount, 2);
    QCOMPARE(decoded.decodedImage.subImageIndex, 1);
  }

  // 原色の申告 (chromaticities) を、色空間として取り出す。
  void readsTheDeclaredChromaticities() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "chroma.exr");
    Imf::Header header = makeHeader(kWidth, kHeight, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    // Rec.2020。
    Imf::addChromaticities(
        header, Imf::Chromaticities(Imath::V2f(0.708F, 0.292F), Imath::V2f(0.170F, 0.797F),
                                    Imath::V2f(0.131F, 0.046F), Imath::V2f(0.3127F, 0.3290F)));
    writeScanline(path, Imf::ZIP_COMPRESSION, rgb(Imf::HALF), Imath::V2i(0, 0), &header);

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(result.declaredColorSpace.isValid());
    QCOMPARE(result.declaredColorSpace.transferFunction(), QColorSpace::TransferFunction::Linear);

    // 申告が無ければ、無効 (呼び側が、線形 sRGB の既定へ退避する)。
    const std::string plainPath = pathFor(directory, "plain.exr");
    writeScanline(plainPath, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    QVERIFY(!decodeExr(QString::fromStdString(plainPath), 0).declaredColorSpace.isValid());
  }

  // 復号の経路を通ると、申告された原色が、色空間として使われる。申告が無ければ、線形 (EXR
  // は線形光を入れる 約束)。
  void resolvesTheColorSpaceThroughTheDecoder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Display P3 の色度。
    const std::string path = pathFor(directory, "p3.exr");
    Imf::Header header = makeHeader(kWidth, kHeight, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    Imf::addChromaticities(
        header, Imf::Chromaticities(Imath::V2f(0.680F, 0.320F), Imath::V2f(0.265F, 0.690F),
                                    Imath::V2f(0.150F, 0.060F), Imath::V2f(0.3127F, 0.3290F)));
    writeScanline(path, Imf::ZIP_COMPRESSION, rgb(Imf::HALF), Imath::V2i(0, 0), &header);

    const auto declared = ImageDecoder().decode(QString::fromStdString(path), true);
    QVERIFY2(declared.ok, qPrintable(declared.errorMessage));
    const QColorSpace expected(QColorSpace::PrimaryPoints{.whitePoint = {0.3127, 0.3290},
                                                          .redPoint = {0.680, 0.320},
                                                          .greenPoint = {0.265, 0.690},
                                                          .bluePoint = {0.150, 0.060}},
                               QColorSpace::TransferFunction::Linear);
    QVERIFY(declared.decodedImage.colorSpace == expected);

    const std::string plainPath = pathFor(directory, "plain.exr");
    writeScanline(plainPath, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    const auto plain = ImageDecoder().decode(QString::fromStdString(plainPath), true);
    QVERIFY2(plain.ok, qPrintable(plain.errorMessage));
    QCOMPARE(plain.decodedImage.colorSpace.transferFunction(),
             QColorSpace::TransferFunction::Linear);
  }

  // 日本語のパスでも読める。
  void decodesFromANonAsciiPath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string source = pathFor(directory, "plain.exr");
    writeScanline(source, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    const QString copy = QDir(directory.path()).filePath(QString::fromUtf8("画像 テスト.exr"));
    QVERIFY(QFile::copy(QString::fromStdString(source), copy));

    const auto result = decodeExr(copy, 0);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, 0, 0, {0, 1, 2}));
  }

  // 壊れたファイルは、落ちずに失敗を返す。
  void failsOnBrokenFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string source = pathFor(directory, "good.exr");
    writeScanline(source, Imf::ZIP_COMPRESSION, rgb(Imf::HALF));
    QFile good(QString::fromStdString(source));
    QVERIFY(good.open(QIODevice::ReadOnly));
    const QByteArray bytes = good.readAll();

    const auto write = [&directory](const char *name, const QByteArray &contents) {
      const QString path = QDir(directory.path()).filePath(name);
      QFile file(path);
      file.open(QIODevice::WriteOnly);
      file.write(contents);
      return path;
    };

    QVERIFY(!decodeExr(write("empty.exr", QByteArray()), 0).raw.ok);
    QVERIFY(!decodeExr(write("garbage.exr", QByteArray(500, 'x')), 0).raw.ok);
    // 途中で切れている (ヘッダーは読めるが、画素が足りない)。
    QVERIFY(!decodeExr(write("cut.exr", bytes.left(bytes.size() / 2)), 0).raw.ok);
    QVERIFY(!decodeExr(QDir(directory.path()).filePath("missing.exr"), 0).raw.ok);
  }

  // 色差を間引いた形式 (Y / RY / BY) は対応しない。失敗を返して、呼び側が退避する。
  void rejectsChromaSubsampledImages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "subsampled.exr");
    // 間引いたチャンネルは、大きさが標本化の倍数でなければ書けない。
    constexpr int kSubWidth = 20;
    constexpr int kSubHeight = 14;
    Imf::Header header(kSubWidth, kSubHeight);
    header.channels().insert("Y", Imf::Channel(Imf::HALF));
    header.channels().insert("RY", Imf::Channel(Imf::HALF, 2, 2));
    header.channels().insert("BY", Imf::Channel(Imf::HALF, 2, 2));
    std::vector<half> samples(static_cast<size_t>(kSubWidth) * kSubHeight, half(0.5F));
    {
      Imf::OutputFile file(path.c_str(), header);
      Imf::FrameBuffer frameBuffer;
      char *base = reinterpret_cast<char *>(samples.data());
      frameBuffer.insert("Y", Imf::Slice(Imf::HALF, base, 2, 2 * kSubWidth));
      frameBuffer.insert("RY", Imf::Slice(Imf::HALF, base, 2, 2 * kSubWidth, 2, 2));
      frameBuffer.insert("BY", Imf::Slice(Imf::HALF, base, 2, 2 * kSubWidth, 2, 2));
      file.setFrameBuffer(frameBuffer);
      file.writePixels(kSubHeight);
    }

    const auto result = decodeExr(QString::fromStdString(path), 0);
    QVERIFY(!result.raw.ok);
    QVERIFY(!result.raw.errorMessage.isEmpty());

    // 復号の経路でも失敗する (対応しない形式)。落ちずに、理由を返す。
    const auto fallback = ImageDecoder().decode(QString::fromStdString(path), true);
    QVERIFY(!fallback.ok);
    QVERIFY(!fallback.errorMessage.isEmpty());
  }

  // ---- 属性 (メタデータ) ----

  void readsTheHeaderAttributes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "attributes.exr");
    Imf::Header header = makeHeader(kWidth, kHeight, Imf::PIZ_COMPRESSION, rgba(Imf::HALF));
    header.insert("owner", Imf::StringAttribute("Heiran Test"));
    header.insert("frameNumber", Imf::IntAttribute(42));
    header.insert("exposure", Imf::FloatAttribute(2.5F));
    header.insert("origin", Imf::V2fAttribute(Imath::V2f(1.5F, -2.0F)));
    writeScanline(path, Imf::PIZ_COMPRESSION, rgba(Imf::HALF), Imath::V2i(0, 0), &header);

    const auto entries = readExrMetadata(QString::fromStdString(path));
    const auto valueOf = [&entries](const QString &key) {
      for (const auto &entry : entries) {
        if (entry.group == "EXR" && entry.key == key) {
          return entry.value;
        }
      }
      return QString("<none>");
    };
    QCOMPARE(valueOf("owner"), QString("Heiran Test"));
    QCOMPARE(valueOf("frameNumber"), QString("42"));
    QCOMPARE(valueOf("exposure"), QString("2.5"));
    QCOMPARE(valueOf("origin"), QString("1.5, -2"));
    QCOMPARE(valueOf("compression"), QString("piz"));
    QCOMPARE(valueOf("dataWindow"), QString("(0, 0) - (18, 12)"));
    QCOMPARE(valueOf("Channels"), QString("A: half, B: half, G: half, R: half"));
    // チャンネルは一覧にまとめ、属性としては出さない。
    QCOMPARE(valueOf("channels"), QString("<none>"));
    QCOMPARE(valueOf("Parts"), QString("<none>"));  // 1 パートなら出さない。
  }

  void readsNothingFromAFileThatIsNotExr() {
    QVERIFY(readExrMetadata("C:/definitely/missing.exr").isEmpty());
    QVERIFY(!looksLikeExr(QByteArray("abcd")));
    QVERIFY(!looksLikeExr(QByteArray()));
    QVERIFY(looksLikeExr(QByteArray("v/1", 4)));
  }

  // 復号の経路を通ると、解析用の値 (正規化せず、1.0 を超える値も) として読める。
  void exposesTheValuesThroughTheDecoder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::string path = pathFor(directory, "decoder.exr");
    writeScanline(path, Imf::ZIP_COMPRESSION, rgba(Imf::HALF));

    const auto result = ImageDecoder().decode(QString::fromStdString(path), true);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.decoderBackend, QString("exr"));
    QCOMPARE(result.decodedImage.sourceSampleType, PixelSampleType::Float16);
    QCOMPARE(result.decodedImage.sourceColorModel, PixelColorModel::RGBA);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        for (int c = 0; c < 4; ++c) {
          QCOMPARE(result.decodedImage.sampleRgba(x, y, c), expectedValue(x, y, c));
        }
      }
    }
    // ページは持たない (1 パート)。
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
  }
};

QTEST_MAIN(ExrDecodeTests)

#include "ExrDecodeTests.moc"
