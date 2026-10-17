#pragma once

#include <vector>

namespace heiran::domain {

struct GlobalViewState {
  double baseScale = 1.0;
  double baseOffsetX = 0.0;
  double baseOffsetY = 0.0;
  int currentPage = 0;
};

struct CellViewState {
  bool syncEnabled = true;
  double offsetDeltaX = 0.0;
  double offsetDeltaY = 0.0;
  double scale = 1.0;
  double gamma = 1.0;
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
  // 表示値の白黒反転。露出とガンマを適用して丸めた後の値を反転する。
  bool invert = false;
  bool honorExifOrientation = true;
};

struct FinalViewState {
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
};

FinalViewState computeFinalViewState(const GlobalViewState &globalState,
                                     const CellViewState &cellState);

// 画像座標を表示座標へ写す。
// view = (image - offset) * scale
double imageToViewX(const FinalViewState &finalState, double imageX);
double imageToViewY(const FinalViewState &finalState, double imageY);

// 表示座標を画像座標へ戻す。
// image = view / scale + offset
double viewToImageX(const FinalViewState &finalState, double viewX);
double viewToImageY(const FinalViewState &finalState, double viewY);

// あるセルの画像座標を、別のセルで同じ表示位置を指す画像座標へ写す。
//
// セルごとに拡大率とオフセットが違うため、同じ画像座標が画面上の同じ場所を
// 指すとは限らない。表示座標を経由して写し直す。選択範囲の重ね描き、
// カーソルの同期、解析対象の切り出しがこれを使う。
//
// どちらかの拡大率が 0 以下なら写しようがない。その場合は元の値を返す。
double mapImageXBetweenCells(const FinalViewState &referenceState,
                             const FinalViewState &targetState, double imageX);
double mapImageYBetweenCells(const FinalViewState &referenceState,
                             const FinalViewState &targetState, double imageY);

// セルにフィットさせる計算の入力。1 セル分の表示対象を表す。
struct FitTarget {
  int imageWidth = 0;
  int imageHeight = 0;
  // セルごとの個別拡大率。
  double cellScale = 1.0;
};

struct FitViewResult {
  bool valid = false;
  double baseScale = 1.0;
  double baseOffsetX = 0.0;
  double baseOffsetY = 0.0;
};

// 全ての対象がセル内に収まる基準拡大率と基準オフセットを求める。
// 最も制約の強い対象に合わせるため、他の対象も必ず収まる。
// オフセットはその対象を中央に置く位置とする。
FitViewResult computeFitView(const std::vector<FitTarget> &targets, int cellWidth, int cellHeight);

void applyPanToGlobal(GlobalViewState *globalState, double deltaX, double deltaY);
void applyPanToCell(CellViewState *cellState, double deltaX, double deltaY);
void applyZoom(GlobalViewState *globalState, double zoomFactor, double anchorImageX,
               double anchorImageY);

}  // namespace heiran::domain
