#pragma once

// GIF と TIFF のフィクスチャを、テストの中でバイト列から組み立てる。
// 外部のツールに頼らず、ページ数、フレーム数、表示時間、色を自由に決められる。

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

namespace heiran::test {

void appendU16(QByteArray *bytes, quint16 value) {
  bytes->append(static_cast<char>(value & 0xFF));
  bytes->append(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(QByteArray *bytes, quint32 value) {
  appendU16(bytes, static_cast<quint16>(value & 0xFFFF));
  appendU16(bytes, static_cast<quint16>((value >> 16) & 0xFFFF));
}

// GIF の LZW。最小符号長 2 (色 4 つ) の 3 ビット符号だけで組む。
// 画素 2 つごとに clear を入れると、辞書が 3 ビットに収まるうちに作り直される。
// 圧縮はしないが、正しい GIF になる。
QByteArray gifLzw(const QVector<quint8> &indices) {
  constexpr int kClear = 4;
  constexpr int kEnd = 5;

  QByteArray packed;
  quint32 bitBuffer = 0;
  int bitCount = 0;
  const auto emit = [&](int code) {
    bitBuffer |= static_cast<quint32>(code) << bitCount;
    bitCount += 3;
    while (bitCount >= 8) {
      packed.append(static_cast<char>(bitBuffer & 0xFF));
      bitBuffer >>= 8;
      bitCount -= 8;
    }
  };

  for (int index = 0; index < indices.size(); ++index) {
    if (index % 2 == 0) {
      emit(kClear);
    }
    emit(indices.at(index));
  }
  emit(kEnd);
  if (bitCount > 0) {
    packed.append(static_cast<char>(bitBuffer & 0xFF));
  }

  QByteArray blocks;
  for (qsizetype offset = 0; offset < packed.size(); offset += 255) {
    const QByteArray chunk = packed.mid(offset, 255);
    blocks.append(static_cast<char>(chunk.size()));
    blocks.append(chunk);
  }
  blocks.append('\0');
  return blocks;
}

// 色番号の一様なフレームを並べた GIF。パレットは 0 赤、1 緑、2 青、3 白。
// delaysCs は 1/100 秒。
QByteArray makeGif(int width, int height, const QVector<int> &frameColorIndices,
                   const QVector<int> &delaysCs) {
  QByteArray gif("GIF89a");
  appendU16(&gif, static_cast<quint16>(width));
  appendU16(&gif, static_cast<quint16>(height));
  gif.append(static_cast<char>(0x81));  // 色表あり、4 色
  gif.append('\0');
  gif.append('\0');
  const quint8 palette[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  gif.append(reinterpret_cast<const char *>(palette), sizeof(palette));

  for (int frame = 0; frame < frameColorIndices.size(); ++frame) {
    // 表示時間と、次を描く前に消さない指定 (disposal 1)。
    gif.append(static_cast<char>(0x21));
    gif.append(static_cast<char>(0xF9));
    gif.append(static_cast<char>(0x04));
    gif.append(static_cast<char>(0x04));
    appendU16(&gif, static_cast<quint16>(delaysCs.value(frame)));
    gif.append('\0');
    gif.append('\0');

    gif.append(static_cast<char>(0x2C));
    appendU16(&gif, 0);
    appendU16(&gif, 0);
    appendU16(&gif, static_cast<quint16>(width));
    appendU16(&gif, static_cast<quint16>(height));
    gif.append('\0');
    gif.append(static_cast<char>(2));  // 最小符号長
    gif.append(
        gifLzw(QVector<quint8>(width * height, static_cast<quint8>(frameColorIndices.at(frame)))));
  }
  gif.append(static_cast<char>(0x3B));
  return gif;
}

struct TiffPage {
  QSize size;
  QColor color;
};

// 非圧縮の 8bit RGB を、ページごとに 1 つの IFD で並べたマルチページ TIFF。
QByteArray makeTiff(const QVector<TiffPage> &pages) {
  constexpr int kEntryCount = 9;
  constexpr int kIfdBytes = 2 + kEntryCount * 12 + 4;

  // 並べ方は、ページごとに 画素、ビット深度の配列、IFD の順。
  QVector<quint32> pixelOffsets;
  QVector<quint32> depthOffsets;
  QVector<quint32> ifdOffsets;
  quint32 cursor = 8;
  for (const auto &page : pages) {
    pixelOffsets.push_back(cursor);
    cursor += static_cast<quint32>(page.size.width() * page.size.height() * 3);
    depthOffsets.push_back(cursor);
    cursor += 6;
    ifdOffsets.push_back(cursor);
    cursor += kIfdBytes;
  }

  QByteArray tiff("II");
  appendU16(&tiff, 42);
  appendU32(&tiff, ifdOffsets.first());

  const auto appendEntry = [&tiff](quint16 tag, quint16 type, quint32 count, quint32 value) {
    appendU16(&tiff, tag);
    appendU16(&tiff, type);
    appendU32(&tiff, count);
    // SHORT は 4 バイトの欄の先頭へ置く。リトルエンディアンでは値をそのまま書けばよい。
    appendU32(&tiff, value);
  };
  constexpr quint16 kShort = 3;
  constexpr quint16 kLong = 4;

  for (int index = 0; index < pages.size(); ++index) {
    const auto &page = pages.at(index);
    const quint32 pixelBytes = static_cast<quint32>(page.size.width() * page.size.height() * 3);
    QByteArray pixels;
    for (int pixel = 0; pixel < page.size.width() * page.size.height(); ++pixel) {
      pixels.append(static_cast<char>(page.color.red()));
      pixels.append(static_cast<char>(page.color.green()));
      pixels.append(static_cast<char>(page.color.blue()));
    }
    tiff.append(pixels);
    appendU16(&tiff, 8);
    appendU16(&tiff, 8);
    appendU16(&tiff, 8);

    appendU16(&tiff, kEntryCount);
    appendEntry(256, kLong, 1, static_cast<quint32>(page.size.width()));
    appendEntry(257, kLong, 1, static_cast<quint32>(page.size.height()));
    appendEntry(258, kShort, 3, depthOffsets.at(index));
    appendEntry(259, kShort, 1, 1);
    appendEntry(262, kShort, 1, 2);
    appendEntry(273, kLong, 1, pixelOffsets.at(index));
    appendEntry(277, kShort, 1, 3);
    appendEntry(278, kLong, 1, static_cast<quint32>(page.size.height()));
    appendEntry(279, kLong, 1, pixelBytes);
    appendU32(&tiff, index + 1 < pages.size() ? ifdOffsets.at(index + 1) : 0);
  }
  return tiff;
}

QString writeFile(const QDir &directory, const QString &name, const QByteArray &bytes) {
  const QString path = directory.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write(bytes);
  return path;
}

// ---- アニメーション PNG ----

inline quint32 pngCrc32(const QByteArray &bytes) {
  quint32 crc = 0xFFFFFFFFU;
  for (const char character : bytes) {
    crc ^= static_cast<quint8>(character);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (0xEDB88320U ^ (crc >> 1)) : (crc >> 1);
    }
  }
  return ~crc;
}

inline void appendBigEndian32(QByteArray *bytes, quint32 value) {
  bytes->append(static_cast<char>((value >> 24) & 0xFF));
  bytes->append(static_cast<char>((value >> 16) & 0xFF));
  bytes->append(static_cast<char>((value >> 8) & 0xFF));
  bytes->append(static_cast<char>(value & 0xFF));
}

inline void appendBigEndian16(QByteArray *bytes, quint16 value) {
  bytes->append(static_cast<char>((value >> 8) & 0xFF));
  bytes->append(static_cast<char>(value & 0xFF));
}

inline QByteArray pngChunk(const char *type, const QByteArray &data) {
  QByteArray chunk;
  appendBigEndian32(&chunk, static_cast<quint32>(data.size()));
  QByteArray typeAndData(type, 4);
  typeAndData.append(data);
  chunk.append(typeAndData);
  appendBigEndian32(&chunk, pngCrc32(typeAndData));
  return chunk;
}

// Qt が書いた PNG から、IDAT の中身だけを取り出す。フレームの画素の圧縮は Qt に任せる。
inline QVector<QByteArray> pngImageData(const QImage &image) {
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");

  QVector<QByteArray> idat;
  qsizetype position = 8;
  while (position + 12 <= png.size()) {
    const auto *p = reinterpret_cast<const quint8 *>(png.constData()) + position;
    const quint32 length = (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
                           (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
    if (png.mid(position + 4, 4) == "IDAT") {
      idat.push_back(png.mid(position + 8, static_cast<qsizetype>(length)));
    }
    position += 12 + static_cast<qsizetype>(length);
  }
  return idat;
}

struct ApngFixtureFrame {
  QImage image;
  int x = 0;
  int y = 0;
  quint16 delayNumerator = 1;
  quint16 delayDenominator = 10;
  quint8 disposeOp = 0;  // 0 なし、1 背景、2 前の状態
  quint8 blendOp = 0;    // 0 置き換え、1 重ねる
};

// アニメーション PNG を組み立てる。`firstFrameIsDefaultImage` が偽なら、最初の画像データは
// アニメーションに含まれない代わりの画像 (`defaultImage`) になる。
inline QByteArray makeApng(int canvasWidth, int canvasHeight,
                           const QVector<ApngFixtureFrame> &frames,
                           bool firstFrameIsDefaultImage = true,
                           const QImage &defaultImage = QImage()) {
  QByteArray png("\x89PNG\r\n\x1A\n", 8);

  QByteArray header;
  appendBigEndian32(&header, static_cast<quint32>(canvasWidth));
  appendBigEndian32(&header, static_cast<quint32>(canvasHeight));
  header.append(char(8));  // ビット深度
  header.append(char(6));  // RGBA
  header.append(char(0));
  header.append(char(0));
  header.append(char(0));
  png.append(pngChunk("IHDR", header));

  QByteArray animationControl;
  appendBigEndian32(&animationControl, static_cast<quint32>(frames.size()));
  appendBigEndian32(&animationControl, 0);  // 無限に繰り返す
  png.append(pngChunk("acTL", animationControl));

  quint32 sequence = 0;
  const auto frameControl = [&sequence](const ApngFixtureFrame &frame) {
    QByteArray body;
    appendBigEndian32(&body, sequence++);
    appendBigEndian32(&body, static_cast<quint32>(frame.image.width()));
    appendBigEndian32(&body, static_cast<quint32>(frame.image.height()));
    appendBigEndian32(&body, static_cast<quint32>(frame.x));
    appendBigEndian32(&body, static_cast<quint32>(frame.y));
    appendBigEndian16(&body, frame.delayNumerator);
    appendBigEndian16(&body, frame.delayDenominator);
    body.append(static_cast<char>(frame.disposeOp));
    body.append(static_cast<char>(frame.blendOp));
    return pngChunk("fcTL", body);
  };

  if (!firstFrameIsDefaultImage) {
    for (const QByteArray &data : pngImageData(defaultImage)) {
      png.append(pngChunk("IDAT", data));
    }
  }
  for (int index = 0; index < frames.size(); ++index) {
    const auto &frame = frames.at(index);
    png.append(frameControl(frame));
    for (const QByteArray &data : pngImageData(frame.image)) {
      if (index == 0 && firstFrameIsDefaultImage) {
        png.append(pngChunk("IDAT", data));
      } else {
        QByteArray body;
        appendBigEndian32(&body, sequence++);
        body.append(data);
        png.append(pngChunk("fdAT", body));
      }
    }
  }
  png.append(pngChunk("IEND", QByteArray()));
  return png;
}

}  // namespace heiran::test
