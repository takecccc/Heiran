#include "heiran/infra/EmbeddedColorProfile.hpp"

#include <QFile>
#include <QPointF>
#include <cstring>

namespace heiran::infra {

namespace {

// プロファイルは画像データより前に置かれる。先頭だけ読めば足りる。
// LUT を持つ ICC プロファイルは 1 MiB を超えることがあるため、EXIF より広く取る。
constexpr qint64 kMaximumHeaderBytes = 4 * 1024 * 1024;

quint16 readUInt16(const QByteArray &data, int offset, bool littleEndian) {
  if (offset < 0 || offset + 2 > data.size()) {
    return 0;
  }
  const auto first = static_cast<quint8>(data.at(offset));
  const auto second = static_cast<quint8>(data.at(offset + 1));
  return littleEndian ? static_cast<quint16>(first | (second << 8))
                      : static_cast<quint16>((first << 8) | second);
}

quint32 readUInt32(const QByteArray &data, int offset, bool littleEndian) {
  if (offset < 0 || offset + 4 > data.size()) {
    return 0;
  }
  const auto first = static_cast<quint8>(data.at(offset));
  const auto second = static_cast<quint8>(data.at(offset + 1));
  const auto third = static_cast<quint8>(data.at(offset + 2));
  const auto fourth = static_cast<quint8>(data.at(offset + 3));
  if (littleEndian) {
    return static_cast<quint32>(first) | (static_cast<quint32>(second) << 8) |
           (static_cast<quint32>(third) << 16) | (static_cast<quint32>(fourth) << 24);
  }
  return (static_cast<quint32>(first) << 24) | (static_cast<quint32>(second) << 16) |
         (static_cast<quint32>(third) << 8) | static_cast<quint32>(fourth);
}

// zlib ストリームを展開する。`qUncompress` は先頭 4 バイトを展開後の大きさとして
// 読むため、見積もりを付けて渡す。小さすぎても Qt 側が伸ばすので、上限は要らない。
QByteArray inflateZlibStream(const QByteArray &compressed) {
  if (compressed.isEmpty()) {
    return {};
  }

  // ICC プロファイルは大きくても数百 KiB のため、見積もりはこの程度で足りる。
  constexpr quint32 kSizeHint = 1024 * 1024;
  QByteArray withHeader;
  withHeader.resize(4);
  withHeader[0] = static_cast<char>((kSizeHint >> 24) & 0xFF);
  withHeader[1] = static_cast<char>((kSizeHint >> 16) & 0xFF);
  withHeader[2] = static_cast<char>((kSizeHint >> 8) & 0xFF);
  withHeader[3] = static_cast<char>(kSizeHint & 0xFF);
  withHeader.append(compressed);
  return qUncompress(withHeader);
}

// PNG の cHRM は色度を 100000 倍した整数で持つ。
QPointF chromaticityFrom(quint32 scaled) {
  return QPointF(static_cast<double>(scaled) / 100000.0, 0.0);
}

EmbeddedColorProfile readFromPng(const QByteArray &data) {
  EmbeddedColorProfile profile;

  bool hasChromaticities = false;
  bool hasGamma = false;
  QColorSpace::PrimaryPoints primaryPoints;
  double gamma = 0.0;
  bool declaredSrgb = false;

  int offset = 8;  // シグネチャの後ろから始める。
  while (offset + 8 <= data.size()) {
    const int length = static_cast<int>(readUInt32(data, offset, false));
    if (length < 0 || offset + 12 + length > data.size()) {
      break;
    }

    const QByteArray type = data.mid(offset + 4, 4);
    const int contentOffset = offset + 8;

    if (type == "iCCP") {
      // プロファイル名 + 0 + 圧縮方式 + zlib ストリーム。
      const int nameEnd = data.indexOf('\0', contentOffset);
      if (nameEnd > 0 && nameEnd + 2 <= contentOffset + length) {
        const int streamOffset = nameEnd + 2;
        profile.iccProfile =
            inflateZlibStream(data.mid(streamOffset, contentOffset + length - streamOffset));
        if (!profile.iccProfile.isEmpty()) {
          return profile;
        }
      }
    } else if (type == "sRGB") {
      declaredSrgb = true;
    } else if (type == "cHRM" && length >= 32) {
      // 白色点、赤、緑、青の順。
      primaryPoints.whitePoint = chromaticityFrom(readUInt32(data, contentOffset, false));
      primaryPoints.whitePoint.setY(readUInt32(data, contentOffset + 4, false) / 100000.0);
      primaryPoints.redPoint = chromaticityFrom(readUInt32(data, contentOffset + 8, false));
      primaryPoints.redPoint.setY(readUInt32(data, contentOffset + 12, false) / 100000.0);
      primaryPoints.greenPoint = chromaticityFrom(readUInt32(data, contentOffset + 16, false));
      primaryPoints.greenPoint.setY(readUInt32(data, contentOffset + 20, false) / 100000.0);
      primaryPoints.bluePoint = chromaticityFrom(readUInt32(data, contentOffset + 24, false));
      primaryPoints.bluePoint.setY(readUInt32(data, contentOffset + 28, false) / 100000.0);
      hasChromaticities = true;
    } else if (type == "gAMA" && length >= 4) {
      // gAMA は「ファイルの値 -> 線形」の指数を 100000 倍で持つ。
      const quint32 scaled = readUInt32(data, contentOffset, false);
      if (scaled > 0) {
        gamma = 1.0 / (static_cast<double>(scaled) / 100000.0);
        hasGamma = true;
      }
    } else if (type == "IDAT" || type == "IEND") {
      // 画像データより後ろに色情報は現れない。
      break;
    }

    offset += 12 + length;  // 長さ + 型 + 中身 + CRC
  }

  if (declaredSrgb) {
    profile.declaredColorSpace = QColorSpace(QColorSpace::SRgb);
    return profile;
  }
  if (hasChromaticities) {
    // gAMA が無い場合は sRGB の曲線を仮に当てる。cHRM だけの PNG は稀で、
    // 曲線の申告が無い以上は最も多い形を採るしかない。
    const QColorSpace space(primaryPoints,
                            hasGamma ? QColorSpace::TransferFunction::Gamma
                                     : QColorSpace::TransferFunction::SRgb,
                            hasGamma ? static_cast<float>(gamma) : 0.0F);
    if (space.isValid()) {
      profile.declaredColorSpace = space;
    }
    return profile;
  }
  if (hasGamma) {
    QColorSpace space(QColorSpace::Primaries::SRgb, QColorSpace::TransferFunction::Gamma,
                      static_cast<float>(gamma));
    if (space.isValid()) {
      profile.declaredColorSpace = space;
    }
  }
  return profile;
}

EmbeddedColorProfile readFromJpeg(const QByteArray &data) {
  EmbeddedColorProfile profile;

  // APP2 は 64 KiB を超える場合に分割される。順番に並べ直してから繋ぐ。
  QList<QByteArray> chunks;
  int expectedCount = 0;

  int offset = 2;  // SOI の後ろから始める。
  while (offset + 4 <= data.size()) {
    if (static_cast<quint8>(data.at(offset)) != 0xFF) {
      break;
    }

    const auto marker = static_cast<quint8>(data.at(offset + 1));
    // SOS より後ろは画像データで、プロファイルは現れない。
    if (marker == 0xDA || marker == 0xD9) {
      break;
    }

    const int segmentLength = readUInt16(data, offset + 2, false);
    if (segmentLength < 2 || offset + 2 + segmentLength > data.size()) {
      break;
    }

    constexpr int kSignatureBytes = 12;  // "ICC_PROFILE\0"
    if (marker == 0xE2 && segmentLength >= 2 + kSignatureBytes + 2 &&
        std::memcmp(data.constData() + offset + 4, "ICC_PROFILE\0", kSignatureBytes) == 0) {
      const int headerOffset = offset + 4 + kSignatureBytes;
      const int sequenceNumber = static_cast<quint8>(data.at(headerOffset));
      const int totalCount = static_cast<quint8>(data.at(headerOffset + 1));
      const int payloadOffset = headerOffset + 2;
      const int payloadLength = offset + 2 + segmentLength - payloadOffset;
      if (payloadLength > 0 && sequenceNumber >= 1 && totalCount >= sequenceNumber) {
        expectedCount = std::max(expectedCount, totalCount);
        while (chunks.size() < totalCount) {
          chunks.append(QByteArray());
        }
        chunks[sequenceNumber - 1] = data.mid(payloadOffset, payloadLength);
      }
    }

    offset += 2 + segmentLength;
  }

  if (expectedCount > 0 && chunks.size() == expectedCount) {
    QByteArray combined;
    for (const auto &chunk : chunks) {
      // 欠けた断片があれば繋がない。中途半端なプロファイルは使えない。
      if (chunk.isEmpty()) {
        return profile;
      }
      combined.append(chunk);
    }
    profile.iccProfile = combined;
  }
  return profile;
}

EmbeddedColorProfile readFromTiff(const QByteArray &data, int tiffStart) {
  EmbeddedColorProfile profile;
  if (tiffStart < 0 || tiffStart + 8 > data.size()) {
    return profile;
  }

  const bool littleEndian = data.at(tiffStart) == 'I' && data.at(tiffStart + 1) == 'I';
  const bool bigEndian = data.at(tiffStart) == 'M' && data.at(tiffStart + 1) == 'M';
  if (!littleEndian && !bigEndian) {
    return profile;
  }
  if (readUInt16(data, tiffStart + 2, littleEndian) != 42) {
    return profile;
  }

  constexpr int kInterColorProfileTag = 34675;
  constexpr int kIfdEntryBytes = 12;

  const int ifdOffset = tiffStart + static_cast<int>(readUInt32(data, tiffStart + 4, littleEndian));
  const int entryCount = readUInt16(data, ifdOffset, littleEndian);
  for (int index = 0; index < entryCount; ++index) {
    const int entryOffset = ifdOffset + 2 + index * kIfdEntryBytes;
    if (entryOffset + kIfdEntryBytes > data.size()) {
      break;
    }
    if (readUInt16(data, entryOffset, littleEndian) != kInterColorProfileTag) {
      continue;
    }

    const int count = static_cast<int>(readUInt32(data, entryOffset + 4, littleEndian));
    // UNDEFINED 型で 1 バイトずつのため、大きさがそのまま個数になる。
    // 4 バイトに収まらないので値は必ず外へ置かれる。
    if (count <= 4) {
      break;
    }
    const int valueOffset =
        tiffStart + static_cast<int>(readUInt32(data, entryOffset + 8, littleEndian));
    if (valueOffset < 0 || valueOffset + count > data.size()) {
      break;
    }
    profile.iccProfile = data.mid(valueOffset, count);
    break;
  }

  return profile;
}

EmbeddedColorProfile readFromWebp(const QByteArray &data) {
  EmbeddedColorProfile profile;

  int offset = 12;  // "RIFF" + 大きさ + "WEBP" の後ろから始める。
  while (offset + 8 <= data.size()) {
    const QByteArray type = data.mid(offset, 4);
    const int length = static_cast<int>(readUInt32(data, offset + 4, true));
    if (length < 0 || offset + 8 + length > data.size()) {
      break;
    }

    if (type == "ICCP") {
      profile.iccProfile = data.mid(offset + 8, length);
      break;
    }

    // チャンクは 2 バイト境界に揃う。
    offset += 8 + length + (length % 2);
  }

  return profile;
}

}  // namespace

QColorSpace EmbeddedColorProfile::resolve() const {
  if (!iccProfile.isEmpty()) {
    const QColorSpace fromIcc = QColorSpace::fromIccProfile(iccProfile);
    if (fromIcc.isValid()) {
      return fromIcc;
    }
  }
  return declaredColorSpace;
}

EmbeddedColorProfile readEmbeddedColorProfile(const QByteArray &fileContents) {
  if (fileContents.size() < 12) {
    return {};
  }

  if (fileContents.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8))) {
    return readFromPng(fileContents);
  }

  const auto first = static_cast<quint8>(fileContents.at(0));
  const auto second = static_cast<quint8>(fileContents.at(1));
  if (first == 0xFF && second == 0xD8) {
    return readFromJpeg(fileContents);
  }
  if ((first == 'I' && second == 'I') || (first == 'M' && second == 'M')) {
    return readFromTiff(fileContents, 0);
  }
  if (fileContents.startsWith("RIFF") && fileContents.mid(8, 4) == "WEBP") {
    return readFromWebp(fileContents);
  }

  return {};
}

EmbeddedColorProfile readEmbeddedColorProfile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return readEmbeddedColorProfile(file.read(kMaximumHeaderBytes));
}

}  // namespace heiran::infra
