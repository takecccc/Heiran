#pragma once

#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/TransparencyBackground.hpp"

#include <QPoint>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace heiran::ui {

// セルの右クリックメニュー。
//
// メニューの組み立てと選択結果の解釈をキャンバスから分ける。キャンバスは
// 現在の状態を渡して結果を受け取り、対応する要求を送るだけにする。
// 表示中の状態に応じてチェックを付けるため、状態は一括で渡す。

struct CellContextMenuState {
  bool favorite = false;
  bool favoriteAvailable = false;
  TransparencyPattern transparencyPattern = TransparencyPattern::Solid;
  bool syncEnabled = true;
  double scale = 1.0;
  double gamma = 1.0;
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
  bool invert = false;
  ChannelMode channelMode = ChannelMode::Color;
  // 画像セットが割り当たっていない場合、ラベル関連の項目は選べない。
  bool hasImageSet = false;
  bool labelVisible = true;
  int imageSetIndex = -1;
  QStringList availableImageNames;
  // 画像を持たないセルでは、コピーとフォルダを開く項目を選べない。
  bool hasImage = false;
  // 差分の項目。全セルの名前 (番号の順) と、このセルの番号、いま差分を出しているか。
  QStringList cellLabels;
  int cellIndex = -1;
  bool diffActive = false;
  // 出している差分が、ワイプか (項目の名前を変える)。
  bool diffIsWipe = false;
};

struct CellContextMenuResult {
  enum class Action {
    None,
    ToggleFavorite,
    SetTransparencyPattern,
    ToggleSync,
    SetScale,
    SetGamma,
    SetRotation,
    SetFlip,
    SetInvert,
    SetLabelVisible,
    EditLabel,
    AssignImageIndex,
    ResetCellDisplay,
    SetChannelMode,
    CopyImageToClipboard,
    ShowInFolder,
    // このセルへ、選んだセルを基準にした差分を出す。
    StartDiff,
    // このセルへ、選んだセルを基準 (左 / 上) にしたワイプを出す。
    StartWipe,
    StopDiff,
  };

  Action action = Action::None;
  TransparencyPattern transparencyPattern = TransparencyPattern::Solid;
  double scale = 1.0;
  double gamma = 1.0;
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
  bool invert = false;
  bool labelVisible = true;
  int imageIndex = -1;
  ChannelMode channelMode = ChannelMode::Color;
  int diffBaseCell = -1;
};

// メニューを出し、選ばれた項目を返す。Custom... の入力ダイアログもここで出す。
// 何も選ばれなかった場合は Action::None を返す。
CellContextMenuResult showCellContextMenu(QWidget *parent, const QPoint &globalPosition,
                                          const CellContextMenuState &state);

}  // namespace heiran::ui
