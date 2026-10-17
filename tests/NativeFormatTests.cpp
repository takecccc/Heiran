// 形式ごとの自前の復号器 (HDR、TGA、DPX、PSD)。
//
// 入力は、テストの中でバイト列から組み立てる。外部のツールに頼らず、向き、圧縮、詰め方の
// 組み合わせを、期待する画素を手で計算できる形で確かめる。

#include "heiran/infra/DpxDecoder.hpp"
#include "heiran/infra/HdrDecoder.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/infra/PsdDecoder.hpp"
#include "heiran/infra/RawImage.hpp"
#include "heiran/infra/TgaDecoder.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <vector>

using namespace heiran::infra;

namespace {

// ---- 共通 ----

void append(QByteArray *bytes, std::initializer_list<int> values) {
  for (const int value : values) {
    bytes->append(static_cast<char>(value));
  }
}

void putU16(QByteArray *bytes, int value, bool bigEndian) {
  if (bigEndian) {
    append(bytes, {(value >> 8) & 0xFF, value & 0xFF});
  } else {
    append(bytes, {value & 0xFF, (value >> 8) & 0xFF});
  }
}

void putU32(QByteArray *bytes, quint32 value, bool bigEndian) {
  if (bigEndian) {
    append(bytes, {static_cast<int>(value >> 24), static_cast<int>((value >> 16) & 0xFF),
                   static_cast<int>((value >> 8) & 0xFF), static_cast<int>(value & 0xFF)});
  } else {
    append(bytes, {static_cast<int>(value & 0xFF), static_cast<int>((value >> 8) & 0xFF),
                   static_cast<int>((value >> 16) & 0xFF), static_cast<int>(value >> 24)});
  }
}

const uchar *u8Pixels(const RawImage &image) {
  return reinterpret_cast<const uchar *>(image.pixels.constData());
}

const quint16 *u16Pixels(const RawImage &image) {
  return reinterpret_cast<const quint16 *>(image.pixels.constData());
}

const float *floatPixels(const RawImage &image) {
  return reinterpret_cast<const float *>(image.pixels.constData());
}

// ---- HDR ----

QByteArray hdrHeader(int width, int height, const char *resolution = nullptr,
                     const char *format = "FORMAT=32-bit_rle_rgbe") {
  QByteArray bytes("#?RADIANCE\n");
  bytes += format;
  bytes += "\n\n";
  if (resolution != nullptr) {
    bytes += resolution;
  } else {
    bytes += QString("-Y %1 +X %2").arg(height).arg(width).toLatin1();
  }
  bytes += "\n";
  return bytes;
}

// 新しい RLE の行を作る。値が揃っている所は繰り返しの印、それ以外は、そのままの並び。
QByteArray newRleScanline(const std::vector<std::array<uchar, 4>> &pixels) {
  const int width = static_cast<int>(pixels.size());
  QByteArray bytes;
  append(&bytes, {2, 2, width >> 8, width & 0xFF});
  for (int plane = 0; plane < 4; ++plane) {
    int column = 0;
    while (column < width) {
      int run = 1;
      while (column + run < width && run < 127 &&
             pixels[column + run][plane] == pixels[column][plane]) {
        ++run;
      }
      if (run >= 3) {
        append(&bytes, {128 + run, pixels[column][plane]});
        column += run;
      } else {
        // そのままの並び (最大 128)。次の繰り返しまでを 1 つにまとめる。
        int literal = 0;
        while (column + literal < width && literal < 128) {
          int ahead = 1;
          while (column + literal + ahead < width && ahead < 3 &&
                 pixels[column + literal + ahead][plane] == pixels[column + literal][plane]) {
            ++ahead;
          }
          if (ahead >= 3) {
            break;
          }
          ++literal;
        }
        literal = std::max(literal, 1);
        append(&bytes, {literal});
        for (int index = 0; index < literal; ++index) {
          bytes.append(static_cast<char>(pixels[column + index][plane]));
        }
        column += literal;
      }
    }
  }
  return bytes;
}

// ---- TGA ----

QByteArray tgaHeader(int imageType, int width, int height, int bitsPerPixel, int descriptor,
                     int colorMapType = 0, int colorMapLength = 0, int colorMapBits = 0) {
  QByteArray bytes;
  append(&bytes, {0, colorMapType, imageType});
  putU16(&bytes, 0, false);  // 色表の最初の番号
  putU16(&bytes, colorMapLength, false);
  append(&bytes, {colorMapBits});
  putU16(&bytes, 0, false);  // x の原点
  putU16(&bytes, 0, false);  // y の原点
  putU16(&bytes, width, false);
  putU16(&bytes, height, false);
  append(&bytes, {bitsPerPixel, descriptor});
  return bytes;
}

// ---- DPX ----

struct DpxOptions {
  int width = 2;
  int height = 1;
  int descriptor = 50;
  int bits = 8;
  int packing = 0;
  int orientation = 0;
  int endOfLinePadding = 0;
  bool bigEndian = true;
  int encoding = 0;
  quint32 dataSign = 0;
};

QByteArray dpxFile(const DpxOptions &options, const QByteArray &imageData) {
  constexpr int kDataOffset = 2048;
  QByteArray bytes(kDataOffset, '\0');
  const auto put32 = [&](int offset, quint32 value) {
    QByteArray field;
    putU32(&field, value, options.bigEndian);
    bytes.replace(offset, 4, field);
  };
  const auto put16 = [&](int offset, int value) {
    QByteArray field;
    putU16(&field, value, options.bigEndian);
    bytes.replace(offset, 2, field);
  };
  // 印の値は常に 0x53445058 ("SDPX")。リトルエンディアンで書くと、先頭のバイトが "XPDS" になる。
  put32(0, 0x53445058u);
  put32(4, kDataOffset);
  put16(768, options.orientation);
  put16(770, 1);
  put32(772, options.width);
  put32(776, options.height);
  put32(780, options.dataSign);
  bytes[800] = static_cast<char>(options.descriptor);
  bytes[803] = static_cast<char>(options.bits);
  put16(804, options.packing);
  put16(806, options.encoding);
  put32(808, kDataOffset);
  put32(812, options.endOfLinePadding);
  return bytes + imageData;
}

QByteArray u32Word(quint32 value, bool bigEndian) {
  QByteArray bytes;
  putU32(&bytes, value, bigEndian);
  return bytes;
}

// ---- PSD ----

struct PsdOptions {
  int width = 2;
  int height = 1;
  int channels = 3;
  int depth = 8;
  int colorMode = 3;
  int compression = 0;
  int version = 1;
  QByteArray imageResources;
};

QByteArray psdFile(const PsdOptions &options, const QByteArray &imageData) {
  QByteArray bytes("8BPS");
  putU16(&bytes, options.version, true);
  bytes += QByteArray(6, '\0');
  putU16(&bytes, options.channels, true);
  putU32(&bytes, options.height, true);
  putU32(&bytes, options.width, true);
  putU16(&bytes, options.depth, true);
  putU16(&bytes, options.colorMode, true);
  putU32(&bytes, 0, true);  // カラーモードのデータ
  putU32(&bytes, options.imageResources.size(), true);
  bytes += options.imageResources;
  putU32(&bytes, 0, true);  // レイヤーとマスク
  putU16(&bytes, options.compression, true);
  return bytes + imageData;
}

QByteArray packBitsRow(const QByteArray &row) {
  // 簡単のため、繰り返しだけを圧縮し、それ以外は 1 バイトずつのそのままの並びにする。
  QByteArray bytes;
  int index = 0;
  while (index < row.size()) {
    int run = 1;
    while (index + run < row.size() && run < 128 && row[index + run] == row[index]) {
      ++run;
    }
    if (run >= 2) {
      bytes.append(static_cast<char>(1 - run));
      bytes.append(row[index]);
      index += run;
    } else {
      bytes.append('\0');
      bytes.append(row[index]);
      ++index;
    }
  }
  return bytes;
}

}  // namespace

class NativeFormatTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ============================== HDR ==============================

  void hdrDecodesFlatPixels() {
    QByteArray file = hdrHeader(2, 1);
    append(&file, {128, 64, 32, 129, 0, 0, 0, 0});  // 2 画素目は指数 0 (黒)。

    const RawImage image = decodeRadianceHdr(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.width, 2);
    QCOMPARE(image.height, 1);
    QCOMPARE(image.channels, 3);
    QCOMPARE(image.sampleType, PixelSampleType::Float32);
    const float *pixels = floatPixels(image);
    // 仮数 / 256 * 2^(指数 - 128) = 仮数 * 2^(指数 - 136)。129 なら 1/128。
    QCOMPARE(pixels[0], 1.0F);
    QCOMPARE(pixels[1], 0.5F);
    QCOMPARE(pixels[2], 0.25F);
    QCOMPARE(pixels[3], 0.0F);
  }

  // 1.0 を超える値も、そのまま持つ。
  void hdrKeepsValuesAboveOne() {
    QByteArray file = hdrHeader(1, 1);
    append(&file, {200, 100, 50, 136});  // 指数 136 は 2^0 倍。
    const RawImage image = decodeRadianceHdr(file);
    QVERIFY(image.ok);
    QCOMPARE(floatPixels(image)[0], 200.0F);
    QCOMPARE(floatPixels(image)[2], 50.0F);
  }

  // 新しい RLE (幅 8 以上)。繰り返しと、そのままの並びが混ざる行を読めること。
  void hdrDecodesNewRle() {
    std::vector<std::array<uchar, 4>> row;
    for (int column = 0; column < 20; ++column) {
      // 前半は同じ色の繰り返し、後半は画素ごとに違う色。
      row.push_back(column < 10
                        ? std::array<uchar, 4>{128, 128, 128, 129}
                        : std::array<uchar, 4>{static_cast<uchar>(10 + column), 20, 30, 129});
    }
    QByteArray file = hdrHeader(20, 1);
    file += newRleScanline(row);

    const RawImage image = decodeRadianceHdr(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(floatPixels(image)[0], 1.0F);
    QCOMPARE(floatPixels(image)[9 * 3], 1.0F);
    QCOMPARE(floatPixels(image)[10 * 3], static_cast<float>(20) / 128.0F);
    QCOMPARE(floatPixels(image)[19 * 3], static_cast<float>(29) / 128.0F);
  }

  // 古い RLE。`1 1 1 回数` が、直前の画素の繰り返しを表す。
  void hdrDecodesOldRle() {
    QByteArray file = hdrHeader(5, 1);
    append(&file, {128, 128, 128, 129});  // 1 画素。
    append(&file, {1, 1, 1, 4});          // それを、4 回繰り返す。
    const RawImage image = decodeRadianceHdr(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    for (int column = 0; column < 5; ++column) {
      QCOMPARE(floatPixels(image)[column * 3], 1.0F);
    }
  }

  // 向きの指定。+Y は下から上、-X は右から左。
  void hdrAppliesTheOrientation() {
    QByteArray top = hdrHeader(2, 2, "+Y 2 +X 2");
    // ファイルの順は、下の行が先。
    append(&top, {128, 128, 128, 129, 64, 64, 64, 129});  // 下の行: 1.0、0.5
    append(&top, {32, 32, 32, 129, 16, 16, 16, 129});     // 上の行: 0.25、0.125
    const RawImage flipped = decodeRadianceHdr(top);
    QVERIFY(flipped.ok);
    QCOMPARE(floatPixels(flipped)[0], 0.25F);     // 画像の上の行の左。
    QCOMPARE(floatPixels(flipped)[2 * 3], 1.0F);  // 下の行の左。

    QByteArray mirrored = hdrHeader(2, 1, "-Y 1 -X 2");
    append(&mirrored, {128, 128, 128, 129, 64, 64, 64, 129});
    const RawImage mirror = decodeRadianceHdr(mirrored);
    QVERIFY(mirror.ok);
    QCOMPARE(floatPixels(mirror)[0], 0.5F);
    QCOMPARE(floatPixels(mirror)[3], 1.0F);
  }

  void hdrRejectsBrokenFiles() {
    QVERIFY(!decodeRadianceHdr("not a radiance file\n").ok);
    QVERIFY(!decodeRadianceHdr(QByteArray()).ok);

    QByteArray xyze = hdrHeader(1, 1, nullptr, "FORMAT=32-bit_rle_xyze");
    append(&xyze, {1, 2, 3, 129});
    QVERIFY(!decodeRadianceHdr(xyze).ok);

    QByteArray transposed = hdrHeader(1, 1, "+X 1 -Y 1");
    append(&transposed, {1, 2, 3, 129});
    QVERIFY(!decodeRadianceHdr(transposed).ok);

    QByteArray truncated = hdrHeader(4, 1);
    append(&truncated, {128, 128, 128, 129});  // 4 画素のうち 1 画素しかない。
    QVERIFY(!decodeRadianceHdr(truncated).ok);

    // 幅が合わない RLE の行。
    QByteArray wrongWidth = hdrHeader(20, 1);
    append(&wrongWidth, {2, 2, 0, 30});
    wrongWidth += QByteArray(200, '\0');
    QVERIFY(!decodeRadianceHdr(wrongWidth).ok);

    // 壊れた大きさ。
    QVERIFY(!decodeRadianceHdr(hdrHeader(1, 1, "-Y 0 +X 0")).ok);
    QVERIFY(!decodeRadianceHdr(hdrHeader(1, 1, "-Y 99999999 +X 99999999")).ok);
  }

  // ============================== TGA ==============================

  // 24bit のフルカラー。既定の原点は左下で、上下を直す。BGR の並びを RGB へ直す。
  void tgaDecodesTrueColorFromTheBottomLeft() {
    QByteArray file = tgaHeader(2, 2, 2, 24, 0);
    // ファイルの先頭の行が、画像の下の行。青緑 (B,G,R の順で書く)。
    append(&file, {255, 0, 0, 0, 255, 0});      // 下の行: 青、緑
    append(&file, {0, 0, 255, 255, 255, 255});  // 上の行: 赤、白

    const RawImage image = decodeTga(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.channels, 3);
    const uchar *pixels = u8Pixels(image);
    QCOMPARE(int(pixels[0]), 255);  // 上の行の左は赤: R=255
    QCOMPARE(int(pixels[1]), 0);
    QCOMPARE(int(pixels[2]), 0);
    QCOMPARE(int(pixels[3]), 255);      // 上の行の右は白
    QCOMPARE(int(pixels[6 + 2]), 255);  // 下の行の左は青: B=255
    QCOMPARE(int(pixels[9 + 1]), 255);  // 下の行の右は緑: G=255
  }

  // 記述子の上下の印が立っていれば、ファイルの順がそのまま上から。右から左の印は左右を直す。
  void tgaAppliesTheOrigin() {
    QByteArray topLeft = tgaHeader(2, 2, 1, 24, 0x20);
    append(&topLeft, {0, 0, 255, 0, 255, 0});  // 赤、緑
    const RawImage normal = decodeTga(topLeft);
    QVERIFY(normal.ok);
    QCOMPARE(int(u8Pixels(normal)[0]), 255);
    QCOMPARE(int(u8Pixels(normal)[4]), 255);

    QByteArray rightToLeft = tgaHeader(2, 2, 1, 24, 0x20 | 0x10);
    append(&rightToLeft, {0, 0, 255, 0, 255, 0});
    const RawImage mirrored = decodeTga(rightToLeft);
    QVERIFY(mirrored.ok);
    QCOMPARE(int(u8Pixels(mirrored)[1]), 255);  // 左が緑。
    QCOMPARE(int(u8Pixels(mirrored)[3]), 255);  // 右が赤。
  }

  // 32bit は、アルファのビット数が 8 ならアルファを持つ。0 なら持たない。
  void tgaDecodesAlpha() {
    QByteArray withAlpha = tgaHeader(2, 1, 1, 32, 0x20 | 8);
    append(&withAlpha, {0, 0, 255, 128});
    const RawImage image = decodeTga(withAlpha);
    QVERIFY(image.ok);
    QCOMPARE(image.channels, 4);
    QCOMPARE(int(u8Pixels(image)[0]), 255);
    QCOMPARE(int(u8Pixels(image)[3]), 128);

    QByteArray withoutAlpha = tgaHeader(2, 1, 1, 32, 0x20);
    append(&withoutAlpha, {0, 0, 255, 7});
    const RawImage opaque = decodeTga(withoutAlpha);
    QVERIFY(opaque.ok);
    QCOMPARE(opaque.channels, 3);
  }

  // 16bit は A1R5G5B5。31 が 255 になるよう、8bit へ引き伸ばす。
  void tgaDecodes16BitColor() {
    QByteArray file = tgaHeader(2, 1, 1, 16, 0x20 | 1);
    putU16(&file, 0x8000 | (31 << 10), false);  // アルファ 1、赤 31
    const RawImage image = decodeTga(file);
    QVERIFY(image.ok);
    QCOMPARE(image.channels, 4);
    QCOMPARE(int(u8Pixels(image)[0]), 255);
    QCOMPARE(int(u8Pixels(image)[1]), 0);
    QCOMPARE(int(u8Pixels(image)[3]), 255);
  }

  void tgaDecodesGray() {
    QByteArray file = tgaHeader(3, 2, 1, 8, 0x20);
    append(&file, {10, 200});
    const RawImage image = decodeTga(file);
    QVERIFY(image.ok);
    QCOMPARE(image.channels, 1);
    QCOMPARE(int(u8Pixels(image)[0]), 10);
    QCOMPARE(int(u8Pixels(image)[1]), 200);
  }

  void tgaDecodesAColorMappedImage() {
    // 24bit のパレット 2 色 (青、赤。BGR の並び)。
    QByteArray file = tgaHeader(1, 3, 1, 8, 0x20, 1, 2, 24);
    append(&file, {255, 0, 0, 0, 0, 255});  // 0: 青、1: 赤
    append(&file, {1, 0, 1});
    const RawImage image = decodeTga(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.channels, 3);
    QCOMPARE(int(u8Pixels(image)[0]), 255);      // 赤
    QCOMPARE(int(u8Pixels(image)[3 + 2]), 255);  // 青
    QCOMPARE(int(u8Pixels(image)[6]), 255);      // 赤
  }

  // RLE。繰り返しの塊と、そのままの塊が混ざり、行をまたぐこと。
  void tgaDecodesRle() {
    QByteArray file = tgaHeader(10, 3, 2, 24, 0x20);
    // 繰り返し: 赤を 4 画素 (3 画素目まで 1 行目、残りは 2 行目へまたぐ)。
    append(&file, {0x80 | 3, 0, 0, 255});
    // そのまま: 2 画素 (緑、青)。
    append(&file, {1, 0, 255, 0, 255, 0, 0});

    const RawImage image = decodeTga(file);
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    const uchar *pixels = u8Pixels(image);
    for (int index = 0; index < 4; ++index) {
      QCOMPARE(int(pixels[index * 3]), 255);
    }
    QCOMPARE(int(pixels[4 * 3 + 1]), 255);  // 緑
    QCOMPARE(int(pixels[5 * 3 + 2]), 255);  // 青
  }

  void tgaRejectsBrokenFiles() {
    QVERIFY(!decodeTga(QByteArray(5, '\0')).ok);
    QVERIFY(!decodeTga(tgaHeader(0, 1, 1, 24, 0)).ok);                        // 画像無し
    QVERIFY(!decodeTga(tgaHeader(2, 0, 1, 24, 0)).ok);                        // 幅 0
    QVERIFY(!decodeTga(tgaHeader(2, 2, 2, 24, 0) + QByteArray(5, '\0')).ok);  // 画素が足りない
    QVERIFY(!decodeTga(tgaHeader(2, 1, 1, 12, 0) + QByteArray(8, '\0')).ok);  // 未対応の深さ
    // RLE が途中で切れている。
    QByteArray truncated = tgaHeader(10, 4, 1, 24, 0x20);
    append(&truncated, {0x80 | 3, 0, 0});
    QVERIFY(!decodeTga(truncated).ok);
  }

  // ============================== DPX ==============================

  void dpxDecodes8BitRgb() {
    QByteArray data;
    append(&data, {255, 0, 0, 0, 255, 0});
    const RawImage image = decodeDpx(dpxFile({.width = 2, .height = 1, .bits = 8}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.channels, 3);
    QCOMPARE(image.sampleType, PixelSampleType::UInt8);
    QCOMPARE(int(u8Pixels(image)[0]), 255);
    QCOMPARE(int(u8Pixels(image)[4]), 255);
  }

  // 16bit は、ビッグエンディアンでもリトルエンディアンでも、同じ画素になること。
  void dpxDecodes16BitInBothByteOrders() {
    for (const bool bigEndian : {true, false}) {
      QByteArray data;
      for (const int value : {0xFFFF, 0x1234, 0x0000}) {
        putU16(&data, value, bigEndian);
      }
      const RawImage image =
          decodeDpx(dpxFile({.width = 1, .height = 1, .bits = 16, .bigEndian = bigEndian}, data));
      QVERIFY2(image.ok, qPrintable(image.errorMessage));
      QCOMPARE(image.sampleType, PixelSampleType::UInt16);
      QCOMPARE(int(u16Pixels(image)[0]), 0xFFFF);
      QCOMPARE(int(u16Pixels(image)[1]), 0x1234);
      QCOMPARE(int(u16Pixels(image)[2]), 0);
    }
  }

  // 10bit、A (余りのビットが下)。1 語に R、G、B。最大値は 16bit の最大値になる。
  void dpxDecodes10BitMethodA() {
    for (const bool bigEndian : {true, false}) {
      // R = 1023、G = 512、B = 0。A は、上に詰めて、下の 2 ビットが余り。
      const quint32 word = (1023u << 22) | (512u << 12) | (0u << 2);
      const RawImage image = decodeDpx(
          dpxFile({.width = 1, .height = 1, .bits = 10, .packing = 1, .bigEndian = bigEndian},
                  u32Word(word, bigEndian)));
      QVERIFY2(image.ok, qPrintable(image.errorMessage));
      QCOMPARE(image.sampleType, PixelSampleType::UInt16);
      QCOMPARE(int(u16Pixels(image)[0]), 65535);
      QCOMPARE(int(u16Pixels(image)[1]), (512 * 65535 + 511) / 1023);
      QCOMPARE(int(u16Pixels(image)[2]), 0);
    }
  }

  // 10bit、B (余りのビットが上)。
  void dpxDecodes10BitMethodB() {
    const quint32 word = (1023u << 20) | (512u << 10) | 1u;
    const RawImage image = decodeDpx(
        dpxFile({.width = 1, .height = 1, .bits = 10, .packing = 2}, u32Word(word, true)));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(int(u16Pixels(image)[0]), 65535);
    QCOMPARE(int(u16Pixels(image)[2]), (1 * 65535 + 511) / 1023);
  }

  // 10bit で、すき間なく詰めた形 (語の下位のビットから詰める)。標本が 2 語にまたがる。
  void dpxDecodes10BitPackedAcrossWords() {
    // 幅 2 の RGB は 6 標本 = 60 ビット。2 語 (64 ビット) に収まる。標本: 1, 2, 3, 4, 5, 1023。
    const int samples[6] = {1, 2, 3, 4, 5, 1023};
    quint64 bits = 0;
    for (int index = 0; index < 6; ++index) {
      bits |= static_cast<quint64>(samples[index]) << (index * 10);
    }
    QByteArray data;
    putU32(&data, static_cast<quint32>(bits & 0xFFFFFFFFu), true);  // 1 語目: 下位から
    putU32(&data, static_cast<quint32>(bits >> 32), true);

    const RawImage image =
        decodeDpx(dpxFile({.width = 2, .height = 1, .bits = 10, .packing = 0}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    for (int index = 0; index < 6; ++index) {
      QCOMPARE(int(u16Pixels(image)[index]), (samples[index] * 65535 + 511) / 1023);
    }
  }

  // 12bit は、16bit の語の上 (A) か下 (B)。
  void dpxDecodes12Bit() {
    QByteArray dataA;
    for (const int value : {4095, 2048, 0}) {
      putU16(&dataA, value << 4, true);
    }
    const RawImage a =
        decodeDpx(dpxFile({.width = 1, .height = 1, .bits = 12, .packing = 1}, dataA));
    QVERIFY2(a.ok, qPrintable(a.errorMessage));
    QCOMPARE(int(u16Pixels(a)[0]), 65535);
    QCOMPARE(int(u16Pixels(a)[1]), (2048 * 65535 + 2047) / 4095);

    QByteArray dataB;
    for (const int value : {4095, 2048, 0}) {
      putU16(&dataB, value, true);
    }
    const RawImage b =
        decodeDpx(dpxFile({.width = 1, .height = 1, .bits = 12, .packing = 2}, dataB));
    QVERIFY2(b.ok, qPrintable(b.errorMessage));
    QCOMPARE(int(u16Pixels(b)[0]), 65535);
    QCOMPARE(int(u16Pixels(b)[1]), int(u16Pixels(a)[1]));
  }

  // RGBA、ABGR (並びを RGBA へ直す)、輝度。
  void dpxDecodesDescriptors() {
    QByteArray rgba;
    append(&rgba, {10, 20, 30, 40});
    const RawImage rgbaImage =
        decodeDpx(dpxFile({.width = 1, .height = 1, .descriptor = 51, .bits = 8}, rgba));
    QVERIFY(rgbaImage.ok);
    QCOMPARE(rgbaImage.channels, 4);
    QCOMPARE(int(u8Pixels(rgbaImage)[0]), 10);
    QCOMPARE(int(u8Pixels(rgbaImage)[3]), 40);

    QByteArray abgr;
    append(&abgr, {40, 30, 20, 10});  // A、B、G、R
    const RawImage abgrImage =
        decodeDpx(dpxFile({.width = 1, .height = 1, .descriptor = 52, .bits = 8}, abgr));
    QVERIFY(abgrImage.ok);
    QCOMPARE(int(u8Pixels(abgrImage)[0]), 10);  // R
    QCOMPARE(int(u8Pixels(abgrImage)[1]), 20);  // G
    QCOMPARE(int(u8Pixels(abgrImage)[2]), 30);  // B
    QCOMPARE(int(u8Pixels(abgrImage)[3]), 40);  // A

    QByteArray luma;
    append(&luma, {77, 99});
    const RawImage lumaImage =
        decodeDpx(dpxFile({.width = 2, .height = 1, .descriptor = 6, .bits = 8}, luma));
    QVERIFY(lumaImage.ok);
    QCOMPARE(lumaImage.channels, 1);
    QCOMPARE(int(u8Pixels(lumaImage)[1]), 99);
  }

  // 行末の余白と、向きの指定。
  void dpxHonorsLinePaddingAndOrientation() {
    QByteArray data;
    append(&data, {1, 1, 1, 0, 0, 0, 0, 0});  // 1 行目: 画素 (1,1,1) + 余白 5 バイト
    append(&data, {2, 2, 2, 0, 0, 0, 0, 0});  // 2 行目
    const RawImage padded =
        decodeDpx(dpxFile({.width = 1, .height = 2, .bits = 8, .endOfLinePadding = 5}, data));
    QVERIFY2(padded.ok, qPrintable(padded.errorMessage));
    QCOMPARE(int(u8Pixels(padded)[0]), 1);
    QCOMPARE(int(u8Pixels(padded)[3]), 2);

    // 向き 2 は、下から上。
    const RawImage flipped = decodeDpx(dpxFile(
        {.width = 1, .height = 2, .bits = 8, .orientation = 2, .endOfLinePadding = 5}, data));
    QVERIFY(flipped.ok);
    QCOMPARE(int(u8Pixels(flipped)[0]), 2);
    QCOMPARE(int(u8Pixels(flipped)[3]), 1);

    // 向き 1 は、右から左。
    QByteArray row;
    append(&row, {1, 1, 1, 2, 2, 2});
    const RawImage mirrored =
        decodeDpx(dpxFile({.width = 2, .height = 1, .bits = 8, .orientation = 1}, row));
    QVERIFY(mirrored.ok);
    QCOMPARE(int(u8Pixels(mirrored)[0]), 2);
    QCOMPARE(int(u8Pixels(mirrored)[3]), 1);
  }

  void dpxRejectsUnsupportedOrBrokenFiles() {
    QByteArray data(16, '\1');
    QVERIFY(!decodeDpx(QByteArray(10, '\0')).ok);
    QVERIFY(!decodeDpx(QByteArray(900, '\0')).ok);  // 印が違う
    QVERIFY(!decodeDpx(dpxFile({.encoding = 1}, data)).ok);
    QVERIFY(!decodeDpx(dpxFile({.dataSign = 1}, data)).ok);
    QVERIFY(!decodeDpx(dpxFile({.descriptor = 1}, data)).ok);  // 要素が分かれた画像
    QVERIFY(!decodeDpx(dpxFile({.bits = 32}, data)).ok);
    QVERIFY(!decodeDpx(dpxFile({.orientation = 5}, data)).ok);  // 縦横が入れ替わる向き
    // 画素が足りない。
    QVERIFY(!decodeDpx(dpxFile({.width = 100, .height = 100, .bits = 8}, data)).ok);
  }

  // ============================== PSD ==============================

  // 無圧縮の 8bit RGB。平面ごとに、行の順に画素が並ぶ。
  void psdDecodesUncompressedRgb() {
    QByteArray data;
    append(&data, {255, 0});  // R 平面
    append(&data, {0, 255});  // G 平面
    append(&data, {0, 0});    // B 平面
    const RawImage image = decodePsd(psdFile({.width = 2, .height = 1, .channels = 3}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.channels, 3);
    QCOMPARE(image.sampleType, PixelSampleType::UInt8);
    const uchar *pixels = u8Pixels(image);
    QCOMPARE(int(pixels[0]), 255);
    QCOMPARE(int(pixels[1]), 0);
    QCOMPARE(int(pixels[3]), 0);
    QCOMPARE(int(pixels[4]), 255);
  }

  // 4 つ目のチャンネルは、アルファ。
  void psdDecodesTheAlphaChannel() {
    QByteArray data;
    append(&data, {10});
    append(&data, {20});
    append(&data, {30});
    append(&data, {128});
    const RawImage image = decodePsd(psdFile({.width = 1, .height = 1, .channels = 4}, data));
    QVERIFY(image.ok);
    QCOMPARE(image.channels, 4);
    QCOMPARE(int(u8Pixels(image)[3]), 128);
  }

  // RLE (PackBits)。先に行ごとの圧縮後のバイト数が並ぶ。
  void psdDecodesRle() {
    const int width = 6;
    const int height = 2;
    QByteArray counts;
    QByteArray packed;
    for (int channel = 0; channel < 3; ++channel) {
      for (int row = 0; row < height; ++row) {
        // 行ごとに、値を変える (繰り返しと、そのままの並びが両方出るように)。
        QByteArray plain;
        for (int column = 0; column < width; ++column) {
          plain.append(static_cast<char>(column < 4 ? 50 + channel * 10 + row : column * 7));
        }
        const QByteArray packedRow = packBitsRow(plain);
        putU16(&counts, packedRow.size(), true);
        packed += packedRow;
      }
    }
    const RawImage image = decodePsd(psdFile(
        {.width = width, .height = height, .channels = 3, .compression = 1}, counts + packed));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    const uchar *pixels = u8Pixels(image);
    // 1 行目の先頭の画素: R=50、G=60、B=70。2 行目は +1。
    QCOMPARE(int(pixels[0]), 50);
    QCOMPARE(int(pixels[1]), 60);
    QCOMPARE(int(pixels[2]), 70);
    QCOMPARE(int(pixels[width * 3 + 0]), 51);
    // 5 列目は、そのままの並びの値 (column * 7)。
    QCOMPARE(int(pixels[5 * 3]), 35);
  }

  // 16bit はビッグエンディアン。ホストの並びへ直し、精度を落とさない。
  void psdDecodes16Bit() {
    QByteArray data;
    for (const int value : {0xFFFF, 0x1234, 0x0001}) {
      putU16(&data, value, true);
    }
    const RawImage image =
        decodePsd(psdFile({.width = 1, .height = 1, .channels = 3, .depth = 16}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.sampleType, PixelSampleType::UInt16);
    QCOMPARE(int(u16Pixels(image)[0]), 0xFFFF);
    QCOMPARE(int(u16Pixels(image)[1]), 0x1234);
    QCOMPARE(int(u16Pixels(image)[2]), 0x0001);
  }

  // 32bit は浮動小数 (ビッグエンディアン)。1.0 を超える値を保つ。
  void psdDecodes32BitFloat() {
    QByteArray data;
    for (const float value : {1.5F, 0.25F, 4.0F}) {
      quint32 bits = 0;
      std::memcpy(&bits, &value, sizeof(bits));
      putU32(&data, bits, true);
    }
    const RawImage image =
        decodePsd(psdFile({.width = 1, .height = 1, .channels = 3, .depth = 32}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.sampleType, PixelSampleType::Float32);
    QCOMPARE(floatPixels(image)[0], 1.5F);
    QCOMPARE(floatPixels(image)[1], 0.25F);
    QCOMPARE(floatPixels(image)[2], 4.0F);
  }

  void psdDecodesGrayscale() {
    QByteArray data;
    append(&data, {10, 200});
    const RawImage image =
        decodePsd(psdFile({.width = 2, .height = 1, .channels = 1, .colorMode = 1}, data));
    QVERIFY(image.ok);
    QCOMPARE(image.channels, 1);
    QCOMPARE(int(u8Pixels(image)[1]), 200);
  }

  // 画像リソースから、埋め込みの ICC プロファイルを取り出す。
  void psdExtractsTheIccProfile() {
    QByteArray resource("8BIM");
    putU16(&resource, 1005, true);  // 別のリソース (解像度情報)。
    append(&resource, {0, 0});      // 名前 (空のパスカル文字列を偶数へ詰めたもの)
    putU32(&resource, 3, true);
    append(&resource, {1, 2, 3, 0});  // データ 3 バイト + 偶数への詰め
    resource += "8BIM";
    putU16(&resource, 1039, true);  // ICC
    append(&resource, {0, 0});
    putU32(&resource, 4, true);
    resource += QByteArray("ICC!");

    QByteArray data;
    append(&data, {1, 2, 3});
    const RawImage image = decodePsd(
        psdFile({.width = 1, .height = 1, .channels = 3, .imageResources = resource}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.iccProfile, QByteArray("ICC!"));
  }

  // CMYK は、反転して持たれたインクの量 (255 がインク無し)。RGB = 反転した C / M / Y に K
  // を掛ける。 平面は C M Y K の順。ICC プロファイルは CMYK 用のため、出力へは付けない。
  void psdConvertsCmykToRgb() {
    // 2 画素。(1) インク無し → 白。(2) C だけ 100% → 赤は 0、緑と青は 255。
    QByteArray data;
    append(&data, {255, 0});    // C
    append(&data, {255, 255});  // M
    append(&data, {255, 255});  // Y
    append(&data, {255, 255});  // K
    const RawImage image =
        decodePsd(psdFile({.width = 2, .height = 1, .channels = 4, .colorMode = 4}, data));
    QVERIFY2(image.ok, qPrintable(image.errorMessage));
    QCOMPARE(image.channels, 3);
    const uchar *pixels = u8Pixels(image);
    QCOMPARE(int(pixels[0]), 255);
    QCOMPARE(int(pixels[1]), 255);
    QCOMPARE(int(pixels[2]), 255);
    QCOMPARE(int(pixels[3]), 0);
    QCOMPARE(int(pixels[4]), 255);
    QCOMPARE(int(pixels[5]), 255);

    // K が 50% (反転で 128) なら、そのぶん暗くなる。インクが 0 のところに掛かる。
    QByteArray darker;
    append(&darker, {255});
    append(&darker, {255});
    append(&darker, {255});
    append(&darker, {128});
    const RawImage gray =
        decodePsd(psdFile({.width = 1, .height = 1, .channels = 4, .colorMode = 4}, darker));
    QVERIFY(gray.ok);
    QCOMPARE(int(u8Pixels(gray)[0]), 128);
  }

  // CMYK + アルファ (5 チャンネル) と、16bit。アルファはそのまま通る。
  void psdDecodesCmykWithAlphaAndSixteenBits() {
    QByteArray eight;
    append(&eight, {255});
    append(&eight, {255});
    append(&eight, {255});
    append(&eight, {255});
    append(&eight, {77});  // アルファ
    const RawImage withAlpha =
        decodePsd(psdFile({.width = 1, .height = 1, .channels = 5, .colorMode = 4}, eight));
    QVERIFY2(withAlpha.ok, qPrintable(withAlpha.errorMessage));
    QCOMPARE(withAlpha.channels, 4);
    QCOMPARE(int(u8Pixels(withAlpha)[3]), 77);

    QByteArray wide;
    for (const quint16 value : {quint16(65535), quint16(65535), quint16(0), quint16(65535)}) {
      putU16(&wide, value, true);  // C M Y K。Y が 100%。
    }
    const RawImage sixteen = decodePsd(
        psdFile({.width = 1, .height = 1, .channels = 4, .depth = 16, .colorMode = 4}, wide));
    QVERIFY2(sixteen.ok, qPrintable(sixteen.errorMessage));
    QCOMPARE(sixteen.sampleType, PixelSampleType::UInt16);
    quint16 blue = 0;
    std::memcpy(&blue, sixteen.pixels.constData() + 4, 2);
    QCOMPARE(int(blue), 0);
    quint16 red = 0;
    std::memcpy(&red, sixteen.pixels.constData(), 2);
    QCOMPARE(int(red), 65535);
  }

  // CMYK 用の ICC プロファイルがあれば、色を管理して変換する (素朴な式とは、色が違う)。
  // 出力は sRGB のため、CMYK のプロファイルは付けない。壊れたプロファイルは、素朴な式へ退避する。
  // 本物のプロファイルは Windows の標準のもの (無ければ飛ばす)。
  void psdUsesTheCmykProfile() {
    QByteArray data;
    append(&data, {200});  // C (反転: インクは少し)
    append(&data, {100});  // M
    append(&data, {100});  // Y
    append(&data, {200});  // K
    const auto build = [&](const QByteArray &profile) {
      QByteArray resource("8BIM");
      putU16(&resource, 1039, true);
      append(&resource, {0, 0});
      putU32(&resource, profile.size(), true);
      resource += profile;
      if (profile.size() % 2 != 0) {
        resource.append(char(0));
      }
      return decodePsd(psdFile(
          {.width = 1, .height = 1, .channels = 4, .colorMode = 4, .imageResources = resource},
          data));
    };

    const RawImage naive = build(QByteArray("not a profile!"));
    QVERIFY2(naive.ok, qPrintable(naive.errorMessage));
    QVERIFY(naive.iccProfile.isEmpty());

    QFile file(QStringLiteral("C:/Windows/System32/spool/drivers/color/RSWOP.icm"));
    if (!file.open(QIODevice::ReadOnly)) {
      QSKIP("The Windows CMYK profile (RSWOP.icm) is not available");
    }
    const RawImage managed = build(file.readAll());
    QVERIFY2(managed.ok, qPrintable(managed.errorMessage));
    QCOMPARE(managed.channels, 3);
    QVERIFY(managed.iccProfile.isEmpty());
    int largestDifference = 0;
    for (int channel = 0; channel < 3; ++channel) {
      largestDifference = std::max(largestDifference, std::abs(int(u8Pixels(managed)[channel]) -
                                                               int(u8Pixels(naive)[channel])));
    }
    QVERIFY2(largestDifference > 8, "the profile did not change the colours");
  }

  void psdRejectsUnsupportedOrBrokenFiles() {
    QByteArray data(8, '\1');
    QVERIFY(!decodePsd(QByteArray(10, '\0')).ok);
    // CMYK でインクの平面が足りない。
    QVERIFY(!decodePsd(psdFile({.width = 2, .height = 1, .channels = 3, .colorMode = 4}, data)).ok);
    // 32bit の CMYK は無い。
    QVERIFY(
        !decodePsd(psdFile({.channels = 4, .depth = 32, .colorMode = 4}, QByteArray(32, ''))).ok);
    QVERIFY(!decodePsd(psdFile({.version = 2}, data)).ok);  // PSB
    QVERIFY(!decodePsd(psdFile({.depth = 1}, data)).ok);
    QVERIFY(!decodePsd(psdFile({.compression = 2}, data)).ok);             // ZIP
    QVERIFY(!decodePsd(psdFile({.width = 100, .height = 100}, data)).ok);  // 画素が足りない
    QVERIFY(!decodePsd(psdFile({.channels = 2}, data)).ok);                // RGB でチャンネル不足
    // RLE の行の表が切れている。
    QVERIFY(
        !decodePsd(psdFile({.width = 4, .height = 4, .compression = 1}, QByteArray(3, '\0'))).ok);
  }

  // ============================ 復号の経路 ============================

  void decoderChoosesTheNativeDecoderByExtension() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto write = [&directory](const QString &name, const QByteArray &bytes) {
      const QString path = QDir(directory.path()).filePath(name);
      QFile file(path);
      file.open(QIODevice::WriteOnly);
      file.write(bytes);
      return path;
    };

    QByteArray tga = tgaHeader(2, 2, 1, 24, 0x20);
    append(&tga, {0, 0, 255, 0, 255, 0});
    const auto tgaResult = ImageDecoder().decode(write("a.tga", tga), true);
    QVERIFY2(tgaResult.ok, qPrintable(tgaResult.errorMessage));
    QCOMPARE(tgaResult.decodedImage.decoderBackend, QString("tga"));
    QCOMPARE(tgaResult.decodedImage.size(), QSize(2, 1));
    QCOMPARE(tgaResult.decodedImage.sampleRgba(0, 0, 0), 1.0F);

    QByteArray hdr = hdrHeader(1, 1);
    append(&hdr, {200, 100, 50, 136});
    const auto hdrResult = ImageDecoder().decode(write("a.hdr", hdr), true);
    QVERIFY2(hdrResult.ok, qPrintable(hdrResult.errorMessage));
    QCOMPARE(hdrResult.decodedImage.decoderBackend, QString("hdr"));
    QCOMPARE(hdrResult.decodedImage.sourceSampleType, PixelSampleType::Float32);
    // 1.0 を超える値を、解析用の値として保つ。
    QCOMPARE(hdrResult.decodedImage.sampleRgba(0, 0, 0), 200.0F);

    QByteArray dpxData;
    append(&dpxData, {255, 0, 0});
    const auto dpxResult =
        ImageDecoder().decode(write("a.dpx", dpxFile({.width = 1, .height = 1}, dpxData)), true);
    QVERIFY2(dpxResult.ok, qPrintable(dpxResult.errorMessage));
    QCOMPARE(dpxResult.decodedImage.decoderBackend, QString("dpx"));
    QCOMPARE(dpxResult.decodedImage.sampleRgba(0, 0, 0), 1.0F);

    // 壊れたファイルは、落ちずに失敗を返す。
    QVERIFY(!ImageDecoder().decode(write("broken.tga", QByteArray(30, 'x')), true).ok);
    QVERIFY(!ImageDecoder().decode(write("broken.psd", QByteArray(30, 'x')), true).ok);
  }

  // 日本語のパスでも読めること。
  void decodesFromANonAsciiPath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QString::fromUtf8("画像 テスト.tga"));
    QByteArray tga = tgaHeader(2, 1, 1, 24, 0x20);
    append(&tga, {0, 0, 255});
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(tga);
    file.close();

    QVERIFY(ImageDecoder().decode(path, true).ok);
  }
};

QTEST_MAIN(NativeFormatTests)

#include "NativeFormatTests.moc"
