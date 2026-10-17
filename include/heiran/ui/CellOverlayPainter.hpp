#pragma once

#include "heiran/ui/CursorStyle.hpp"
#include <QColor>

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <optional>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace heiran::ui {

// セルの上へ重ねる表示の入力。
//
// 画像そのものは GPU が描く。ここで扱うのは選択範囲、共有カーソル、ラベルの
// 3 つで、いずれも `QPainter` で画像の上へ乗せる。
//
// 画像座標から表示座標への写しに要る値だけを受け取る。ウィジェットの状態を
// 直接見ないため、描いた結果をテストで確かめられる。
struct CellOverlayInput {
  // 最終的な表示倍率と、画像座標での表示原点。
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
  // 描画先の大きさ。ラベルの省略幅と、選択範囲のはみ出し止めに使う。
  // 空のままだと選択範囲を切り取らない。
  QSize canvasSize;

  // 確定した選択範囲と、ドラッグ中の仮の選択範囲。画像座標で持つ。
  std::optional<QRectF> selectionRect;
  std::optional<QRectF> previewSelectionRect;

  // 全セルで共有するカーソル位置。画像座標で持つ。
  std::optional<QPointF> sharedCursorPosition;
  // カーソルの描き方。`Contrast` は、カーソルの周りの画素の色を `sampleImage` (画像座標で引く、
  // 表示している色の画像) から取って、離れた色を選ぶ。画像が無ければ `Outline` と同じ色になる。
  CursorStyle cursorStyle = CursorStyle::Outline;
  const QImage *sampleImage = nullptr;

  // ラベル。`labelRect` が空なら描かない。
  QString labelText;
  QRect labelRect;
};

// 重ねる表示を描く。`painter` の状態は呼び出し前後で変わらない。
void paintCellOverlays(QPainter *painter, const CellOverlayInput &input);

}  // namespace heiran::ui
