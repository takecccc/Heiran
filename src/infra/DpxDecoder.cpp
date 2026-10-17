#include "heiran/infra/DpxDecoder.hpp"

#include <algorithm>

namespace heiran::infra {

namespace {

constexpr quint32 kMagicBigEndian = 0x53445058;     // "SDPX"
constexpr quint32 kMagicLittleEndian = 0x58504453;  // "XPDS"
constexpr qsizetype kMinimumHeaderBytes = 820;

enum class Packing {
  Packed = 0,
  FilledMethodA = 1,
  FilledMethodB = 2,
};

class Reader {
public:
  Reader(const QByteArray &bytes, bool bigEndian)
      : data_(reinterpret_cast<const uchar *>(bytes.constData())), size_(bytes.size()),
        bigEndian_(bigEndian) {}

  quint16 u16(qsizetype offset) const {
    const uchar *bytes = data_ + offset;
    return bigEndian_ ? static_cast<quint16>((bytes[0] << 8) | bytes[1])
                      : static_cast<quint16>(bytes[0] | (bytes[1] << 8));
  }

  quint32 u32(qsizetype offset) const {
    const uchar *bytes = data_ + offset;
    return bigEndian_
               ? (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) |
                     (static_cast<quint32>(bytes[2]) << 8) | bytes[3]
               : (static_cast<quint32>(bytes[3]) << 24) | (static_cast<quint32>(bytes[2]) << 16) |
                     (static_cast<quint32>(bytes[1]) << 8) | bytes[0];
  }

  quint8 u8(qsizetype offset) const { return data_[offset]; }
  qsizetype size() const { return size_; }

private:
  const uchar *data_;
  qsizetype size_;
  bool bigEndian_;
};

// 16bit の全範囲へ引き伸ばす。
quint16 scaleTo16(quint32 value, int bits) {
  const quint32 maximum = (1u << bits) - 1;
  return static_cast<quint16>((value * 65535u + maximum / 2) / maximum);
}

}  // namespace

RawImage decodeDpx(const QByteArray &fileContents) {
  if (fileContents.size() < kMinimumHeaderBytes) {
    return rawImageFailure(QStringLiteral("The DPX header is truncated"));
  }
  const auto *head = reinterpret_cast<const uchar *>(fileContents.constData());
  const quint32 magicBig = (static_cast<quint32>(head[0]) << 24) |
                           (static_cast<quint32>(head[1]) << 16) |
                           (static_cast<quint32>(head[2]) << 8) | head[3];
  bool bigEndian = true;
  if (magicBig == kMagicBigEndian) {
    bigEndian = true;
  } else if (magicBig == kMagicLittleEndian) {
    bigEndian = false;
  } else {
    return rawImageFailure(QStringLiteral("Not a DPX file"));
  }
  const Reader reader(fileContents, bigEndian);

  const quint16 orientation = reader.u16(768);
  const quint32 width = reader.u32(772);
  const quint32 height = reader.u32(776);
  const quint32 dataSign = reader.u32(780);
  const quint8 descriptor = reader.u8(800);
  const quint8 bits = reader.u8(803);
  const quint16 packing = reader.u16(804);
  const quint16 encoding = reader.u16(806);
  const quint32 dataOffset = reader.u32(808);
  const quint32 endOfLinePadding = reader.u32(812);

  if (width == 0 || height == 0 || static_cast<qint64>(width) * height > kMaxRawImagePixels) {
    return rawImageFailure(QStringLiteral("The image size is invalid"));
  }
  if (dataSign != 0) {
    return rawImageFailure(QStringLiteral("Signed DPX data is not supported"));
  }
  if (encoding != 0) {
    return rawImageFailure(QStringLiteral("RLE DPX data is not supported"));
  }
  if (orientation > 3) {
    return rawImageFailure(QStringLiteral("This DPX orientation is not supported"));
  }

  int channels = 0;
  switch (descriptor) {
    case 50:  // RGB
      channels = 3;
      break;
    case 51:  // RGBA
    case 52:  // ABGR
      channels = 4;
      break;
    case 6:  // 輝度
      channels = 1;
      break;
    default:
      return rawImageFailure(QStringLiteral("This DPX element layout is not supported"));
  }
  if (bits != 8 && bits != 10 && bits != 12 && bits != 16) {
    return rawImageFailure(QStringLiteral("This DPX bit depth is not supported"));
  }
  if (packing > static_cast<quint16>(Packing::FilledMethodB)) {
    return rawImageFailure(QStringLiteral("This DPX packing is not supported"));
  }

  // 1 行の標本の数と、1 行のバイト数。
  const qint64 samplesPerLine = static_cast<qint64>(width) * channels;
  qint64 lineBytes = 0;
  if (bits == 8) {
    lineBytes = samplesPerLine;
  } else if (bits == 16) {
    lineBytes = samplesPerLine * 2;
  } else if (packing == static_cast<quint16>(Packing::Packed)) {
    // すき間なく詰める。行の終わりは 32bit の境へ。
    lineBytes = ((samplesPerLine * bits + 31) / 32) * 4;
  } else if (bits == 10) {
    lineBytes = ((samplesPerLine + 2) / 3) * 4;  // 1 語に 3 標本。
  } else {
    lineBytes = samplesPerLine * 2;  // 12bit は、16bit に 1 標本。
  }
  const qint64 stride = lineBytes + endOfLinePadding;
  if (static_cast<qint64>(dataOffset) + stride * height - endOfLinePadding > reader.size()) {
    return rawImageFailure(QStringLiteral("The pixel data is truncated"));
  }

  RawImage image;
  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.channels = channels;
  image.sampleType = bits == 8 ? PixelSampleType::UInt8 : PixelSampleType::UInt16;
  image.pixels.resize(static_cast<qsizetype>(samplesPerLine) * height * (bits == 8 ? 1 : 2));
  auto *output8 = reinterpret_cast<uchar *>(image.pixels.data());
  auto *output16 = reinterpret_cast<quint16 *>(image.pixels.data());

  const bool flipHorizontally = orientation == 1 || orientation == 3;
  const bool flipVertically = orientation == 2 || orientation == 3;

  // 行の中の n 番目の標本 (0 から) を取り出す。
  const auto sampleAt = [&](qsizetype lineStart, qint64 index) -> quint32 {
    switch (bits) {
      case 8:
        return reader.u8(lineStart + index);
      case 16:
        return reader.u16(lineStart + index * 2);
      default:
        break;
    }
    if (packing == static_cast<quint16>(Packing::Packed)) {
      // 語の下位のビットから詰めた、ビットの並び。標本が 2 語にまたがる
      // ことがある。
      const qint64 bitPosition = index * bits;
      const qint64 wordIndex = bitPosition / 32;
      const int bitInWord = static_cast<int>(bitPosition % 32);
      const quint64 low = reader.u32(lineStart + wordIndex * 4);
      const quint64 high =
          (bitInWord + bits > 32) ? reader.u32(lineStart + (wordIndex + 1) * 4) : 0;
      const quint64 pair = (high << 32) | low;
      return static_cast<quint32>((pair >> bitInWord) & ((1u << bits) - 1));
    }
    if (bits == 10) {
      // 1 語に 3 標本。A は上に詰めて下に 2 ビットの余り、B は上に 2 ビットの余り。
      const quint32 word = reader.u32(lineStart + (index / 3) * 4);
      const int slot = static_cast<int>(index % 3);
      const int shift =
          packing == static_cast<quint16>(Packing::FilledMethodA) ? 22 - slot * 10 : 20 - slot * 10;
      return (word >> shift) & 0x3FF;
    }
    // 12bit。16bit の語の上 (A) か下 (B) に置く。
    const quint32 word = reader.u16(lineStart + index * 2);
    return packing == static_cast<quint16>(Packing::FilledMethodA) ? (word >> 4) & 0xFFF
                                                                   : word & 0xFFF;
  };

  for (quint32 row = 0; row < height; ++row) {
    const qsizetype lineStart = static_cast<qsizetype>(dataOffset + stride * row);
    const quint32 destinationRow = flipVertically ? height - 1 - row : row;
    for (quint32 column = 0; column < width; ++column) {
      const quint32 destinationColumn = flipHorizontally ? width - 1 - column : column;
      const qsizetype destination =
          (static_cast<qsizetype>(destinationRow) * width + destinationColumn) * channels;
      for (int channel = 0; channel < channels; ++channel) {
        const quint32 value = sampleAt(lineStart, static_cast<qint64>(column) * channels + channel);
        // ABGR は、RGBA の並びへ入れ替える。
        const int outputChannel = descriptor == 52 ? (channel == 0 ? 3 : 3 - channel) : channel;
        if (bits == 8) {
          output8[destination + outputChannel] = static_cast<uchar>(value);
        } else {
          output16[destination + outputChannel] =
              bits == 16 ? static_cast<quint16>(value) : scaleTo16(value, bits);
        }
      }
    }
  }

  image.ok = true;
  return image;
}

}  // namespace heiran::infra
