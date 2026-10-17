#include "heiran/infra/ExifOrientation.hpp"

#include <QFile>
#include <cstring>

namespace heiran::infra {

namespace {

constexpr int kOrientationTag = 0x0112;
constexpr int kTiffHeaderBytes = 8;
constexpr int kIfdEntryBytes = 12;

// 先頭だけ読めば足りる。EXIF は画像データより前に置かれる。
constexpr qint64 kMaximumHeaderBytes = 512 * 1024;

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

ExifOrientation orientationFromValue(quint16 value) {
  return value >= 1 && value <= 8 ? static_cast<ExifOrientation>(value) : ExifOrientation::Normal;
}

// TIFF の構造から orientation を探す。`tiffStart` はバイトオーダ標識の位置。
ExifOrientation readFromTiffBlock(const QByteArray &data, int tiffStart) {
  if (tiffStart < 0 || tiffStart + kTiffHeaderBytes > data.size()) {
    return ExifOrientation::Normal;
  }

  const bool littleEndian = data.at(tiffStart) == 'I' && data.at(tiffStart + 1) == 'I';
  const bool bigEndian = data.at(tiffStart) == 'M' && data.at(tiffStart + 1) == 'M';
  if (!littleEndian && !bigEndian) {
    return ExifOrientation::Normal;
  }
  if (readUInt16(data, tiffStart + 2, littleEndian) != 42) {
    return ExifOrientation::Normal;
  }

  const int ifdOffset = tiffStart + static_cast<int>(readUInt32(data, tiffStart + 4, littleEndian));
  const int entryCount = readUInt16(data, ifdOffset, littleEndian);
  for (int index = 0; index < entryCount; ++index) {
    const int entryOffset = ifdOffset + 2 + index * kIfdEntryBytes;
    if (entryOffset + kIfdEntryBytes > data.size()) {
      break;
    }
    if (readUInt16(data, entryOffset, littleEndian) != kOrientationTag) {
      continue;
    }
    // 型は SHORT で個数は 1。値は 4 バイトの領域の先頭 2 バイトに入る。
    return orientationFromValue(readUInt16(data, entryOffset + 8, littleEndian));
  }

  return ExifOrientation::Normal;
}

// JPEG のマーカーを辿って APP1 の Exif ブロックを探す。
ExifOrientation readFromJpeg(const QByteArray &data) {
  int offset = 2;  // SOI の後ろから始める。
  while (offset + 4 <= data.size()) {
    if (static_cast<quint8>(data.at(offset)) != 0xFF) {
      return ExifOrientation::Normal;
    }

    const auto marker = static_cast<quint8>(data.at(offset + 1));
    // SOS より後ろは画像データで、EXIF は現れない。
    if (marker == 0xDA || marker == 0xD9) {
      return ExifOrientation::Normal;
    }

    const int segmentLength = readUInt16(data, offset + 2, false);
    if (segmentLength < 2) {
      return ExifOrientation::Normal;
    }

    if (marker == 0xE1 && offset + 4 + 6 <= data.size() &&
        std::memcmp(data.constData() + offset + 4, "Exif\0\0", 6) == 0) {
      return readFromTiffBlock(data, offset + 4 + 6);
    }

    offset += 2 + segmentLength;
  }

  return ExifOrientation::Normal;
}

}  // namespace

ExifOrientation readExifOrientation(const QByteArray &fileContents) {
  if (fileContents.size() < 4) {
    return ExifOrientation::Normal;
  }

  const auto first = static_cast<quint8>(fileContents.at(0));
  const auto second = static_cast<quint8>(fileContents.at(1));
  if (first == 0xFF && second == 0xD8) {
    return readFromJpeg(fileContents);
  }
  if ((first == 'I' && second == 'I') || (first == 'M' && second == 'M')) {
    return readFromTiffBlock(fileContents, 0);
  }

  return ExifOrientation::Normal;
}

ExifOrientation readExifOrientation(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return ExifOrientation::Normal;
  }
  return readExifOrientation(file.read(kMaximumHeaderBytes));
}

ExifTransform toExifTransform(ExifOrientation orientation) {
  // 回転してから反転する順序で表す。表示側の回転と反転の適用順に合わせる。
  switch (orientation) {
    case ExifOrientation::FlipHorizontal:
      return {.rotationQuarterTurns = 0, .flipX = true, .flipY = false};
    case ExifOrientation::Rotate180:
      return {.rotationQuarterTurns = 2, .flipX = false, .flipY = false};
    case ExifOrientation::FlipVertical:
      return {.rotationQuarterTurns = 0, .flipX = false, .flipY = true};
    case ExifOrientation::Transpose:
      return {.rotationQuarterTurns = 1, .flipX = true, .flipY = false};
    case ExifOrientation::Rotate90:
      return {.rotationQuarterTurns = 1, .flipX = false, .flipY = false};
    case ExifOrientation::Transverse:
      return {.rotationQuarterTurns = 3, .flipX = true, .flipY = false};
    case ExifOrientation::Rotate270:
      return {.rotationQuarterTurns = 3, .flipX = false, .flipY = false};
    case ExifOrientation::Normal:
    default:
      return {};
  }
}

}  // namespace heiran::infra
