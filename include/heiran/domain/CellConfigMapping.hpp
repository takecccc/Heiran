#pragma once

#include "heiran/domain/SessionModel.hpp"
#include "heiran/domain/ViewState.hpp"

namespace heiran::domain {

// セルの設定。割り当てが無い、または範囲外のセルには既定の設定を返す。
CellConfig cellConfigAt(const SessionModel &session, int cellIndex);

// 設定の表示に関わる値 (拡大率、ガンマ、回転、反転、白黒反転) を状態へ写す。
// 同期、パンのずれ、EXIF の向きは触らない。リセットはこれだけを使う。
void applyCellConfigDisplayFields(const CellConfig &config, CellViewState *state);

// セッションの内容でセルの状態を作り直す。パンのずれは触らない。
// 同期と、割り当てた画像セットの EXIF の向きの扱いも含めて写す。
void applyCellConfigToViewState(const SessionModel &session, int cellIndex, CellViewState *state);

// applyCellConfigToViewState の逆。保存する設定へ現在の状態を書き戻す。
// 割り当てとチャネルは状態の側に無いため触らない。
void writeCellViewStateToConfig(const CellViewState &state, CellConfig *config);

}  // namespace heiran::domain
