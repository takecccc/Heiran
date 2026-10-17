#include "heiran/infra/PsdDecoder.hpp"

#include "heiran/infra/CmykConversion.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace heiran::infra {

namespace {

constexpr int kColorModeGray = 1;
constexpr int kColorModeRgb = 3;
constexpr int kColorModeCmyk = 4;
constexpr int kImageResourceIccProfile = 1039;

quint16 readU16(const uchar *bytes) {
  return static_cast<quint16>((bytes[0] << 8) | bytes[1]);
}

quint32 readU32(const uchar *bytes) {
  return (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) |
         (static_cast<quint32>(bytes[2]) << 8) | bytes[3];
}

// 画像リソースの並びから、ICC プロファイルを探す。
// 並びは 8BIM、識別子 (2)、パスカル文字列の名前 (偶数へ詰める)、長さ (4)、データ (偶数へ詰める)。
QByteArray findIccProfile(const uchar *resources, qsizetype size) {
  qsizetype position = 0;
  while (position + 12 <= size) {
    if (std::memcmp(resources + position, "8BIM", 4) != 0) {
      break;
    }
    const int identifier = readU16(resources + position + 4);
    const int nameLength = resources[position + 6];
    qsizetype next = position + 7 + nameLength;
    if ((1 + nameLength) % 2 != 0) {
      ++next;  // パスカル文字列は、長さの 1 バイトを含めて偶数へ詰める。
    }
    if (next + 4 > size) {
      break;
    }
    const quint32 dataLength = readU32(resources + next);
    next += 4;
    if (next + dataLength > static_cast<quint64>(size)) {
      break;
    }
    if (identifier == kImageResourceIccProfile) {
      return QByteArray(reinterpret_cast<const char *>(resources + next),
                        static_cast<qsizetype>(dataLength));
    }
    position = next + dataLength + (dataLength % 2);
  }
  return {};
}

// PackBits の 1 行を展開する。`expected` 個のバイトが出来たら true。
bool unpackRow(const uchar *source, qsizetype sourceSize, uchar *destination, qsizetype expected) {
  qsizetype in = 0;
  qsizetype out = 0;
  while (out < expected) {
    if (in >= sourceSize) {
      return false;
    }
    const int control = static_cast<signed char>(source[in++]);
    if (control >= 0) {
      const qsizetype count = control + 1;
      if (in + count > sourceSize || out + count > expected) {
        return false;
      }
      std::memcpy(destination + out, source + in, static_cast<size_t>(count));
      in += count;
      out += count;
    } else if (control != -128) {
      const qsizetype count = 1 - control;
      if (in >= sourceSize || out + count > expected) {
        return false;
      }
      std::memset(destination + out, source[in++], static_cast<size_t>(count));
      out += count;
    }
  }
  return true;
}

}  // namespace

RawImage decodePsd(const QByteArray &fileContents) {
  const auto *data = reinterpret_cast<const uchar *>(fileContents.constData());
  const qsizetype size = fileContents.size();
  if (size < 26 || std::memcmp(data, "8BPS", 4) != 0) {
    return rawImageFailure(QStringLiteral("Not a PSD file"));
  }
  const int version = readU16(data + 4);
  if (version != 1) {
    return rawImageFailure(QStringLiteral("PSB (large document) files are not supported"));
  }
  const int channelCount = readU16(data + 12);
  const quint32 height = readU32(data + 14);
  const quint32 width = readU32(data + 18);
  const int depth = readU16(data + 22);
  const int colorMode = readU16(data + 24);

  if (width == 0 || height == 0 || static_cast<qint64>(width) * height > kMaxRawImagePixels) {
    return rawImageFailure(QStringLiteral("The image size is invalid"));
  }
  if (colorMode != kColorModeGray && colorMode != kColorModeRgb && colorMode != kColorModeCmyk) {
    return rawImageFailure(QStringLiteral("Only grayscale, RGB and CMYK PSD files are supported"));
  }
  if (depth != 8 && depth != 16 && depth != 32) {
    return rawImageFailure(QStringLiteral("This PSD bit depth is not supported"));
  }
  const bool cmyk = colorMode == kColorModeCmyk;
  if (cmyk && depth == 32) {
    return rawImageFailure(QStringLiteral("32-bit CMYK PSD files are not supported"));
  }
  const int colorChannels = colorMode == kColorModeRgb ? 3 : (cmyk ? 4 : 1);
  if (channelCount < colorChannels) {
    return rawImageFailure(QStringLiteral("The PSD file has too few channels"));
  }

  // カラーモードのデータ、画像リソース、レイヤーとマスクを飛ばす。それぞれ、長さ (4) が先頭に付く。
  qsizetype position = 26;
  QByteArray icc;
  for (int section = 0; section < 3; ++section) {
    if (position + 4 > size) {
      return rawImageFailure(QStringLiteral("The PSD file is truncated"));
    }
    const quint32 length = readU32(data + position);
    position += 4;
    if (position + length > static_cast<quint64>(size)) {
      return rawImageFailure(QStringLiteral("The PSD file is truncated"));
    }
    if (section == 1) {
      icc = findIccProfile(data + position, static_cast<qsizetype>(length));
    }
    position += length;
  }

  // 合成済みの画像。圧縮の方式 (2)、その後に平面ごとの画素。
  if (position + 2 > size) {
    return rawImageFailure(QStringLiteral("The PSD file is truncated"));
  }
  const int compression = readU16(data + position);
  position += 2;
  if (compression != 0 && compression != 1) {
    return rawImageFailure(QStringLiteral("This PSD compression is not supported"));
  }

  // 読むチャンネル。色のチャンネルの次にあれば、アルファ。
  // CMYK は、読んだあと RGB へ畳むため、出力は 3 チャンネル (とアルファ) になる。
  const bool hasAlpha = channelCount > colorChannels;
  const int storedChannels = colorChannels + (hasAlpha ? 1 : 0);
  const int outputChannels = (cmyk ? 3 : colorChannels) + (hasAlpha ? 1 : 0);
  const int bytesPerSample = depth / 8;
  const qsizetype rowBytes = static_cast<qsizetype>(width) * bytesPerSample;

  // 平面ごと (チャンネルごと) の、行の展開後のバイトを集める。
  std::vector<uchar> planes(static_cast<size_t>(storedChannels) * height * rowBytes);

  if (compression == 0) {
    const qint64 planeBytes = static_cast<qint64>(rowBytes) * height;
    if (position + planeBytes * storedChannels > size) {
      return rawImageFailure(QStringLiteral("The pixel data is truncated"));
    }
    for (int channel = 0; channel < storedChannels; ++channel) {
      std::memcpy(planes.data() + static_cast<size_t>(channel) * planeBytes,
                  data + position + planeBytes * channel, static_cast<size_t>(planeBytes));
    }
  } else {
    // RLE。先に、全チャンネル分の、行ごとの圧縮後のバイト数 (2) が並ぶ。
    const qint64 rowCountTotal = static_cast<qint64>(height) * channelCount;
    if (position + rowCountTotal * 2 > size) {
      return rawImageFailure(QStringLiteral("The PSD row table is truncated"));
    }
    const uchar *counts = data + position;
    qsizetype dataPosition = position + static_cast<qsizetype>(rowCountTotal * 2);
    for (int channel = 0; channel < channelCount; ++channel) {
      for (quint32 row = 0; row < height; ++row) {
        const qsizetype packedBytes =
            readU16(counts + (static_cast<qsizetype>(channel) * height + row) * 2);
        if (dataPosition + packedBytes > size) {
          return rawImageFailure(QStringLiteral("The pixel data is truncated"));
        }
        if (channel < storedChannels) {
          uchar *destination =
              planes.data() + (static_cast<size_t>(channel) * height + row) * rowBytes;
          if (!unpackRow(data + dataPosition, packedBytes, destination, rowBytes)) {
            return rawImageFailure(QStringLiteral("The pixel data is corrupt"));
          }
        }
        dataPosition += packedBytes;
      }
    }
  }

  // 平面から、画素ごとにチャンネルが並ぶ形へ。16bit と 32bit は、ビッグエンディアンから
  // ホストの並びへ直す。
  RawImage image;
  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.channels = outputChannels;
  image.sampleType = depth == 8    ? PixelSampleType::UInt8
                     : depth == 16 ? PixelSampleType::UInt16
                                   : PixelSampleType::Float32;
  image.iccProfile = icc;
  image.pixels.resize(static_cast<qsizetype>(width) * height * outputChannels * bytesPerSample);
  auto *output = reinterpret_cast<uchar *>(image.pixels.data());

  const auto plane = [&](quint32 row, quint32 column, int channel) {
    return planes.data() + (static_cast<size_t>(channel) * height + row) * rowBytes +
           static_cast<size_t>(column) * bytesPerSample;
  };

  if (cmyk) {
    // CMYK は、Photoshop ではインクの量を反転して持つ (255 がインク無し)。
    // 画素ごとに C M Y K と並べ直して、sRGB へ変換する。埋め込みの ICC プロファイル (CMYK 用) が
    // あれば、色を管理して変換する。無ければ素朴な式になる。出力は sRGB
    // のため、プロファイルは付けない。
    std::vector<uchar> inks(static_cast<size_t>(width) * height * 4 * bytesPerSample);
    std::vector<uchar> converted(static_cast<size_t>(width) * height * 3 * bytesPerSample);
    for (quint32 row = 0; row < height; ++row) {
      for (quint32 column = 0; column < width; ++column) {
        for (int channel = 0; channel < 4; ++channel) {
          const uchar *source = plane(row, column, channel);
          uchar *target =
              inks.data() + ((static_cast<size_t>(row) * width + column) * 4 + channel) *
                                static_cast<size_t>(bytesPerSample);
          if (depth == 8) {
            target[0] = source[0];
          } else {
            const quint16 value = readU16(source);
            std::memcpy(target, &value, sizeof(value));
          }
        }
      }
    }
    convertCmykToSrgb(icc, inks.data(), static_cast<int>(width), static_cast<int>(height),
                      bytesPerSample, 4, true, converted.data());
    image.iccProfile.clear();
    for (quint32 row = 0; row < height; ++row) {
      for (quint32 column = 0; column < width; ++column) {
        const size_t pixel = static_cast<size_t>(row) * width + column;
        uchar *target = output + (static_cast<qsizetype>(pixel) * outputChannels) * bytesPerSample;
        std::memcpy(target, converted.data() + pixel * 3 * static_cast<size_t>(bytesPerSample),
                    static_cast<size_t>(3 * bytesPerSample));
        if (hasAlpha) {
          const uchar *source = plane(row, column, 4);
          if (depth == 8) {
            target[3] = source[0];
          } else {
            const quint16 value = readU16(source);
            std::memcpy(target + 6, &value, sizeof(value));
          }
        }
      }
    }
    image.ok = true;
    return image;
  }

  for (quint32 row = 0; row < height; ++row) {
    for (quint32 column = 0; column < width; ++column) {
      for (int channel = 0; channel < outputChannels; ++channel) {
        const uchar *source = plane(row, column, channel);
        uchar *target =
            output + ((static_cast<qsizetype>(row) * width + column) * outputChannels + channel) *
                         bytesPerSample;
        if (depth == 8) {
          target[0] = source[0];
        } else if (depth == 16) {
          const quint16 value = readU16(source);
          std::memcpy(target, &value, sizeof(value));
        } else {
          const quint32 bits = readU32(source);
          std::memcpy(target, &bits, sizeof(bits));  // 浮動小数のビット列をそのまま写す。
        }
      }
    }
  }

  image.ok = true;
  return image;
}

}  // namespace heiran::infra
