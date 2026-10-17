#pragma once

#include <QString>
#include <QVector>

namespace heiran::infra {

// ファイルに入っているメタデータの 1 項目。
struct MetadataEntry {
  // 分類。`EXIF`、`GPS`、IFD0 の `Tags`、EXR の `EXR`。
  QString group;
  QString key;
  QString value;
};

// ファイルからメタデータを読む。画素は復号しない。
//
// JPEG、PNG、WebP、TIFF は EXIF (`ExifReader`)、EXR はヘッダーの属性 (`readExrMetadata`)。
// 読めない形式、メタデータを持たないファイルは空を返す (画像の表示は、これと無関係に成り立つ)。
QVector<MetadataEntry> readFileMetadata(const QString &filePath);

}  // namespace heiran::infra
