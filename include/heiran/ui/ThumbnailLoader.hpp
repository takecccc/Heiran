#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace heiran::ui {

// 一覧表示のためのサムネイルを作る。
//
// 表示に要る大きさだけを読む。ビューアの経路では間引かない (現在ページも先読みも
// 元解像度が要る) が、一覧では低い解像度で足りて、画素値を答える必要も無い。
//
// **形式ごとに最も速い経路が違う。** 一律に間引きへ通すと逆効果になる。
// 3840x2160 での計測 (2026-09-04):
//
// - JPEG: `QImageReader::setScaledSize()` が DCT の段階で効く。
//   元解像度 46.7 ms に対し 1/8 で 7.1 ms。
// - PNG: `setScaledSize()` は 35 ms で、libspng で元解像度を読む 19.8 ms より遅い。
//   読んでから縮小するほうが速い。
//
// 縦横比は保つ。`maximumSize` に収まる最大の大きさへ縮める。元が小さい場合は
// 引き伸ばさない。
//
// 色空間は表示先 (sRGB) へ合わせる。線形で持つ EXR をそのまま縮めると暗く出る。
QImage loadThumbnail(const QString &filePath, const QSize &maximumSize,
                     bool honorExifOrientation = true);

}  // namespace heiran::ui
