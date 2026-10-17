#pragma once

#include "heiran/ui/CellStatus.hpp"

#include <QRect>
#include <QSize>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace heiran::ui {

// 読込失敗時の再読込ボタンの位置。失敗表示以外では空の矩形を返す。
//
// 描く側と押されたかを判定する側で同じ矩形を使う。別々に計算すると、片方だけ
// 動かしたときに「見えているのに押せない」状態になる。
QRect cellReloadButtonRect(const QSize &canvasSize, CellStatus status);

// 画像を持たないセルの中身を描く。
//
// 空欄は中立色の 1 行、読込失敗は見出しとファイル名と理由と再読込ボタンを出す。
// 見た目を分けるのは、置いていないセルと失敗したセルを取り違えないため。
// `Ready` では何も描かない。
void paintCellStatus(QPainter *painter, const QRect &canvasRect, const CellStatusInfo &statusInfo);

}  // namespace heiran::ui
