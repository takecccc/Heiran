#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>
#include <QVector>

namespace heiran::ui {

class SessionLayoutController {
public:
  // 開く画像の枚数に合う格子を返す。列を先に増やすため、横に広い画面へ収まりやすい。
  // 1 枚なら 1x1、2 枚なら 1x2、3 から 4 枚なら 2x2 になる。
  static heiran::domain::ViewerLayout layoutForImageCount(int imageCount);

  // 1 セッションで見せられるセルの数の上限 (.hrn の読み込みの上限と同じ)。
  static constexpr int kMaxCellCount = 16;

  // セルの数がちょうど cellCount になる格子 (行 x 列) を、行の少ない順に返す。例: 6 なら
  // 1x6、2x3、3x2、6x1。 cellCount が 1 未満か、上限を超えるときは、空。
  static QVector<heiran::domain::ViewerLayout> layoutsForCellCount(int cellCount);
  static heiran::domain::ViewerLayout preferredLayoutForCellCount(int cellCount);
  static void rememberLayout(int rows, int cols);

  // 格子を、セルが 1 つ増える向きに広げる。列が 4 に満たなければ列を、4
  // 列ちょうどまでなら行を、増やす。 5 列以上の横長の格子は、列を増やす (帯のまま延ばす)。上限 (16)
  // を超える向きは避け、どちらも超えるなら広げない。 広げられなければ、false。
  static bool growLayout(heiran::domain::ViewerLayout *layout);

  static bool applyLayout(heiran::domain::SessionModel *session,
                          QVector<heiran::domain::CellConfig> *cellLayoutCache, int rows, int cols);
  static bool applyLayoutOrder(heiran::domain::SessionModel *session, const QString &order);
  static bool applyCustomImageOrder(const heiran::domain::SessionModel &session,
                                    const QVector<heiran::domain::CellConfig> &cellLayoutCache,
                                    const QVector<int> &imageIndices,
                                    QVector<heiran::domain::CellConfig> *newCells,
                                    QString *errorMessage);
  static bool reorderImagesAscending(const heiran::domain::SessionModel &session,
                                     QVector<heiran::domain::CellConfig> *cellLayoutCache);
  static bool reorderImagesReverse(const heiran::domain::SessionModel &session,
                                   QVector<heiran::domain::CellConfig> *cellLayoutCache);
  static bool reorderImagesRotateLeft(const heiran::domain::SessionModel &session,
                                      QVector<heiran::domain::CellConfig> *cellLayoutCache);
  static bool reorderImagesRotateRight(const heiran::domain::SessionModel &session,
                                       QVector<heiran::domain::CellConfig> *cellLayoutCache);

private:
  static int visibleCellCount(const heiran::domain::SessionModel &session);
  static void ensureVisibleCellCapacity(QVector<heiran::domain::CellConfig> *cellLayoutCache,
                                        int visibleCellCount);
};

}  // namespace heiran::ui
