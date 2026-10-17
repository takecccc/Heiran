#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

namespace heiran::ui {

struct ApplyDroppedItemsResult {
  bool changed = false;
  // ドロップした枚数に合わせて格子を広げた。呼び側は、表示を合わせ直す必要がある。
  bool layoutChanged = false;
  bool preserveView = false;
  int targetPage = 0;
  QString statusMessage;
};

class SessionMutationController {
public:
  // 1 度のドロップで格子を広げてよいセルの数の上限。数十枚をドロップして、同時に
  // 数十枚を読み込む格子になるのを避ける。超えた分は無視して知らせる。
  static constexpr int kMaxCellsGrownByDrop = 16;

  static ApplyDroppedItemsResult applyDroppedItems(
      heiran::domain::SessionModel *session, QVector<heiran::domain::CellConfig> *cellLayoutCache,
      int targetCellIndex, const QStringList &paths, int targetDisplayPage);

  static bool applyLabelVisibility(heiran::domain::SessionModel *session, int cellIndex,
                                   bool visible);
  static bool applyLabelText(heiran::domain::SessionModel *session, int cellIndex,
                             const QString &label);
  static bool assignImageIndexToCell(heiran::domain::SessionModel *session,
                                     QVector<heiran::domain::CellConfig> *cellLayoutCache,
                                     int cellIndex, int imageIndex);
  // 全セルの割り当てを外す。画像セットは残す。あとから右クリックの Assign Image で
  // 選び直せるようにするため。何も外れなければ false を返す。
  static bool clearAllCells(heiran::domain::SessionModel *session,
                            QVector<heiran::domain::CellConfig> *cellLayoutCache);
  // 画像セット管理ダイアログの結果を反映する。セルの倍率は、ダイアログが編集した値のまま
  // (画像セットの既定の倍率との揃えは、ダイアログの表の編集で行う)。画像セットのパスは解決し直す。
  static void applyImageSetEdit(heiran::domain::SessionModel *session,
                                const QVector<heiran::domain::ImageSet> &images,
                                const QVector<heiran::domain::CellConfig> &cells);
};

}  // namespace heiran::ui
