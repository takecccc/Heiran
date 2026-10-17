#pragma once

#include "heiran/infra/ImageMetadata.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

class QIODevice;

namespace heiran::infra {

// EXIF (TIFF の IFD の並び) を読む。
//
// 対応するのは、EXIF を持てる入れ物のうち、JPEG (APP1)、PNG (eXIf)、WebP (EXIF)、TIFF
// (ファイルそのもの)。入れ物の中の IFD0 (撮影機器、日時、向きなど)、Exif の下位 IFD (露出、
// ISO、レンズなど)、GPS の下位 IFD を読む。サムネイルの IFD1 と、メーカー固有の MakerNote
// は読まない。
//
// 分類は、IFD0 が `Tags`、Exif の下位が `EXIF`、GPS が `GPS`。
// 値は、人が読む形へ整える (露出時間は `1/250`、GPS は十進の度、向きなどは名前を添える)。
// 入れ物が EXIF を持たない、または壊れていれば空を返す。落ちない。
QVector<MetadataEntry> readExifMetadata(QIODevice &device);

// バイト列から読む。テストと、入れ物から取り出した EXIF のブロックを渡すのに使う。
QVector<MetadataEntry> readExifMetadata(const QByteArray &fileContents);

// 入れ物 (JPEG、PNG、WebP) から、EXIF のブロック (TIFF のヘッダーから) を取り出す。
// TIFF ファイルそのものや、EXIF が無い場合は空。
QByteArray extractExifBlock(QIODevice &device);

}  // namespace heiran::infra
