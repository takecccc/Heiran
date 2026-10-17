#include "heiran/infra/ExifReader.hpp"

#include <QBuffer>
#include <QIODevice>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>

namespace heiran::infra {

namespace {

// 読み出しの上限。壊れたファイルが、巨大な確保や、長い巡回を起こさないための歯止め。
constexpr qint64 kMaxExifBlockBytes = 1 << 20;
constexpr int kMaxEntriesPerIfd = 512;
constexpr int kMaxIfdDepth = 4;
constexpr qint64 kMaxValueBytes = 4096;

// ---- バイト列の読み手 ----

class Source {
public:
  explicit Source(QIODevice &device) : device_(device) {}

  // offset から length バイト。範囲を超えたら、読めた分だけ (空もありうる)。
  QByteArray read(qint64 offset, qint64 length) {
    if (offset < 0 || length < 0 || length > kMaxExifBlockBytes) {
      return {};
    }
    if (!device_.seek(offset)) {
      return {};
    }
    return device_.read(length);
  }

  qint64 size() const { return device_.size(); }

private:
  QIODevice &device_;
};

quint16 u16(const QByteArray &bytes, int offset, bool bigEndian) {
  const auto *p = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
  return bigEndian ? static_cast<quint16>((p[0] << 8) | p[1])
                   : static_cast<quint16>(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray &bytes, int offset, bool bigEndian) {
  const auto *p = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
  return bigEndian ? (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
                         (static_cast<quint32>(p[2]) << 8) | p[3]
                   : (static_cast<quint32>(p[3]) << 24) | (static_cast<quint32>(p[2]) << 16) |
                         (static_cast<quint32>(p[1]) << 8) | p[0];
}

// ---- 値の整形 ----

QString formatRational(quint32 numerator, quint32 denominator) {
  if (denominator == 0) {
    return QStringLiteral("-");
  }
  return QString::number(static_cast<double>(numerator) / denominator, 'g', 6);
}

QString formatSignedRational(qint32 numerator, qint32 denominator) {
  if (denominator == 0) {
    return QStringLiteral("-");
  }
  return QString::number(static_cast<double>(numerator) / denominator, 'g', 6);
}

// 露出時間。1 秒より短ければ、分数 (1/250) で。
QString formatExposureTime(quint32 numerator, quint32 denominator) {
  if (denominator == 0) {
    return QStringLiteral("-");
  }
  if (numerator == 0) {
    return QStringLiteral("0");
  }
  if (numerator >= denominator) {
    return QString("%1 s").arg(static_cast<double>(numerator) / denominator, 0, 'g', 4);
  }
  const double reciprocal = static_cast<double>(denominator) / numerator;
  // 1/250 のように、きれいな分母のとき。
  if (std::abs(reciprocal - std::round(reciprocal)) < 0.05) {
    return QString("1/%1 s").arg(static_cast<qint64>(std::llround(reciprocal)));
  }
  return QString("%1/%2 s").arg(numerator).arg(denominator);
}

QString nameForValue(int value, const QHash<int, QString> &names) {
  const auto found = names.constFind(value);
  return found == names.constEnd() ? QString::number(value)
                                   : QString("%1 (%2)").arg(value).arg(found.value());
}

const QHash<int, QString> &orientationNames() {
  static const QHash<int, QString> names = {
      {1, "Normal"},    {2, "Mirror horizontal"}, {3, "Rotate 180"}, {4, "Mirror vertical"},
      {5, "Transpose"}, {6, "Rotate 90 CW"},      {7, "Transverse"}, {8, "Rotate 270 CW"},
  };
  return names;
}

const QHash<int, QString> &resolutionUnitNames() {
  static const QHash<int, QString> names = {{1, "none"}, {2, "inch"}, {3, "cm"}};
  return names;
}

const QHash<int, QString> &exposureProgramNames() {
  static const QHash<int, QString> names = {
      {0, "Not defined"},      {1, "Manual"},   {2, "Program"}, {3, "Aperture priority"},
      {4, "Shutter priority"}, {5, "Creative"}, {6, "Action"},  {7, "Portrait"},
      {8, "Landscape"},
  };
  return names;
}

const QHash<int, QString> &meteringModeNames() {
  static const QHash<int, QString> names = {
      {0, "Unknown"},    {1, "Average"}, {2, "Center-weighted"}, {3, "Spot"},
      {4, "Multi-spot"}, {5, "Pattern"}, {6, "Partial"},         {255, "Other"},
  };
  return names;
}

const QHash<int, QString> &colorSpaceNames() {
  static const QHash<int, QString> names = {{1, "sRGB"}, {65535, "Uncalibrated"}};
  return names;
}

const QHash<int, QString> &exposureModeNames() {
  static const QHash<int, QString> names = {{0, "Auto"}, {1, "Manual"}, {2, "Auto bracket"}};
  return names;
}

const QHash<int, QString> &whiteBalanceNames() {
  static const QHash<int, QString> names = {{0, "Auto"}, {1, "Manual"}};
  return names;
}

QString flashDescription(int value) {
  return QString("%1 (%2)").arg(value).arg((value & 1) != 0 ? "fired" : "did not fire");
}

// ---- タグの表 ----

enum class Ifd { Zero, Exif, Gps };

struct TagInfo {
  QString name;
};

const QHash<int, QString> &tagNames(Ifd ifd) {
  static const QHash<int, QString> zero = {
      {0x010E, "ImageDescription"}, {0x010F, "Make"},        {0x0110, "Model"},
      {0x0112, "Orientation"},      {0x011A, "XResolution"}, {0x011B, "YResolution"},
      {0x0128, "ResolutionUnit"},   {0x0131, "Software"},    {0x0132, "DateTime"},
      {0x013B, "Artist"},           {0x8298, "Copyright"},
  };
  static const QHash<int, QString> exif = {
      {0x829A, "ExposureTime"},
      {0x829D, "FNumber"},
      {0x8822, "ExposureProgram"},
      {0x8827, "ISOSpeedRatings"},
      {0x9000, "ExifVersion"},
      {0x9003, "DateTimeOriginal"},
      {0x9004, "DateTimeDigitized"},
      {0x9204, "ExposureBiasValue"},
      {0x9207, "MeteringMode"},
      {0x9209, "Flash"},
      {0x920A, "FocalLength"},
      {0xA001, "ColorSpace"},
      {0xA002, "PixelXDimension"},
      {0xA003, "PixelYDimension"},
      {0xA402, "ExposureMode"},
      {0xA403, "WhiteBalance"},
      {0xA405, "FocalLengthIn35mmFilm"},
      {0xA433, "LensMake"},
      {0xA434, "LensModel"},
  };
  static const QHash<int, QString> gps = {
      {0x0001, "GPSLatitudeRef"}, {0x0002, "GPSLatitude"},    {0x0003, "GPSLongitudeRef"},
      {0x0004, "GPSLongitude"},   {0x0005, "GPSAltitudeRef"}, {0x0006, "GPSAltitude"},
  };
  switch (ifd) {
    case Ifd::Exif:
      return exif;
    case Ifd::Gps:
      return gps;
    case Ifd::Zero:
    default:
      return zero;
  }
}

constexpr int kTagExifIfd = 0x8769;
constexpr int kTagGpsIfd = 0x8825;

// TIFF の型ごとの 1 要素のバイト数。0 は未対応の型。
int typeSize(int type) {
  switch (type) {
    case 1:  // BYTE
    case 2:  // ASCII
    case 6:  // SBYTE
    case 7:  // UNDEFINED
      return 1;
    case 3:  // SHORT
    case 8:  // SSHORT
      return 2;
    case 4:   // LONG
    case 9:   // SLONG
    case 11:  // FLOAT
      return 4;
    case 5:   // RATIONAL
    case 10:  // SRATIONAL
    case 12:  // DOUBLE
      return 8;
    default:
      return 0;
  }
}

class Parser {
public:
  Parser(Source &source, bool bigEndian) : source_(source), bigEndian_(bigEndian) {}

  void readIfd(qint64 offset, Ifd ifd, int depth, QVector<MetadataEntry> *entries) {
    if (depth > kMaxIfdDepth || visited_.contains(offset)) {
      return;
    }
    visited_.insert(offset);

    const QByteArray countBytes = source_.read(offset, 2);
    if (countBytes.size() < 2) {
      return;
    }
    const int count = std::min<int>(u16(countBytes, 0, bigEndian_), kMaxEntriesPerIfd);
    const QByteArray table = source_.read(offset + 2, static_cast<qint64>(count) * 12);
    const int available = static_cast<int>(table.size() / 12);

    // 下位の IFD は、この表を読み終えてから辿る (順序を、タグの並びに揃える)。
    qint64 exifOffset = 0;
    qint64 gpsOffset = 0;

    for (int index = 0; index < available; ++index) {
      const int base = index * 12;
      const int tag = u16(table, base, bigEndian_);
      const int type = u16(table, base + 2, bigEndian_);
      const quint32 valueCount = u32(table, base + 4, bigEndian_);

      if (ifd == Ifd::Zero && tag == kTagExifIfd) {
        exifOffset = u32(table, base + 8, bigEndian_);
        continue;
      }
      if (ifd == Ifd::Zero && tag == kTagGpsIfd) {
        gpsOffset = u32(table, base + 8, bigEndian_);
        continue;
      }

      const auto nameIt = tagNames(ifd).constFind(tag);
      if (nameIt == tagNames(ifd).constEnd()) {
        continue;  // 知らないタグは出さない。
      }

      const int size = typeSize(type);
      if (size == 0 || valueCount == 0) {
        continue;
      }
      const qint64 totalBytes = static_cast<qint64>(size) * valueCount;
      QByteArray data;
      if (totalBytes <= 4) {
        data = table.mid(base + 8, static_cast<int>(totalBytes));
      } else if (totalBytes <= kMaxValueBytes) {
        data = source_.read(u32(table, base + 8, bigEndian_), totalBytes);
        if (data.size() < totalBytes) {
          continue;
        }
      } else {
        continue;
      }

      const QString value = formatValue(ifd, tag, type, static_cast<int>(valueCount), data);
      if (!value.isEmpty()) {
        const QString group = ifd == Ifd::Zero ? "Tags" : (ifd == Ifd::Exif ? "EXIF" : "GPS");
        entries->push_back({group, nameIt.value(), value});
      }
    }

    if (exifOffset > 0) {
      readIfd(exifOffset, Ifd::Exif, depth + 1, entries);
    }
    if (gpsOffset > 0) {
      readIfd(gpsOffset, Ifd::Gps, depth + 1, entries);
    }
  }

private:
  quint32 rationalPart(const QByteArray &data, int index, int part) const {
    return u32(data, index * 8 + part * 4, bigEndian_);
  }

  // GPS の緯度・経度。度、分、秒の 3 つの有理数を、十進の度へ。
  QString formatGpsCoordinate(const QByteArray &data, int count) const {
    if (count < 3) {
      return {};
    }
    double degrees = 0.0;
    for (int part = 0; part < 3; ++part) {
      const quint32 denominator = rationalPart(data, part, 1);
      if (denominator == 0) {
        return {};
      }
      degrees +=
          static_cast<double>(rationalPart(data, part, 0)) / denominator / std::pow(60.0, part);
    }
    return QString("%1°").arg(degrees, 0, 'f', 6);
  }

  QString formatValue(Ifd ifd, int tag, int type, int count, const QByteArray &data) const {
    // 文字列。
    if (type == 2) {
      QByteArray text = data;
      const qsizetype nul = text.indexOf('\0');
      if (nul >= 0) {
        text.truncate(nul);
      }
      return QString::fromUtf8(text).trimmed();
    }

    // バージョンなど、文字として読める UNDEFINED。
    if (type == 7) {
      if (tag == 0x9000) {
        return QString::fromLatin1(data);
      }
      return {};
    }

    if (type == 5) {  // RATIONAL
      if (ifd == Ifd::Gps && (tag == 0x0002 || tag == 0x0004)) {
        return formatGpsCoordinate(data, count);
      }
      const quint32 numerator = rationalPart(data, 0, 0);
      const quint32 denominator = rationalPart(data, 0, 1);
      if (ifd == Ifd::Exif && tag == 0x829A) {
        return formatExposureTime(numerator, denominator);
      }
      if (ifd == Ifd::Exif && tag == 0x829D) {
        return "f/" + formatRational(numerator, denominator);
      }
      if (ifd == Ifd::Exif && tag == 0x920A) {
        return formatRational(numerator, denominator) + " mm";
      }
      return formatRational(numerator, denominator);
    }
    if (type == 10) {  // SRATIONAL
      const qint32 numerator = static_cast<qint32>(rationalPart(data, 0, 0));
      const qint32 denominator = static_cast<qint32>(rationalPart(data, 0, 1));
      const QString text = formatSignedRational(numerator, denominator);
      return (ifd == Ifd::Exif && tag == 0x9204) ? text + " EV" : text;
    }

    // 整数 (1 つ目だけ)。
    qint64 value = 0;
    if (type == 3 || type == 8) {
      value = u16(data, 0, bigEndian_);
    } else if (type == 4 || type == 9) {
      value = u32(data, 0, bigEndian_);
    } else if (type == 1 || type == 6) {
      value = static_cast<uchar>(data.at(0));
    } else {
      return {};
    }

    const int v = static_cast<int>(value);
    if (ifd == Ifd::Zero && tag == 0x0112) {
      return nameForValue(v, orientationNames());
    }
    if (ifd == Ifd::Zero && tag == 0x0128) {
      return nameForValue(v, resolutionUnitNames());
    }
    if (ifd == Ifd::Exif) {
      switch (tag) {
        case 0x8822:
          return nameForValue(v, exposureProgramNames());
        case 0x9207:
          return nameForValue(v, meteringModeNames());
        case 0x9209:
          return flashDescription(v);
        case 0xA001:
          return nameForValue(v, colorSpaceNames());
        case 0xA402:
          return nameForValue(v, exposureModeNames());
        case 0xA403:
          return nameForValue(v, whiteBalanceNames());
        default:
          break;
      }
    }
    return QString::number(value);
  }

  Source &source_;
  bool bigEndian_;
  QSet<qint64> visited_;
};

}  // namespace

QVector<MetadataEntry> readExifMetadata(QIODevice &device) {
  QVector<MetadataEntry> entries;
  Source source(device);

  // TIFF のヘッダー: 並び (II または MM)、42、先頭の IFD への位置。
  const QByteArray header = source.read(0, 8);
  if (header.size() < 8) {
    return entries;
  }
  bool bigEndian = false;
  if (header.startsWith("MM")) {
    bigEndian = true;
  } else if (!header.startsWith("II")) {
    return entries;
  }
  if (u16(header, 2, bigEndian) != 42) {
    return entries;
  }

  Parser parser(source, bigEndian);
  parser.readIfd(u32(header, 4, bigEndian), Ifd::Zero, 0, &entries);
  return entries;
}

QVector<MetadataEntry> readExifMetadata(const QByteArray &fileContents) {
  QBuffer buffer;
  buffer.setData(fileContents);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return {};
  }
  return readExifMetadata(buffer);
}

QByteArray extractExifBlock(QIODevice &device) {
  Source source(device);
  const QByteArray signature = source.read(0, 12);

  // JPEG: マーカーを順に辿り、APP1 の "Exif\0\0" を探す。画素の始まり (SOS) で打ち切る。
  if (signature.size() >= 2 && static_cast<uchar>(signature[0]) == 0xFF &&
      static_cast<uchar>(signature[1]) == 0xD8) {
    qint64 position = 2;
    for (int guard = 0; guard < 64; ++guard) {
      const QByteArray marker = source.read(position, 4);
      if (marker.size() < 4 || static_cast<uchar>(marker[0]) != 0xFF) {
        return {};
      }
      const int code = static_cast<uchar>(marker[1]);
      if (code == 0xDA || code == 0xD9) {
        return {};
      }
      const int length = u16(marker, 2, true);
      if (length < 2) {
        return {};
      }
      if (code == 0xE1) {
        const QByteArray body = source.read(position + 4, length - 2);
        if (body.startsWith(QByteArray("Exif\0\0", 6))) {
          return body.mid(6);
        }
      }
      position += 2 + length;
    }
    return {};
  }

  // PNG: 署名の後、チャンク (長さ、種類、データ、CRC) を辿り、eXIf を探す。
  if (signature.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8))) {
    qint64 position = 8;
    const qint64 fileSize = source.size();
    for (int guard = 0; guard < 4096 && position + 12 <= fileSize; ++guard) {
      const QByteArray head = source.read(position, 8);
      if (head.size() < 8) {
        return {};
      }
      const qint64 length = u32(head, 0, true);
      const QByteArray type = head.mid(4, 4);
      if (type == "eXIf") {
        if (length > kMaxExifBlockBytes) {
          return {};
        }
        QByteArray body = source.read(position + 8, length);
        // "Exif\0\0" の接頭辞が付いている書き手もある。
        return body.startsWith(QByteArray("Exif\0\0", 6)) ? body.mid(6) : body;
      }
      if (type == "IEND") {
        return {};
      }
      position += 12 + length;
    }
    return {};
  }

  // WebP: RIFF、大きさ、WEBP、チャンク (4 文字、リトルエンディアンの大きさ、データ、偶数への詰め)。
  if (signature.startsWith("RIFF") && signature.mid(8, 4) == "WEBP") {
    qint64 position = 12;
    const qint64 fileSize = source.size();
    for (int guard = 0; guard < 4096 && position + 8 <= fileSize; ++guard) {
      const QByteArray head = source.read(position, 8);
      if (head.size() < 8) {
        return {};
      }
      const qint64 length = u32(head, 4, false);
      if (head.left(4) == "EXIF") {
        if (length > kMaxExifBlockBytes) {
          return {};
        }
        const QByteArray body = source.read(position + 8, length);
        return body.startsWith(QByteArray("Exif\0\0", 6)) ? body.mid(6) : body;
      }
      position += 8 + length + (length % 2);
    }
    return {};
  }

  return {};
}

}  // namespace heiran::infra
