#pragma once

#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/infra/ImageMetadata.hpp"

#include <QColorSpace>
#include <QString>
#include <QStringList>
#include <QVector>

namespace heiran::ui {

// 1 つのセルの画像について、表に出す項目 (分類、名前、値)。
struct ImageInfoCell {
  QString label;
  QVector<heiran::infra::MetadataEntry> entries;
};

// 表の 1 行。セルごとの値が並ぶ。
struct ImageInfoRow {
  QString group;
  QString key;
  // セルごとの値 (`cells` と同じ順)。そのセルに無い項目は "-"。
  QStringList values;
  // セルの間で値が違うか。比べるための印。
  bool differs = false;
};

// セルの項目を、セルをまたいだ行にまとめる。
// 行の並びは、項目が初めて現れた順 (最初のセルの項目が先、後のセルだけにある項目はその後ろ)。
// 分類ごとに並べ直す。
QVector<ImageInfoRow> buildImageInfoRows(const QVector<ImageInfoCell> &cells);

// 色空間の説明 (`sRGB`、`Display P3 / PQ` など)。無効なら空。
QString describeColorSpace(const QColorSpace &colorSpace);

// 1 つの画像について、ファイルの情報、画像の情報、色の情報、ファイル内のメタデータを集める。
// `fileMetadata` は `heiran::infra::readFileMetadata()` の結果 (読み込み側で持ち回せるよう、
// ここでは読まない)。
ImageInfoCell describeImage(const QString &label, const QString &filePath,
                            const heiran::infra::DecodedImage &decodedImage,
                            const QVector<heiran::infra::MetadataEntry> &fileMetadata);

}  // namespace heiran::ui
