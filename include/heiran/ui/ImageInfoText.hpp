#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QSize>
#include <QString>

namespace heiran::ui {

// ファイルの大きさを、読みやすい単位で。1024 進。B、KB、MB、GB。B 以外は小数 1 桁。
// 負の値 (不明) は空を返す。
QString formatFileSizeText(qint64 bytes);

// 画素の形式の表示。色のモデルと 1 チャネルあたりのビット数。
// `Gray8`、`GrayA8`、`RGB8`、`RGBA8`、`RGB16`、`RGBA16`、`RGB16F` (half float)、`RGB32F`。
// 不明なら空。ファイルの拡張子ではなく、読んだ画素そのものの形式。
QString pixelFormatText(const heiran::infra::DecodedImage &decodedImage);

// セルの見出しに出す、画像の情報。`RGB8 · 4000×3000 · 12.3 MB`。
// 大きさが空なら出さない。画素の形式やファイルの大きさが不明なら、その項目を省く。
QString formatImageInfoText(const QString &pixelFormat, const QSize &size, qint64 fileBytes);

}  // namespace heiran::ui
