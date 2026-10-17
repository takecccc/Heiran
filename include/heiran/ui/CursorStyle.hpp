#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace heiran::ui {

// 共有カーソル (十字) の描き方。
enum class CursorStyle {
  // 決まった色の線に、暗い縁を付ける。
  Outline,
  // カーソルの下の画素の色から離れた色を選ぶ。縁は付けない。
  Contrast,
};

QString cursorStyleToString(CursorStyle style);
CursorStyle cursorStyleFromString(const QString &text);

// 指している周りの画素の色 (`samples`、表示している色) から、離れて見える色を選ぶ。
// 単純な反転は、中間のグレーで元の色と同じになって見えなくなる。そのため、白、黒と、
// 彩度の高い 6 色 (赤、緑、青、黄、シアン、マゼンタ) の中から、知覚的な色差 (CIELAB の ΔE) が
// 最も離れるものを選ぶ。どの画素からも離れている色が良いので、各候補の、最も近い画素との色差で比べる。
// `samples` が空なら、既定の色 (黄) を返す。
QColor chooseContrastColor(const QVector<QRgb> &samples);

// 2 色の知覚的な色差 (CIELAB の ΔE76)。選んだ色が離れているかの確認に使う。
double colorDifference(QRgb first, QRgb second);

}  // namespace heiran::ui
