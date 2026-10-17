#include "heiran/infra/EmbeddedColorProfile.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Qt に書かせた ICC プロファイル。実在する形のものを使う。
QByteArray namedIccProfile(QColorSpace::NamedColorSpace named) {
  return QColorSpace(named).iccProfile();
}

QByteArray beUInt32(quint32 value) {
  QByteArray bytes(4, '\0');
  bytes[0] = static_cast<char>((value >> 24) & 0xFF);
  bytes[1] = static_cast<char>((value >> 16) & 0xFF);
  bytes[2] = static_cast<char>((value >> 8) & 0xFF);
  bytes[3] = static_cast<char>(value & 0xFF);
  return bytes;
}

// PNG のチャンクを組み立てる。CRC は読み側が見ないため 0 で埋める。
QByteArray pngChunk(const QByteArray &type, const QByteArray &content) {
  return beUInt32(static_cast<quint32>(content.size())) + type + content + beUInt32(0);
}

// 最小の PNG。チャンクを差し込む土台として使う。
QByteArray pngWithChunks(const QByteArray &extraChunks) {
  QByteArray png("\x89PNG\r\n\x1a\n", 8);
  QByteArray header = beUInt32(1) + beUInt32(1);
  header.append(static_cast<char>(8));  // bit depth
  header.append(static_cast<char>(2));  // color type
  header.append(3, '\0');               // compression / filter / interlace
  png.append(pngChunk("IHDR", header));
  png.append(extraChunks);
  png.append(pngChunk("IDAT", QByteArray(8, '\0')));
  png.append(pngChunk("IEND", QByteArray()));
  return png;
}

// JPEG の APP2 へ ICC を分割して差し込む。
QByteArray jpegWithIccProfile(const QByteArray &iccProfile, int chunkCount) {
  QByteArray jpeg("\xFF\xD8", 2);

  const int chunkSize = (iccProfile.size() + chunkCount - 1) / chunkCount;
  for (int index = 0; index < chunkCount; ++index) {
    QByteArray payload("ICC_PROFILE\0", 12);
    payload.append(static_cast<char>(index + 1));
    payload.append(static_cast<char>(chunkCount));
    payload.append(iccProfile.mid(index * chunkSize, chunkSize));

    const int segmentLength = payload.size() + 2;
    jpeg.append(static_cast<char>(0xFF));
    jpeg.append(static_cast<char>(0xE2));
    jpeg.append(static_cast<char>((segmentLength >> 8) & 0xFF));
    jpeg.append(static_cast<char>(segmentLength & 0xFF));
    jpeg.append(payload);
  }

  jpeg.append(QByteArray("\xFF\xDA", 2));  // SOS
  return jpeg;
}

}  // namespace

class EmbeddedColorProfileTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 埋め込みが無ければ空を返すこと。
  void reportsNothingWhenThereIsNoProfile() {
    const auto profile = heiran::infra::readEmbeddedColorProfile(pngWithChunks(QByteArray()));
    QVERIFY(profile.isEmpty());
    QVERIFY(!profile.resolve().isValid());
  }

  // PNG の iCCP から ICC プロファイルを取り出せること。中身は zlib で圧縮されている。
  void readsTheIccProfileFromPng() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::DisplayP3);
    QVERIFY(!iccProfile.isEmpty());

    // qCompress は先頭 4 バイトに大きさを付けるため、そこを外して zlib 部分だけ使う。
    QByteArray content("profile", 7);
    content.append('\0');
    content.append('\0');  // 圧縮方式
    content.append(qCompress(iccProfile).mid(4));

    const auto profile =
        heiran::infra::readEmbeddedColorProfile(pngWithChunks(pngChunk("iCCP", content)));
    QCOMPARE(profile.iccProfile, iccProfile);

    const QColorSpace resolved = profile.resolve();
    QVERIFY(resolved.isValid());
    QCOMPARE(resolved.primaries(), QColorSpace::Primaries::DciP3D65);
  }

  // PNG の sRGB チャンクを申告として読むこと。
  void readsTheSrgbDeclarationFromPng() {
    QByteArray content;
    content.append(static_cast<char>(0));  // rendering intent
    const auto profile =
        heiran::infra::readEmbeddedColorProfile(pngWithChunks(pngChunk("sRGB", content)));

    QVERIFY(profile.iccProfile.isEmpty());
    QVERIFY(profile.declaredColorSpace.isValid());
    QCOMPARE(profile.resolve().primaries(), QColorSpace::Primaries::SRgb);
    QCOMPARE(profile.resolve().transferFunction(), QColorSpace::TransferFunction::SRgb);
  }

  // cHRM と gAMA から色空間を組み立てられること。
  void buildsAColorSpaceFromChrmAndGama() {
    // Display P3 の色度。値は 100000 倍の整数で入れる。
    QByteArray chrm;
    chrm.append(beUInt32(31270)).append(beUInt32(32900));  // 白色点
    chrm.append(beUInt32(68000)).append(beUInt32(32000));  // 赤
    chrm.append(beUInt32(26500)).append(beUInt32(69000));  // 緑
    chrm.append(beUInt32(15000)).append(beUInt32(6000));   // 青

    // gAMA は「ファイルの値 -> 線形」の指数。1/2.2 を 100000 倍で入れる。
    const QByteArray gama = beUInt32(45455);

    const auto profile = heiran::infra::readEmbeddedColorProfile(
        pngWithChunks(pngChunk("cHRM", chrm) + pngChunk("gAMA", gama)));

    QVERIFY(profile.iccProfile.isEmpty());
    const QColorSpace resolved = profile.resolve();
    QVERIFY(resolved.isValid());

    // 同じ色度から組み立てた色空間と一致すること。
    // `primaryPoints()` の戻りは項目がずれて返るため、比較には使わない
    // (Qt 6.11.1 で確認。docs/設計判断.md に記録)。
    const QColorSpace expected(QColorSpace::PrimaryPoints{.whitePoint = {0.3127, 0.3290},
                                                          .redPoint = {0.680, 0.320},
                                                          .greenPoint = {0.265, 0.690},
                                                          .bluePoint = {0.150, 0.060}},
                               QColorSpace::TransferFunction::Gamma, 2.2F);
    QCOMPARE(resolved, expected);

    // 色度が実際に効いていること。sRGB の原色とは別物になる。
    QVERIFY(resolved !=
            QColorSpace(QColorSpace::Primaries::SRgb, QColorSpace::TransferFunction::Gamma, 2.2F));
    QCOMPARE(resolved.transferFunction(), QColorSpace::TransferFunction::Gamma);
    QVERIFY2(std::abs(resolved.gamma() - 2.2F) < 0.01F,
             qPrintable(QString::number(resolved.gamma())));
  }

  // `QColorSpace::primaryPoints()` の戻りが項目 1 つ分ずれて返ること。
  // 自前の解析の誤りと取り違えないよう、Qt 側の挙動として固定しておく。
  // 直った場合はこのテストが落ちるので、そこで解析側の比較を見直せる。
  void documentsThatQtRotatesPrimaryPoints() {
    const auto points = QColorSpace(QColorSpace::DisplayP3).primaryPoints();
    // 本来は redPoint が (0.680, 0.320) になるはずのところ、緑が返る。
    QVERIFY2(
        std::abs(points.redPoint.x() - 0.265) < 0.001,
        qPrintable(
            QString("Qt may have fixed primaryPoints(): red.x = %1").arg(points.redPoint.x())));
  }

  // iCCP がある場合は cHRM より優先すること。ICC の方が強い申告になる。
  void prefersTheIccProfileOverChrm() {
    QByteArray chrm;
    chrm.append(beUInt32(31270)).append(beUInt32(32900));
    chrm.append(beUInt32(64000)).append(beUInt32(33000));
    chrm.append(beUInt32(30000)).append(beUInt32(60000));
    chrm.append(beUInt32(15000)).append(beUInt32(6000));

    const QByteArray iccProfile = namedIccProfile(QColorSpace::Bt2020);
    QByteArray content("profile", 7);
    content.append('\0');
    content.append('\0');
    content.append(qCompress(iccProfile).mid(4));

    const auto profile = heiran::infra::readEmbeddedColorProfile(
        pngWithChunks(pngChunk("cHRM", chrm) + pngChunk("iCCP", content)));

    QCOMPARE(profile.resolve().primaries(), QColorSpace::Primaries::Bt2020);
  }

  // JPEG の APP2 から取り出せること。1 つの区画に収まる場合。
  void readsTheIccProfileFromJpeg() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::AdobeRgb);
    const auto profile = heiran::infra::readEmbeddedColorProfile(jpegWithIccProfile(iccProfile, 1));

    QCOMPARE(profile.iccProfile, iccProfile);
    QCOMPARE(profile.resolve().primaries(), QColorSpace::Primaries::AdobeRgb);
  }

  // 64 KiB を超えるプロファイルは複数の APP2 へ分割される。並べ直して繋ぐこと。
  void reassemblesASplitJpegProfile() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::DisplayP3);
    const auto profile = heiran::infra::readEmbeddedColorProfile(jpegWithIccProfile(iccProfile, 4));

    QCOMPARE(profile.iccProfile, iccProfile);
    QCOMPARE(profile.resolve().primaries(), QColorSpace::Primaries::DciP3D65);
  }

  // 断片が欠けている場合は繋がないこと。中途半端なプロファイルは使えない。
  void rejectsAnIncompleteJpegProfile() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::DisplayP3);
    QByteArray jpeg = jpegWithIccProfile(iccProfile, 3);

    // 2 番目の区画を落とす。区画の並びを崩さないよう、印を APP2 以外へ変える。
    const int second = jpeg.indexOf(QByteArray("ICC_PROFILE\0", 12),
                                    jpeg.indexOf(QByteArray("ICC_PROFILE\0", 12)) + 1);
    QVERIFY(second > 0);
    jpeg[second - 3] = static_cast<char>(0xE5);

    const auto profile = heiran::infra::readEmbeddedColorProfile(jpeg);
    QVERIFY(profile.iccProfile.isEmpty());
  }

  // TIFF のタグ 34675 から取り出せること。
  void readsTheIccProfileFromTiff() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::Bt2020);

    QByteArray tiff("II", 2);
    tiff.append(QByteArray::fromHex("2a00"));      // 42
    tiff.append(QByteArray::fromHex("08000000"));  // IFD0 は 8 バイト目から
    tiff.append(QByteArray::fromHex("0100"));      // 項目は 1 つ

    const int valueOffset = 8 + 2 + 12 + 4;
    tiff.append(QByteArray::fromHex("7387"));  // タグ 34675 (0x8773) のリトルエンディアン
    tiff.append(QByteArray::fromHex("0700"));  // 型 UNDEFINED
    QByteArray countBytes(4, '\0');
    const quint32 count = static_cast<quint32>(iccProfile.size());
    countBytes[0] = static_cast<char>(count & 0xFF);
    countBytes[1] = static_cast<char>((count >> 8) & 0xFF);
    countBytes[2] = static_cast<char>((count >> 16) & 0xFF);
    countBytes[3] = static_cast<char>((count >> 24) & 0xFF);
    tiff.append(countBytes);
    QByteArray offsetBytes(4, '\0');
    offsetBytes[0] = static_cast<char>(valueOffset & 0xFF);
    offsetBytes[1] = static_cast<char>((valueOffset >> 8) & 0xFF);
    tiff.append(offsetBytes);
    tiff.append(QByteArray::fromHex("00000000"));  // 次の IFD は無い
    tiff.append(iccProfile);

    const auto profile = heiran::infra::readEmbeddedColorProfile(tiff);
    QCOMPARE(profile.iccProfile, iccProfile);
    QCOMPARE(profile.resolve().primaries(), QColorSpace::Primaries::Bt2020);
  }

  // WebP の RIFF から取り出せること。
  void readsTheIccProfileFromWebp() {
    const QByteArray iccProfile = namedIccProfile(QColorSpace::AdobeRgb);

    QByteArray payload("WEBP", 4);
    QByteArray chunk("ICCP", 4);
    QByteArray lengthBytes(4, '\0');
    const quint32 length = static_cast<quint32>(iccProfile.size());
    lengthBytes[0] = static_cast<char>(length & 0xFF);
    lengthBytes[1] = static_cast<char>((length >> 8) & 0xFF);
    lengthBytes[2] = static_cast<char>((length >> 16) & 0xFF);
    lengthBytes[3] = static_cast<char>((length >> 24) & 0xFF);
    chunk.append(lengthBytes);
    chunk.append(iccProfile);
    if (iccProfile.size() % 2 != 0) {
      chunk.append('\0');
    }
    payload.append(chunk);

    QByteArray webp("RIFF", 4);
    QByteArray sizeBytes(4, '\0');
    const quint32 riffSize = static_cast<quint32>(payload.size());
    sizeBytes[0] = static_cast<char>(riffSize & 0xFF);
    sizeBytes[1] = static_cast<char>((riffSize >> 8) & 0xFF);
    sizeBytes[2] = static_cast<char>((riffSize >> 16) & 0xFF);
    sizeBytes[3] = static_cast<char>((riffSize >> 24) & 0xFF);
    webp.append(sizeBytes);
    webp.append(payload);

    const auto profile = heiran::infra::readEmbeddedColorProfile(webp);
    QCOMPARE(profile.iccProfile, iccProfile);
  }

  // 復号結果が色空間を持つこと。埋め込みが無い 8bit 画像は sRGB として扱う。
  void decoderReportsSrgbForAPlainImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    const QString filePath = QDir(directory.path()).filePath("plain.png");
    QVERIFY(image.save(filePath));

    const heiran::infra::ImageDecoder decoder;
    const auto result = decoder.decode(filePath, true);
    QVERIFY(result.ok);
    QVERIFY(result.decodedImage.colorSpace.isValid());
    QCOMPARE(result.decodedImage.colorSpace.primaries(), QColorSpace::Primaries::SRgb);
  }

  // 埋め込みプロファイルのある画像では、そちらが使われること。
  void decoderUsesTheEmbeddedProfile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    const QString filePath = QDir(directory.path()).filePath("p3.png");
    QVERIFY(image.save(filePath));

    const heiran::infra::ImageDecoder decoder;
    const auto result = decoder.decode(filePath, true);
    QVERIFY(result.ok);
    QVERIFY2(!result.decodedImage.embeddedColorProfile.isEmpty(),
             "the written PNG carried no color information");
    QCOMPARE(result.decodedImage.colorSpace.primaries(), QColorSpace::Primaries::DciP3D65);
  }
};

QTEST_MAIN(EmbeddedColorProfileTests)

#include "EmbeddedColorProfileTests.moc"
