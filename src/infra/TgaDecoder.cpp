#include "heiran/infra/TgaDecoder.hpp"

#include <algorithm>
#include <vector>

namespace heiran::infra {

namespace {

constexpr int kHeaderBytes = 18;

quint16 readU16(const uchar *bytes) {
  return static_cast<quint16>(bytes[0] | (bytes[1] << 8));
}

// パレットの 1 つ、または 1 画素の色。アルファが無ければ 255。
struct Color {
  uchar red = 0;
  uchar green = 0;
  uchar blue = 0;
  uchar alpha = 255;
};

// 5bit を 8bit へ。上の 3 ビットを下へ繰り返して、31 が 255 になるようにする。
uchar expand5(int value) {
  return static_cast<uchar>((value << 3) | (value >> 2));
}

// 1 画素分 (`bytesPerPixel` バイト) の色を読む。TGA は青、緑、赤、(アルファ) の順。
Color readColor(const uchar *bytes, int bitsPerPixel, bool useAlpha) {
  Color color;
  switch (bitsPerPixel) {
    case 15:
    case 16: {
      // A1R5G5B5。
      const int value = readU16(bytes);
      color.red = expand5((value >> 10) & 0x1F);
      color.green = expand5((value >> 5) & 0x1F);
      color.blue = expand5(value & 0x1F);
      color.alpha = useAlpha ? ((value & 0x8000) != 0 ? 255 : 0) : 255;
      break;
    }
    case 24:
      color.blue = bytes[0];
      color.green = bytes[1];
      color.red = bytes[2];
      break;
    case 32:
      color.blue = bytes[0];
      color.green = bytes[1];
      color.red = bytes[2];
      color.alpha = useAlpha ? bytes[3] : 255;
      break;
    default:
      break;
  }
  return color;
}

}  // namespace

RawImage decodeTga(const QByteArray &fileContents) {
  if (fileContents.size() < kHeaderBytes) {
    return rawImageFailure(QStringLiteral("The TGA header is truncated"));
  }
  const auto *header = reinterpret_cast<const uchar *>(fileContents.constData());
  const int idLength = header[0];
  const int colorMapType = header[1];
  const int imageType = header[2];
  const int colorMapFirst = readU16(header + 3);
  const int colorMapLength = readU16(header + 5);
  const int colorMapEntryBits = header[7];
  const int width = readU16(header + 12);
  const int height = readU16(header + 14);
  const int bitsPerPixel = header[16];
  const int descriptor = header[17];

  const bool rle = imageType >= 9;
  const int baseType = rle ? imageType - 8 : imageType;
  if (baseType != 1 && baseType != 2 && baseType != 3) {
    return rawImageFailure(QStringLiteral("This TGA image type is not supported"));
  }
  if (width <= 0 || height <= 0 || static_cast<qint64>(width) * height > kMaxRawImagePixels) {
    return rawImageFailure(QStringLiteral("The image size is invalid"));
  }

  const int alphaBits = descriptor & 0x0F;
  const bool topToBottom = (descriptor & 0x20) != 0;
  const bool rightToLeft = (descriptor & 0x10) != 0;

  // 画素の読み方を決める。
  const bool mapped = baseType == 1;
  const bool gray = baseType == 3;
  if (mapped && (bitsPerPixel != 8 || colorMapType != 1)) {
    return rawImageFailure(QStringLiteral("Unsupported color-mapped TGA"));
  }
  if (gray && bitsPerPixel != 8 && bitsPerPixel != 16) {
    return rawImageFailure(QStringLiteral("Unsupported gray TGA depth"));
  }
  if (baseType == 2 && bitsPerPixel != 15 && bitsPerPixel != 16 && bitsPerPixel != 24 &&
      bitsPerPixel != 32) {
    return rawImageFailure(QStringLiteral("Unsupported true-color TGA depth"));
  }

  qsizetype position = kHeaderBytes + idLength;

  // パレット。
  std::vector<Color> palette;
  bool paletteHasAlpha = false;
  if (colorMapType == 1) {
    const int entryBytes = (colorMapEntryBits + 7) / 8;
    if (colorMapEntryBits != 15 && colorMapEntryBits != 16 && colorMapEntryBits != 24 &&
        colorMapEntryBits != 32) {
      return rawImageFailure(QStringLiteral("Unsupported color map entry size"));
    }
    const qsizetype paletteBytes = static_cast<qsizetype>(colorMapLength) * entryBytes;
    if (position + paletteBytes > fileContents.size()) {
      return rawImageFailure(QStringLiteral("The color map is truncated"));
    }
    paletteHasAlpha = colorMapEntryBits == 32 && alphaBits > 0;
    palette.resize(static_cast<size_t>(colorMapFirst) + colorMapLength);
    for (int index = 0; index < colorMapLength; ++index) {
      palette[static_cast<size_t>(colorMapFirst) + index] =
          readColor(header + position + static_cast<qsizetype>(index) * entryBytes,
                    colorMapEntryBits, paletteHasAlpha);
    }
    position += paletteBytes;
  }

  // 出力の形。
  const bool useAlpha =
      mapped ? paletteHasAlpha
             : (gray ? bitsPerPixel == 16
                     : ((bitsPerPixel == 32 || bitsPerPixel == 16 || bitsPerPixel == 15) &&
                        alphaBits > 0));
  const int channels = gray ? (useAlpha ? 2 : 1) : (useAlpha ? 4 : 3);
  const int bytesPerPixel = (bitsPerPixel + 7) / 8;

  RawImage image;
  image.width = width;
  image.height = height;
  image.channels = channels;
  image.sampleType = PixelSampleType::UInt8;
  image.pixels.resize(static_cast<qsizetype>(width) * height * channels);
  auto *output = reinterpret_cast<uchar *>(image.pixels.data());

  const auto *data = reinterpret_cast<const uchar *>(fileContents.constData());
  const qsizetype dataSize = fileContents.size();

  // 1 画素を書く。`index` は、ファイルの順で何番目の画素か。
  const auto store = [&](qint64 index, const uchar *source) {
    const int fileRow = static_cast<int>(index / width);
    const int fileColumn = static_cast<int>(index % width);
    const int row = topToBottom ? fileRow : height - 1 - fileRow;
    const int column = rightToLeft ? width - 1 - fileColumn : fileColumn;
    uchar *target = output + (static_cast<qsizetype>(row) * width + column) * channels;
    if (gray) {
      target[0] = source[0];
      if (useAlpha) {
        target[1] = source[1];
      }
    } else if (mapped) {
      const size_t entry = source[0];
      const Color color = entry < palette.size() ? palette[entry] : Color{};
      target[0] = color.red;
      target[1] = color.green;
      target[2] = color.blue;
      if (useAlpha) {
        target[3] = color.alpha;
      }
    } else {
      const Color color = readColor(source, bitsPerPixel, useAlpha);
      target[0] = color.red;
      target[1] = color.green;
      target[2] = color.blue;
      if (useAlpha) {
        target[3] = color.alpha;
      }
    }
  };

  const qint64 pixelCount = static_cast<qint64>(width) * height;
  qint64 written = 0;
  if (!rle) {
    if (position + pixelCount * bytesPerPixel > dataSize) {
      return rawImageFailure(QStringLiteral("The pixel data is truncated"));
    }
    for (; written < pixelCount; ++written) {
      store(written, data + position + written * bytesPerPixel);
    }
  } else {
    // RLE。先頭の 1 バイトの上位 1 ビットが 1 なら、次の 1 画素を (下位 7 ビット + 1) 回繰り返す。
    // 0 なら、(下位 7 ビット + 1) 個の画素がそのまま続く。行をまたぐことがある。
    while (written < pixelCount) {
      if (position >= dataSize) {
        return rawImageFailure(QStringLiteral("The pixel data is truncated"));
      }
      const int packet = data[position++];
      const int count = (packet & 0x7F) + 1;
      if (packet & 0x80) {
        if (position + bytesPerPixel > dataSize) {
          return rawImageFailure(QStringLiteral("The pixel data is truncated"));
        }
        for (int repeat = 0; repeat < count && written < pixelCount; ++repeat) {
          store(written++, data + position);
        }
        position += bytesPerPixel;
      } else {
        if (position + static_cast<qsizetype>(count) * bytesPerPixel > dataSize) {
          return rawImageFailure(QStringLiteral("The pixel data is truncated"));
        }
        for (int index = 0; index < count && written < pixelCount; ++index) {
          store(written++, data + position);
          position += bytesPerPixel;
        }
      }
    }
  }

  image.ok = true;
  return image;
}

}  // namespace heiran::infra
