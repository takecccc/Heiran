// TIFF の復号 (libtiff を直接使う経路)。
//
// 入力は、libtiff 自身の書き出し API
// で、テストの中で作る。期待する画素を手で計算できる形で持たせて、
// 型、圧縮、詰め方、タイル、向き、ページの組み合わせを確かめる。

#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/infra/TiffDecoder.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <tiffio.h>
#include <vector>

using namespace heiran::infra;

namespace {

constexpr int kWidth = 21;
constexpr int kHeight = 14;

// 画素 (x, y) のチャンネル c の値 (0-255 の整数)。
int level(int x, int y, int channel) {
  return (x * 11 + y * 23 + channel * 37) % 256;
}

struct WriteOptions {
  int width = kWidth;
  int height = kHeight;
  int bits = 8;
  int samples = 3;
  int photometric = PHOTOMETRIC_RGB;
  int sampleFormat = SAMPLEFORMAT_UINT;
  int planar = PLANARCONFIG_CONTIG;
  int compression = COMPRESSION_NONE;
  int predictor = 0;
  int orientation = 0;   // 0 なら書かない
  int extraSample = -1;  // -1 なら書かない。EXTRASAMPLE_ASSOCALPHA など
  int tileWidth = 0;     // 0 ならストリップ
  int tileHeight = 0;
  int rowsPerStrip = 0;
  QByteArray icc;
};

// 標本 (x, y, c) のバイト列を作る。型に応じて、level を写す。
void sampleBytes(const WriteOptions &options, int x, int y, int channel, uchar *out) {
  const int value = level(x, y, channel);
  if (options.sampleFormat == SAMPLEFORMAT_IEEEFP && options.bits == 32) {
    const float number = static_cast<float>(value) / 8.0F - 4.0F;  // 負の値と 1.0 超を含む
    std::memcpy(out, &number, sizeof(number));
  } else if (options.bits == 16) {
    const quint16 number = static_cast<quint16>(value * 257);
    std::memcpy(out, &number, sizeof(number));
  } else {
    out[0] = static_cast<uchar>(value);
  }
}

// 1 ページを書く。`directory` が 0 なら最初のページ。
bool writePage(TIFF *tiff, const WriteOptions &options, bool lastPage) {
  Q_UNUSED(lastPage);
  TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, options.width);
  TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, options.height);
  TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, options.bits);
  TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, options.samples);
  TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, options.photometric);
  TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, options.sampleFormat);
  TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, options.planar);
  TIFFSetField(tiff, TIFFTAG_COMPRESSION, options.compression);
  if (options.predictor != 0) {
    TIFFSetField(tiff, TIFFTAG_PREDICTOR, options.predictor);
  }
  if (options.orientation != 0) {
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, options.orientation);
  }
  if (options.extraSample >= 0) {
    const uint16_t extras[1] = {static_cast<uint16_t>(options.extraSample)};
    TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, extras);
  }
  if (!options.icc.isEmpty()) {
    TIFFSetField(tiff, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(options.icc.size()),
                 options.icc.constData());
  }

  const size_t bytesPerSample = static_cast<size_t>(options.bits / 8);
  const bool separate = options.planar == PLANARCONFIG_SEPARATE && options.samples > 1;

  if (options.tileWidth > 0) {
    TIFFSetField(tiff, TIFFTAG_TILEWIDTH, options.tileWidth);
    TIFFSetField(tiff, TIFFTAG_TILELENGTH, options.tileHeight);
    std::vector<uchar> tile(static_cast<size_t>(TIFFTileSize(tiff)));
    const int planes = separate ? options.samples : 1;
    for (int plane = 0; plane < planes; ++plane) {
      for (int tileY = 0; tileY < options.height; tileY += options.tileHeight) {
        for (int tileX = 0; tileX < options.width; tileX += options.tileWidth) {
          std::fill(tile.begin(), tile.end(), 0);
          for (int row = 0; row < options.tileHeight; ++row) {
            for (int column = 0; column < options.tileWidth; ++column) {
              const int x = tileX + column;
              const int y = tileY + row;
              if (x >= options.width || y >= options.height) {
                continue;
              }
              if (separate) {
                sampleBytes(options, x, y, plane,
                            tile.data() + (static_cast<size_t>(row) * options.tileWidth + column) *
                                              bytesPerSample);
              } else {
                for (int c = 0; c < options.samples; ++c) {
                  sampleBytes(options, x, y, c,
                              tile.data() +
                                  ((static_cast<size_t>(row) * options.tileWidth + column) *
                                       options.samples +
                                   c) *
                                      bytesPerSample);
                }
              }
            }
          }
          if (TIFFWriteTile(tiff, tile.data(), tileX, tileY, 0, static_cast<uint16_t>(plane)) < 0) {
            return false;
          }
        }
      }
    }
    return TIFFWriteDirectory(tiff) != 0;
  }

  const int rowsPerStrip = options.rowsPerStrip > 0 ? options.rowsPerStrip : options.height;
  TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, rowsPerStrip);
  const int planes = separate ? options.samples : 1;
  std::vector<uchar> line(static_cast<size_t>(options.width) * options.samples * bytesPerSample);
  for (int plane = 0; plane < planes; ++plane) {
    for (int y = 0; y < options.height; ++y) {
      for (int x = 0; x < options.width; ++x) {
        if (separate) {
          sampleBytes(options, x, y, plane, line.data() + static_cast<size_t>(x) * bytesPerSample);
        } else {
          for (int c = 0; c < options.samples; ++c) {
            sampleBytes(options, x, y, c,
                        line.data() +
                            (static_cast<size_t>(x) * options.samples + c) * bytesPerSample);
          }
        }
      }
      if (TIFFWriteScanline(tiff, line.data(), static_cast<uint32_t>(y),
                            static_cast<uint16_t>(plane)) < 0) {
        return false;
      }
    }
  }
  return TIFFWriteDirectory(tiff) != 0;
}

QString writeTiff(const QTemporaryDir &directory, const char *name,
                  const std::vector<WriteOptions> &pages) {
  const QString path = QDir(directory.path()).filePath(name);
  TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "w");
  if (tiff == nullptr) {
    return {};
  }
  for (size_t index = 0; index < pages.size(); ++index) {
    if (!writePage(tiff, pages[index], index + 1 == pages.size())) {
      TIFFClose(tiff);
      return {};
    }
  }
  TIFFClose(tiff);
  return path;
}

QString writeTiff(const QTemporaryDir &directory, const char *name, const WriteOptions &options) {
  return writeTiff(directory, name, std::vector<WriteOptions>{options});
}

// 結果が、期待する値と一致するか。`expectedChannels` は、出力のチャンネルに対する level の番号。
// 値は、型に応じた尺度の整数 (8bit: level、16bit: level * 257) か、浮動小数 (level / 8 - 4)。
bool matches(const RawImage &raw, const WriteOptions &options,
             const std::vector<int> &expectedChannels, bool inverted = false) {
  if (!raw.ok || raw.channels != static_cast<int>(expectedChannels.size()) ||
      raw.width != options.width || raw.height != options.height) {
    return false;
  }
  for (int y = 0; y < raw.height; ++y) {
    for (int x = 0; x < raw.width; ++x) {
      for (int c = 0; c < raw.channels; ++c) {
        const int expectedLevel = level(x, y, expectedChannels[c]);
        const size_t index = (static_cast<size_t>(y) * raw.width + x) * raw.channels + c;
        if (raw.sampleType == PixelSampleType::UInt8) {
          const int actual = static_cast<uchar>(raw.pixels[static_cast<qsizetype>(index)]);
          if (actual != (inverted ? 255 - expectedLevel : expectedLevel)) {
            return false;
          }
        } else if (raw.sampleType == PixelSampleType::UInt16) {
          quint16 actual = 0;
          std::memcpy(&actual, raw.pixels.constData() + index * 2, 2);
          const int expected = inverted ? 65535 - expectedLevel * 257 : expectedLevel * 257;
          if (actual != expected) {
            return false;
          }
        } else if (raw.sampleType == PixelSampleType::Float32) {
          float actual = 0.0F;
          std::memcpy(&actual, raw.pixels.constData() + index * 4, 4);
          if (actual != static_cast<float>(expectedLevel) / 8.0F - 4.0F) {
            return false;
          }
        } else {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

class TiffDecodeTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ---- 型とチャンネル ----

  void decodesEachTypeAndChannelLayout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 8bit のグレー、グレー + アルファ、RGB、RGBA。アルファは、掛けない値のまま。
    for (const int samples : {1, 2, 3, 4}) {
      WriteOptions options;
      options.samples = samples;
      options.photometric = samples <= 2 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
      options.extraSample = (samples == 2 || samples == 4) ? EXTRASAMPLE_UNASSALPHA : -1;
      const QString path = writeTiff(directory, "u8.tif", options);
      QVERIFY(!path.isEmpty());
      const auto result = decodeTiff(path, 0, true);
      QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
      QCOMPARE(result.raw.sampleType, PixelSampleType::UInt8);
      std::vector<int> expected(static_cast<size_t>(samples));
      for (int c = 0; c < samples; ++c) {
        expected[static_cast<size_t>(c)] = c;
      }
      QVERIFY2(matches(result.raw, options, expected),
               qPrintable(QString("samples %1").arg(samples)));
    }

    // 16bit の RGB。
    WriteOptions u16;
    u16.bits = 16;
    const auto u16Result = decodeTiff(writeTiff(directory, "u16.tif", u16), 0, true);
    QVERIFY2(u16Result.raw.ok, qPrintable(u16Result.raw.errorMessage));
    QCOMPARE(u16Result.raw.sampleType, PixelSampleType::UInt16);
    QVERIFY(matches(u16Result.raw, u16, {0, 1, 2}));

    // 32bit の浮動小数 (負の値と 1.0 超を含む)。
    WriteOptions f32;
    f32.bits = 32;
    f32.sampleFormat = SAMPLEFORMAT_IEEEFP;
    const auto f32Result = decodeTiff(writeTiff(directory, "f32.tif", f32), 0, true);
    QVERIFY2(f32Result.raw.ok, qPrintable(f32Result.raw.errorMessage));
    QCOMPARE(f32Result.raw.sampleType, PixelSampleType::Float32);
    QVERIFY(matches(f32Result.raw, f32, {0, 1, 2}));
  }

  // 圧縮と予測子。どれも、同じ画素になる。
  void decodesEveryCompression() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    struct Case {
      int compression;
      int predictor;
      int bits;
    };
    const Case cases[] = {{COMPRESSION_NONE, 0, 8},          {COMPRESSION_LZW, 0, 8},
                          {COMPRESSION_LZW, 2, 8},           {COMPRESSION_LZW, 2, 16},
                          {COMPRESSION_ADOBE_DEFLATE, 0, 8}, {COMPRESSION_ADOBE_DEFLATE, 2, 16},
                          {COMPRESSION_PACKBITS, 0, 8}};
    for (const auto &entry : cases) {
      WriteOptions options;
      options.compression = entry.compression;
      options.predictor = entry.predictor;
      options.bits = entry.bits;
      options.rowsPerStrip = 3;
      const auto result = decodeTiff(writeTiff(directory, "c.tif", options), 0, true);
      QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
      QVERIFY2(matches(result.raw, options, {0, 1, 2}),
               qPrintable(QString("compression %1 predictor %2 bits %3")
                              .arg(entry.compression)
                              .arg(entry.predictor)
                              .arg(entry.bits)));
    }

    // 浮動小数の予測子。
    WriteOptions floating;
    floating.bits = 32;
    floating.sampleFormat = SAMPLEFORMAT_IEEEFP;
    floating.compression = COMPRESSION_ADOBE_DEFLATE;
    floating.predictor = 3;
    const auto result = decodeTiff(writeTiff(directory, "f.tif", floating), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, floating, {0, 1, 2}));
  }

  // ---- 並べ方 ----

  void decodesPlanarAndTiledLayouts() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    WriteOptions planar;
    planar.planar = PLANARCONFIG_SEPARATE;
    planar.samples = 4;
    planar.extraSample = EXTRASAMPLE_UNASSALPHA;
    auto result = decodeTiff(writeTiff(directory, "planar.tif", planar), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, planar, {0, 1, 2, 3}));

    // 端ではみ出すタイル (21x14 に 16x16)。
    WriteOptions tiled;
    tiled.tileWidth = 16;
    tiled.tileHeight = 16;
    tiled.compression = COMPRESSION_ADOBE_DEFLATE;
    result = decodeTiff(writeTiff(directory, "tiled.tif", tiled), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, tiled, {0, 1, 2}));

    // タイルで、平面が分かれる。
    WriteOptions both = tiled;
    both.planar = PLANARCONFIG_SEPARATE;
    both.bits = 16;
    result = decodeTiff(writeTiff(directory, "both.tif", both), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, both, {0, 1, 2}));
  }

  // 4 MB 以上の画像は、帯に分けて、帯ごとにハンドルを開いて並列に読む。
  // 小さい画像と同じ結果になること (ストリップ、タイル、平面の分かれたものすべて)。
  void decodesLargeImagesInParallel() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    WriteOptions strips;
    strips.width = 1500;
    strips.height = 700;
    strips.bits = 16;
    strips.compression = COMPRESSION_ADOBE_DEFLATE;
    strips.rowsPerStrip = 13;  // 端で割り切れない。
    auto result = decodeTiff(writeTiff(directory, "strips.tif", strips), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, strips, {0, 1, 2}));

    WriteOptions planar = strips;
    planar.planar = PLANARCONFIG_SEPARATE;
    planar.bits = 8;
    planar.width = 1700;
    planar.height = 900;
    planar.rowsPerStrip = 32;
    result = decodeTiff(writeTiff(directory, "planar_big.tif", planar), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, planar, {0, 1, 2}));

    WriteOptions tiled = strips;
    tiled.tileWidth = 128;
    tiled.tileHeight = 64;
    result = decodeTiff(writeTiff(directory, "tiled_big.tif", tiled), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, tiled, {0, 1, 2}));

    WriteOptions tiledPlanar = tiled;
    tiledPlanar.planar = PLANARCONFIG_SEPARATE;
    result = decodeTiff(writeTiff(directory, "tiled_planar_big.tif", tiledPlanar), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, tiledPlanar, {0, 1, 2}));

    // 2 ページ目も、同じ手順で読める (ハンドルを開き直して、ページを選ぶ)。
    WriteOptions second = strips;
    second.width = 1600;
    const QString path =
        writeTiff(directory, "two_big.tif", std::vector<WriteOptions>{strips, second});
    result = decodeTiff(path, 1, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QVERIFY(matches(result.raw, second, {0, 1, 2}));
  }

  // 壊れたファイル (ストリップの途中で切れた) は、並列でも失敗として返る。
  void failsOnATruncatedLargeImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions strips;
    strips.width = 1500;
    strips.height = 700;
    strips.bits = 16;
    strips.compression = COMPRESSION_NONE;
    const QString path = writeTiff(directory, "cut.tif", strips);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(file.size() / 2));
    file.close();
    const auto result = decodeTiff(path, 0, true);
    QVERIFY(!result.raw.ok);
  }

  // CMYK は、標本をそのまま読んで RGB へ変換する (インクの量は、0 がインク無し)。
  // プロファイルが無い、または読めないときは、R = (1 - C) * (1 - K) の素朴な式。
  // 出力は RGB のため、CMYK 用のプロファイルは付けない。アルファは通す。
  void convertsCmykToRgb() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto write = [&](const char *name, int bits, int samples, const QByteArray &data,
                           const QByteArray &icc) {
      const QString path = QDir(directory.path()).filePath(name);
      TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "w");
      TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 2);
      TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1);
      TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, bits);
      TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, samples);
      TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
      TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
      TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);
      if (samples == 5) {
        const uint16_t extras[1] = {EXTRASAMPLE_UNASSALPHA};
        TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, extras);
      }
      if (!icc.isEmpty()) {
        TIFFSetField(tiff, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(icc.size()), icc.constData());
      }
      QByteArray row = data;
      TIFFWriteScanline(tiff, row.data(), 0);
      TIFFClose(tiff);
      return path;
    };

    // 8bit。(1) インク無し → 白。(2) C が 100% → R は 0、G と B は
    // 255。壊れたプロファイルでも読める。
    const QByteArray eight = QByteArray::fromHex("00000000ff000000");
    auto result = decodeTiff(write("cmyk8.tif", 8, 4, eight, QByteArray("not a profile")), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.channels, 3);
    QVERIFY(result.raw.iccProfile.isEmpty());
    const auto *bytes = reinterpret_cast<const uchar *>(result.raw.pixels.constData());
    QCOMPARE(int(bytes[0]), 255);
    QCOMPARE(int(bytes[1]), 255);
    QCOMPARE(int(bytes[2]), 255);
    QCOMPARE(int(bytes[3]), 0);
    QCOMPARE(int(bytes[4]), 255);
    QCOMPARE(int(bytes[5]), 255);

    // 16bit + アルファ。K が 50% なら、そのぶん暗い。
    QByteArray wide;
    // 1 画素目: C M Y K = 0 0 0 50%、アルファ 100%。2 画素目: C 100%、アルファ 1000。
    for (const quint16 value :
         {quint16(0), quint16(0), quint16(0), quint16(32768), quint16(65535), quint16(65535),
          quint16(0), quint16(0), quint16(0), quint16(1000)}) {
      wide.append(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    result = decodeTiff(write("cmyk16a.tif", 16, 5, wide, {}), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.channels, 4);
    QCOMPARE(result.raw.sampleType, PixelSampleType::UInt16);
    const auto *words = reinterpret_cast<const quint16 *>(result.raw.pixels.constData());
    QCOMPARE(int(words[0]), 32767);  // (65535 - 0) * (65535 - 32768) / 65535
    QCOMPARE(int(words[3]), 65535);
    QCOMPARE(int(words[4]), 0);
    QCOMPARE(int(words[7]), 1000);
  }

  // 「最小が白」のグレーは、8bit も 16bit も反転して、白が白になる。
  void invertsMinIsWhiteGray() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const int bits : {8, 16}) {
      WriteOptions options;
      options.samples = 1;
      options.bits = bits;
      options.photometric = PHOTOMETRIC_MINISWHITE;
      const auto result = decodeTiff(writeTiff(directory, "white.tif", options), 0, true);
      QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
      QVERIFY2(matches(result.raw, options, {0}, true), qPrintable(QString("bits %1").arg(bits)));
    }
  }

  // ---- 変換して読む形 ----

  void expandsAPalette() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("palette.tif");
    TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "w");
    QVERIFY(tiff != nullptr);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 4);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_PALETTE);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);
    uint16_t red[256] = {};
    uint16_t green[256] = {};
    uint16_t blue[256] = {};
    red[1] = 65535;    // 1: 赤
    green[2] = 65535;  // 2: 緑
    blue[3] = 65535;   // 3: 青
    TIFFSetField(tiff, TIFFTAG_COLORMAP, red, green, blue);
    uchar row[4] = {0, 1, 2, 3};
    QVERIFY(TIFFWriteScanline(tiff, row, 0) >= 0);
    TIFFClose(tiff);

    const auto result = decodeTiff(path, 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.channels, 3);
    const auto *pixels = reinterpret_cast<const uchar *>(result.raw.pixels.constData());
    QCOMPARE(int(pixels[0 * 3 + 0]), 0);    // 0: 黒
    QCOMPARE(int(pixels[1 * 3 + 0]), 255);  // 赤
    QCOMPARE(int(pixels[2 * 3 + 1]), 255);  // 緑
    QCOMPARE(int(pixels[3 * 3 + 2]), 255);  // 青
  }

  // 1bit は、0 と 255 へ展開する (最小が白なら反転)。
  void expandsOneBitImages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const int photometric : {PHOTOMETRIC_MINISBLACK, PHOTOMETRIC_MINISWHITE}) {
      const QString path = QDir(directory.path()).filePath("bilevel.tif");
      TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "w");
      QVERIFY(tiff != nullptr);
      TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 8);
      TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1);
      TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 1);
      TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
      TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, photometric);
      TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);
      uchar row[1] = {0b10101010};  // 先頭から 1, 0, 1, 0, ...
      QVERIFY(TIFFWriteScanline(tiff, row, 0) >= 0);
      TIFFClose(tiff);

      const auto result = decodeTiff(path, 0, true);
      QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
      const auto *pixels = reinterpret_cast<const uchar *>(result.raw.pixels.constData());
      const int one = photometric == PHOTOMETRIC_MINISBLACK ? 255 : 0;
      QCOMPARE(int(pixels[0]), one);
      QCOMPARE(int(pixels[3]), 255 - one);
    }
  }

  // ---- 向き ----

  // 向きの適用。3x2 の画素 (a b c / d e f) が、8 通りの向きで正しい位置へ行く。
  void appliesEveryOrientation() {
    const auto make = []() {
      RawImage image;
      image.ok = true;
      image.width = 3;
      image.height = 2;
      image.channels = 1;
      image.sampleType = PixelSampleType::UInt8;
      image.pixels = QByteArray("abcdef");
      return image;
    };
    struct Case {
      int orientation;
      int width;
      int height;
      const char *expected;
    };
    const Case cases[] = {
        {1, 3, 2, "abcdef"}, {2, 3, 2, "cbafed"}, {3, 3, 2, "fedcba"}, {4, 3, 2, "defabc"},
        {5, 2, 3, "adbecf"}, {6, 2, 3, "daebfc"}, {7, 2, 3, "fcebda"}, {8, 2, 3, "cfbead"},
    };
    for (const auto &entry : cases) {
      RawImage image = make();
      applyOrientationToRawImage(&image, entry.orientation);
      QCOMPARE(image.width, entry.width);
      QCOMPARE(image.height, entry.height);
      QCOMPARE(image.pixels, QByteArray(entry.expected));
    }

    // 範囲外は何もしない。
    RawImage untouched = make();
    applyOrientationToRawImage(&untouched, 0);
    applyOrientationToRawImage(&untouched, 9);
    QCOMPARE(untouched.pixels, QByteArray("abcdef"));
  }

  // ファイルの向きは、適用する指定のときだけ適用する。
  void appliesTheOrientationTagOnlyWhenAsked() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions options;
    options.orientation = 6;  // 90 度 (時計回り)
    const QString path = writeTiff(directory, "rotated.tif", options);

    const auto asStored = decodeTiff(path, 0, false);
    QVERIFY(asStored.raw.ok);
    QCOMPARE(asStored.raw.width, kWidth);
    QCOMPARE(asStored.raw.height, kHeight);

    const auto rotated = decodeTiff(path, 0, true);
    QVERIFY(rotated.raw.ok);
    QCOMPARE(rotated.raw.width, kHeight);
    QCOMPARE(rotated.raw.height, kWidth);
    // 元の (0, 0) は、時計回りに回すと、右上 (幅 - 1, 0) へ行く。
    const auto *stored = reinterpret_cast<const uchar *>(asStored.raw.pixels.constData());
    const auto *turned = reinterpret_cast<const uchar *>(rotated.raw.pixels.constData());
    QCOMPARE(int(turned[(0 * rotated.raw.width + (kHeight - 1)) * 3]), int(stored[0]));
  }

  // 復号の経路では、適用した向きを、そのページの値として記録する。適用しなければ記録しない。
  void recordsTheAppliedOrientation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions first;
    first.orientation = 6;
    WriteOptions second;  // 2 ページ目は向きなし
    const QString path = writeTiff(directory, "o.tif", std::vector<WriteOptions>{first, second});

    const auto honored = ImageDecoder().decode(path, true, 0);
    QVERIFY2(honored.ok, qPrintable(honored.errorMessage));
    QCOMPARE(honored.decodedImage.appliedExifOrientation, ExifOrientation::Rotate90);
    QCOMPARE(honored.decodedImage.size(), QSize(kHeight, kWidth));

    // 2 ページ目は、先頭のページの向きに引きずられない。
    const auto secondPage = ImageDecoder().decode(path, true, 1);
    QVERIFY2(secondPage.ok, qPrintable(secondPage.errorMessage));
    QCOMPARE(secondPage.decodedImage.appliedExifOrientation, ExifOrientation::Normal);
    QCOMPARE(secondPage.decodedImage.size(), QSize(kWidth, kHeight));

    const auto ignored = ImageDecoder().decode(path, false, 0);
    QVERIFY2(ignored.ok, qPrintable(ignored.errorMessage));
    QCOMPARE(ignored.decodedImage.appliedExifOrientation, ExifOrientation::Normal);
    QCOMPARE(ignored.decodedImage.size(), QSize(kWidth, kHeight));
  }

  // ---- ページ、プロファイル ----

  void decodesEachPageOfAMultipageFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions first;
    WriteOptions second;
    second.width = 9;
    second.height = 5;
    second.bits = 16;
    WriteOptions third;
    third.samples = 1;
    third.photometric = PHOTOMETRIC_MINISBLACK;
    const QString path =
        writeTiff(directory, "pages.tif", std::vector<WriteOptions>{first, second, third});
    QVERIFY(!path.isEmpty());

    const auto page0 = decodeTiff(path, 0, true);
    QVERIFY2(page0.raw.ok, qPrintable(page0.raw.errorMessage));
    QCOMPARE(page0.pageCount, 3);
    QVERIFY(matches(page0.raw, first, {0, 1, 2}));

    const auto page1 = decodeTiff(path, 1, true);
    QVERIFY2(page1.raw.ok, qPrintable(page1.raw.errorMessage));
    QCOMPARE(page1.raw.width, 9);
    QCOMPARE(page1.raw.sampleType, PixelSampleType::UInt16);
    QVERIFY(matches(page1.raw, second, {0, 1, 2}));

    const auto page2 = decodeTiff(path, 2, true);
    QVERIFY2(page2.raw.ok, qPrintable(page2.raw.errorMessage));
    QCOMPARE(page2.raw.channels, 1);

    QVERIFY(!decodeTiff(path, 3, true).raw.ok);
    QVERIFY(!decodeTiff(path, -1, true).raw.ok);

    // 復号の経路では、ページになる。
    const auto decoded = ImageDecoder().decode(path, true, 1);
    QVERIFY2(decoded.ok, qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.decodedImage.decoderBackend, QString("tiff"));
    QCOMPARE(decoded.decodedImage.subImageKind, SubImageKind::Pages);
    QCOMPARE(decoded.decodedImage.subImageCount, 3);
    QCOMPARE(decoded.decodedImage.subImageIndex, 1);
  }

  void extractsTheEmbeddedIccProfile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions options;
    options.icc = QByteArray("ICC-DATA-1234");
    const auto result = decodeTiff(writeTiff(directory, "icc.tif", options), 0, true);
    QVERIFY2(result.raw.ok, qPrintable(result.raw.errorMessage));
    QCOMPARE(result.raw.iccProfile, QByteArray("ICC-DATA-1234"));
  }

  // ---- 失敗 ----

  // 読めない形は、失敗を返す (呼び側が退避する)。壊れたファイルは、落ちずに失敗を返す。
  void failsOnUnsupportedOrBrokenFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 32bit の整数。
    WriteOptions integer32;
    integer32.bits = 32;
    QVERIFY(!decodeTiff(writeTiff(directory, "u32.tif", integer32), 0, true).raw.ok);

    // 16bit で、チャンネルが多い (RGB + 2 つの余分)。8bit なら、libtiff の変換で読める。
    WriteOptions many;
    many.samples = 5;
    many.bits = 16;
    QVERIFY(!decodeTiff(writeTiff(directory, "many.tif", many), 0, true).raw.ok);

    QVERIFY(!decodeTiff(QDir(directory.path()).filePath("missing.tif"), 0, true).raw.ok);

    const auto write = [&directory](const char *name, const QByteArray &bytes) {
      const QString path = QDir(directory.path()).filePath(name);
      QFile file(path);
      file.open(QIODevice::WriteOnly);
      file.write(bytes);
      return path;
    };
    QVERIFY(!decodeTiff(write("garbage.tif", QByteArray(300, 'x')), 0, true).raw.ok);
    QVERIFY(!decodeTiff(write("empty.tif", QByteArray()), 0, true).raw.ok);

    // 途中で切れている。
    WriteOptions good;
    good.compression = COMPRESSION_ADOBE_DEFLATE;
    QFile source(writeTiff(directory, "good.tif", good));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray bytes = source.readAll();
    QVERIFY(!decodeTiff(write("cut.tif", bytes.left(bytes.size() / 2)), 0, true).raw.ok);
  }

  // 日本語のパスでも読める。
  void decodesFromANonAsciiPath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = writeTiff(directory, "plain.tif", WriteOptions());
    const QString copy = QDir(directory.path()).filePath(QString::fromUtf8("画像 テスト.tif"));
    QVERIFY(QFile::copy(source, copy));
    QVERIFY(decodeTiff(copy, 0, true).raw.ok);
  }

  // 復号の経路を通ると、解析用の値として読める。
  void decodesThroughTheDecoder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WriteOptions options;
    options.bits = 16;
    const auto result = ImageDecoder().decode(writeTiff(directory, "d.tif", options), true);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.decodedImage.decoderBackend, QString("tiff"));
    QCOMPARE(result.decodedImage.sourceSampleType, PixelSampleType::UInt16);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        for (int c = 0; c < 3; ++c) {
          const float expected = static_cast<float>(level(x, y, c) * 257) / 65535.0F;
          QVERIFY(std::abs(result.decodedImage.sampleRgba(x, y, c) - expected) < 1e-6F);
        }
      }
    }
  }
};

QTEST_MAIN(TiffDecodeTests)

#include "TiffDecodeTests.moc"
