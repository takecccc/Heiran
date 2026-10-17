// EXIF の読み取り (ExifReader)。入力は、テストの中でバイト列から組み立てる。

#include "heiran/infra/ExifReader.hpp"
#include "heiran/infra/ImageMetadata.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace heiran::infra;

namespace {

void putU16(QByteArray *bytes, quint16 value, bool bigEndian) {
  if (bigEndian) {
    bytes->append(static_cast<char>(value >> 8));
    bytes->append(static_cast<char>(value & 0xFF));
  } else {
    bytes->append(static_cast<char>(value & 0xFF));
    bytes->append(static_cast<char>(value >> 8));
  }
}

void putU32(QByteArray *bytes, quint32 value, bool bigEndian) {
  if (bigEndian) {
    putU16(bytes, static_cast<quint16>(value >> 16), true);
    putU16(bytes, static_cast<quint16>(value & 0xFFFF), true);
  } else {
    putU16(bytes, static_cast<quint16>(value & 0xFFFF), false);
    putU16(bytes, static_cast<quint16>(value >> 16), false);
  }
}

// IFD の 1 項目。`data` は、その型の値を、並びを揃えて作ったもの (long や rational の組)。
struct Entry {
  quint16 tag;
  quint16 type;
  quint32 count;
  QByteArray data;
};

Entry ascii(quint16 tag, const QString &text) {
  QByteArray data = text.toLatin1();
  data.append('\0');
  return {tag, 2, static_cast<quint32>(data.size()), data};
}

Entry shortValue(quint16 tag, quint16 value, bool bigEndian) {
  QByteArray data;
  putU16(&data, value, bigEndian);
  return {tag, 3, 1, data};
}

Entry rational(quint16 tag, quint32 numerator, quint32 denominator, bool bigEndian) {
  QByteArray data;
  putU32(&data, numerator, bigEndian);
  putU32(&data, denominator, bigEndian);
  return {tag, 5, 1, data};
}

Entry signedRational(quint16 tag, qint32 numerator, qint32 denominator, bool bigEndian) {
  QByteArray data;
  putU32(&data, static_cast<quint32>(numerator), bigEndian);
  putU32(&data, static_cast<quint32>(denominator), bigEndian);
  return {tag, 10, 1, data};
}

Entry rationalTriple(quint16 tag, quint32 a, quint32 b, quint32 c, bool bigEndian) {
  QByteArray data;
  for (const quint32 value : {a, quint32{1}, b, quint32{1}, c, quint32{1}}) {
    putU32(&data, value, bigEndian);
  }
  return {tag, 5, 3, data};
}

// TIFF のブロックを組み立てる。IFD0、Exif の下位、GPS の下位。
QByteArray buildTiff(bool bigEndian, const QVector<Entry> &ifd0, const QVector<Entry> &exif,
                     const QVector<Entry> &gps) {
  const auto ifdBytes = [](qsizetype count) { return 2 + count * 12 + 4; };

  QVector<Entry> zero = ifd0;
  // 下位の IFD への印は、あとで位置を入れる。
  const int exifPointerIndex = static_cast<int>(zero.size());
  if (!exif.isEmpty()) {
    zero.push_back({0x8769, 4, 1, QByteArray(4, '\0')});
  }
  const int gpsPointerIndex = static_cast<int>(zero.size());
  if (!gps.isEmpty()) {
    zero.push_back({0x8825, 4, 1, QByteArray(4, '\0')});
  }

  const quint32 zeroOffset = 8;
  const quint32 exifOffset = zeroOffset + static_cast<quint32>(ifdBytes(zero.size()));
  const quint32 gpsOffset =
      exifOffset + static_cast<quint32>(exif.isEmpty() ? 0 : ifdBytes(exif.size()));
  const quint32 dataStart =
      gpsOffset + static_cast<quint32>(gps.isEmpty() ? 0 : ifdBytes(gps.size()));

  if (!exif.isEmpty()) {
    QByteArray pointer;
    putU32(&pointer, exifOffset, bigEndian);
    zero[exifPointerIndex].data = pointer;
  }
  if (!gps.isEmpty()) {
    QByteArray pointer;
    putU32(&pointer, gpsOffset, bigEndian);
    zero[gpsPointerIndex].data = pointer;
  }

  QByteArray bytes(bigEndian ? "MM" : "II");
  putU16(&bytes, 42, bigEndian);
  putU32(&bytes, zeroOffset, bigEndian);

  QByteArray dataArea;
  const auto writeIfd = [&](const QVector<Entry> &entries) {
    putU16(&bytes, static_cast<quint16>(entries.size()), bigEndian);
    for (const auto &entry : entries) {
      putU16(&bytes, entry.tag, bigEndian);
      putU16(&bytes, entry.type, bigEndian);
      putU32(&bytes, entry.count, bigEndian);
      if (entry.data.size() <= 4) {
        QByteArray padded = entry.data;
        padded.append(QByteArray(4 - padded.size(), '\0'));
        bytes += padded;
      } else {
        putU32(&bytes, dataStart + static_cast<quint32>(dataArea.size()), bigEndian);
        dataArea += entry.data;
        if (dataArea.size() % 2 != 0) {
          dataArea.append('\0');
        }
      }
    }
    putU32(&bytes, 0, bigEndian);  // 次の IFD は無い。
  };
  writeIfd(zero);
  if (!exif.isEmpty()) {
    writeIfd(exif);
  }
  if (!gps.isEmpty()) {
    writeIfd(gps);
  }
  return bytes + dataArea;
}

QByteArray jpegWithExif(const QByteArray &tiff) {
  QByteArray bytes;
  bytes.append("\xFF\xD8", 2);
  // 先に別のセグメント (JFIF の APP0) があっても、APP1 を見つけられること。
  bytes.append("\xFF\xE0", 2);
  putU16(&bytes, 16, true);
  bytes.append("JFIF\0\1\1\0\0\1\0\1\0\0", 14);
  const QByteArray body = QByteArray("Exif\0\0", 6) + tiff;
  bytes.append("\xFF\xE1", 2);
  putU16(&bytes, static_cast<quint16>(body.size() + 2), true);
  bytes += body;
  bytes.append("\xFF\xDA\x00\x02", 4);  // 画素の始まり。以降は読まない。
  return bytes;
}

QByteArray pngWithExif(const QByteArray &tiff) {
  QByteArray bytes("\x89PNG\r\n\x1a\n", 8);
  // IHDR (13 バイト。中身は読まない)。
  putU32(&bytes, 13, true);
  bytes.append("IHDR");
  bytes += QByteArray(13, '\0');
  putU32(&bytes, 0, true);  // CRC (検証しない)
  putU32(&bytes, static_cast<quint32>(tiff.size()), true);
  bytes.append("eXIf");
  bytes += tiff;
  putU32(&bytes, 0, true);
  putU32(&bytes, 0, true);
  bytes.append("IEND");
  putU32(&bytes, 0, true);
  return bytes;
}

QByteArray webpWithExif(const QByteArray &tiff) {
  QByteArray chunks;
  chunks.append("VP8 ");
  putU32(&chunks, 3, false);
  chunks += QByteArray(3, '\0');
  chunks.append('\0');  // 偶数への詰め
  chunks.append("EXIF");
  putU32(&chunks, static_cast<quint32>(tiff.size()), false);
  chunks += tiff;
  if (tiff.size() % 2 != 0) {
    chunks.append('\0');
  }
  QByteArray bytes("RIFF");
  putU32(&bytes, static_cast<quint32>(chunks.size() + 4), false);
  bytes.append("WEBP");
  return bytes + chunks;
}

QString valueOf(const QVector<MetadataEntry> &entries, const QString &group, const QString &key) {
  for (const auto &entry : entries) {
    if (entry.group == group && entry.key == key) {
      return entry.value;
    }
  }
  return QStringLiteral("<none>");
}

QVector<Entry> sampleIfd0(bool bigEndian) {
  return {ascii(0x010F, "ACME"), ascii(0x0110, "Model X"), shortValue(0x0112, 6, bigEndian),
          ascii(0x0131, "Heiran Test 1.0")};
}

QVector<Entry> sampleExif(bool bigEndian) {
  return {rational(0x829A, 1, 250, bigEndian),      rational(0x829D, 28, 10, bigEndian),
          shortValue(0x8827, 400, bigEndian),       ascii(0x9003, "2026:10:01 12:34:56"),
          signedRational(0x9204, -1, 3, bigEndian), shortValue(0x9209, 1, bigEndian),
          rational(0x920A, 50, 1, bigEndian),       shortValue(0xA001, 1, bigEndian)};
}

QVector<Entry> sampleGps(bool bigEndian) {
  return {ascii(0x0001, "N"), rationalTriple(0x0002, 35, 39, 29, bigEndian), ascii(0x0003, "E"),
          rationalTriple(0x0004, 139, 44, 28, bigEndian)};
}

}  // namespace

class ExifReaderTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ビッグ / リトルエンディアンの両方で、同じ内容が読めること。
  void readsTheMainTagsInBothByteOrders() {
    for (const bool bigEndian : {true, false}) {
      const QByteArray tiff =
          buildTiff(bigEndian, sampleIfd0(bigEndian), sampleExif(bigEndian), sampleGps(bigEndian));
      const auto entries = readExifMetadata(tiff);

      QCOMPARE(valueOf(entries, "Tags", "Make"), QString("ACME"));
      QCOMPARE(valueOf(entries, "Tags", "Model"), QString("Model X"));
      QCOMPARE(valueOf(entries, "Tags", "Software"), QString("Heiran Test 1.0"));
      QCOMPARE(valueOf(entries, "Tags", "Orientation"), QString("6 (Rotate 90 CW)"));
      QCOMPARE(valueOf(entries, "EXIF", "ExposureTime"), QString("1/250 s"));
      QCOMPARE(valueOf(entries, "EXIF", "FNumber"), QString("f/2.8"));
      QCOMPARE(valueOf(entries, "EXIF", "ISOSpeedRatings"), QString("400"));
      QCOMPARE(valueOf(entries, "EXIF", "DateTimeOriginal"), QString("2026:10:01 12:34:56"));
      QCOMPARE(valueOf(entries, "EXIF", "FocalLength"), QString("50 mm"));
      QCOMPARE(valueOf(entries, "EXIF", "Flash"), QString("1 (fired)"));
      QCOMPARE(valueOf(entries, "EXIF", "ColorSpace"), QString("1 (sRGB)"));
      QVERIFY2(valueOf(entries, "EXIF", "ExposureBiasValue").startsWith("-0.33333"),
               qPrintable(valueOf(entries, "EXIF", "ExposureBiasValue")));
      // GPS は十進の度へ。35 度 39 分 29 秒 = 35.658056 度。
      QCOMPARE(valueOf(entries, "GPS", "GPSLatitudeRef"), QString("N"));
      QVERIFY2(valueOf(entries, "GPS", "GPSLatitude").startsWith("35.658"),
               qPrintable(valueOf(entries, "GPS", "GPSLatitude")));
      QVERIFY2(valueOf(entries, "GPS", "GPSLongitude").startsWith("139.741"),
               qPrintable(valueOf(entries, "GPS", "GPSLongitude")));
    }
  }

  // 露出時間の書き方。1 秒より長ければ秒、短ければ分数。
  void formatsTheExposureTime() {
    const auto exposure = [](quint32 numerator, quint32 denominator) {
      const auto entries = readExifMetadata(
          buildTiff(false, {}, {rational(0x829A, numerator, denominator, false)}, {}));
      return valueOf(entries, "EXIF", "ExposureTime");
    };
    QCOMPARE(exposure(1, 250), QString("1/250 s"));
    QCOMPARE(exposure(1, 8000), QString("1/8000 s"));
    QCOMPARE(exposure(2, 1), QString("2 s"));
    QCOMPARE(exposure(5, 2), QString("2.5 s"));
    QCOMPARE(exposure(0, 1), QString("0"));
    // 割り切れない分数は、そのまま。
    QCOMPARE(exposure(3, 10), QString("3/10 s"));
  }

  // 知らないタグは出さない。分母が 0 の有理数は、落ちずに "-"。
  void ignoresUnknownTagsAndBadRationals() {
    const auto entries =
        readExifMetadata(buildTiff(false, {ascii(0xFFFF, "unknown"), ascii(0x010F, "Maker")},
                                   {rational(0x829D, 1, 0, false)}, {}));

    QCOMPARE(valueOf(entries, "Tags", "Make"), QString("Maker"));
    QCOMPARE(entries.size(), 2);  // Make と FNumber だけ。
    QCOMPARE(valueOf(entries, "EXIF", "FNumber"), QString("f/-"));
  }

  // ---- 入れ物 ----

  void readsExifFromAJpeg() {
    const QByteArray tiff = buildTiff(true, sampleIfd0(true), sampleExif(true), {});
    QBuffer buffer;
    buffer.setData(jpegWithExif(tiff));
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    const QByteArray block = extractExifBlock(buffer);

    QCOMPARE(block, tiff);
    QCOMPARE(valueOf(readExifMetadata(block), "Tags", "Make"), QString("ACME"));
  }

  void readsExifFromAPng() {
    const QByteArray tiff = buildTiff(false, sampleIfd0(false), {}, {});
    QBuffer buffer;
    buffer.setData(pngWithExif(tiff));
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QCOMPARE(extractExifBlock(buffer), tiff);
  }

  void readsExifFromAWebp() {
    const QByteArray tiff = buildTiff(false, sampleIfd0(false), {}, {});
    QBuffer buffer;
    buffer.setData(webpWithExif(tiff));
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QCOMPARE(extractExifBlock(buffer), tiff);
  }

  // 入れ物が EXIF を持たなければ、空。落ちない。
  void returnsNothingWhenThereIsNoExif() {
    QByteArray bare;
    bare.append("\xFF\xD8", 2);
    bare.append("\xFF\xDA\x00\x02", 4);
    QBuffer jpeg;
    jpeg.setData(bare);
    QVERIFY(jpeg.open(QIODevice::ReadOnly));
    QVERIFY(extractExifBlock(jpeg).isEmpty());

    QBuffer garbage;
    garbage.setData(QByteArray(200, 'z'));
    QVERIFY(garbage.open(QIODevice::ReadOnly));
    QVERIFY(extractExifBlock(garbage).isEmpty());

    QVERIFY(readExifMetadata(QByteArray()).isEmpty());
    QVERIFY(readExifMetadata(QByteArray("not a tiff block at all")).isEmpty());
  }

  // 壊れた入力 (途中で切れる、自分を指す IFD、巨大な個数) で、落ちず、止まること。
  void survivesBrokenBlocks() {
    const QByteArray good = buildTiff(false, sampleIfd0(false), sampleExif(false), {});
    for (int length = 0; length < good.size(); length += 7) {
      readExifMetadata(good.left(length));  // 結果は問わない。落ちなければよい。
    }

    // 先頭の IFD が、自分自身を下位の IFD として指す。
    QByteArray loop("II");
    putU16(&loop, 42, false);
    putU32(&loop, 8, false);
    putU16(&loop, 1, false);
    putU16(&loop, 0x8769, false);
    putU16(&loop, 4, false);
    putU32(&loop, 1, false);
    putU32(&loop, 8, false);  // 自分の位置
    putU32(&loop, 0, false);
    QVERIFY(readExifMetadata(loop).isEmpty());

    // 値の個数が巨大。
    QByteArray huge("II");
    putU16(&huge, 42, false);
    putU32(&huge, 8, false);
    putU16(&huge, 1, false);
    putU16(&huge, 0x010F, false);
    putU16(&huge, 2, false);
    putU32(&huge, 0xFFFFFFF0u, false);
    putU32(&huge, 100, false);
    putU32(&huge, 0, false);
    QVERIFY(readExifMetadata(huge).isEmpty());
  }

  // ---- ファイル ----

  void readsMetadataFromFilesOfEachContainer() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray tiff = buildTiff(true, sampleIfd0(true), sampleExif(true), sampleGps(true));
    const auto write = [&directory](const QString &name, const QByteArray &bytes) {
      const QString path = QDir(directory.path()).filePath(name);
      QFile file(path);
      file.open(QIODevice::WriteOnly);
      file.write(bytes);
      return path;
    };

    for (const auto &path : {write("a.jpg", jpegWithExif(tiff)), write("a.png", pngWithExif(tiff)),
                             write("a.webp", webpWithExif(tiff)), write("a.tif", tiff)}) {
      const auto entries = readFileMetadata(path);
      QCOMPARE(valueOf(entries, "Tags", "Make"), QString("ACME"));
      QCOMPARE(valueOf(entries, "EXIF", "ISOSpeedRatings"), QString("400"));
    }
    QVERIFY(readFileMetadata(QDir(directory.path()).filePath("missing.jpg")).isEmpty());
  }
};

QTEST_MAIN(ExifReaderTests)

#include "ExifReaderTests.moc"
