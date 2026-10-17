#include "heiran/ui/SessionLayoutController.hpp"

#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::SessionModel;
using heiran::ui::SessionLayoutController;

namespace {

// 指定した格子と、セルへの画像割り当てを持つセッションを作る。
SessionModel makeSession(int rows, int cols, int imageCount = 4) {
  SessionModel session;
  session.viewer.layout.rows = rows;
  session.viewer.layout.cols = cols;
  session.images.resize(imageCount);
  return session;
}

QVector<CellConfig> makeCells(const QVector<int> &imageIndices) {
  QVector<CellConfig> cells;
  cells.reserve(imageIndices.size());
  for (const int imageIndex : imageIndices) {
    CellConfig cell;
    cell.imageIndex = imageIndex;
    cells.push_back(cell);
  }
  return cells;
}

QVector<int> imageIndicesOf(const QVector<CellConfig> &cells) {
  QVector<int> indices;
  indices.reserve(cells.size());
  for (const auto &cell : cells) {
    indices.push_back(cell.imageIndex);
  }
  return indices;
}

}  // namespace

class SessionLayoutControllerTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 枚数に合う格子を返すこと。列を先に増やすため横に広い画面へ収まりやすい。
  void picksAGridThatFitsTheImageCount() {
    const auto layoutFor = [](int count) {
      const auto layout = SessionLayoutController::layoutForImageCount(count);
      return QString("%1x%2").arg(layout.rows).arg(layout.cols);
    };

    QCOMPARE(layoutFor(0), QString("1x1"));
    QCOMPARE(layoutFor(1), QString("1x1"));
    QCOMPARE(layoutFor(2), QString("1x2"));
    QCOMPARE(layoutFor(3), QString("2x2"));
    QCOMPARE(layoutFor(4), QString("2x2"));
    QCOMPARE(layoutFor(5), QString("2x3"));
    QCOMPARE(layoutFor(9), QString("3x3"));
  }

  // 昇順に並べ替えること。空のセルは最後へ送る。
  //
  // 空を混ぜたまま昇順にすると、間に穴が空いて詰めた意味が無くなる。
  void sortsImagesAscendingAndPushesEmptyCellsLast() {
    const auto session = makeSession(2, 2);
    auto cells = makeCells({2, -1, 0, 1});

    QVERIFY(SessionLayoutController::reorderImagesAscending(session, &cells));
    QCOMPARE(imageIndicesOf(cells), QVector<int>({0, 1, 2, -1}));
  }

  // 表示中のセルだけを並べ替え、その先は触らないこと。
  //
  // 格子を小さくすると、隠れたセルの割り当ては残しておく。全体を並べ替えると
  // 格子を戻したときに配置が崩れる。
  void reordersOnlyTheVisibleCells() {
    const auto session = makeSession(1, 2);
    auto cells = makeCells({3, 1, 9, 8});

    QVERIFY(SessionLayoutController::reorderImagesAscending(session, &cells));
    QCOMPARE(imageIndicesOf(cells), QVector<int>({1, 3, 9, 8}));

    auto reversed = makeCells({0, 1, 9, 8});
    QVERIFY(SessionLayoutController::reorderImagesReverse(session, &reversed));
    QCOMPARE(imageIndicesOf(reversed), QVector<int>({1, 0, 9, 8}));
  }

  // 逆順にできること。
  void reversesTheVisibleCells() {
    const auto session = makeSession(2, 2);
    auto cells = makeCells({0, 1, 2, 3});

    QVERIFY(SessionLayoutController::reorderImagesReverse(session, &cells));
    QCOMPARE(imageIndicesOf(cells), QVector<int>({3, 2, 1, 0}));
  }

  // 左右へ 1 つずつ回せること。向きを取り違えると比較の並びが逆になる。
  void rotatesInBothDirections() {
    const auto session = makeSession(2, 2);

    auto left = makeCells({0, 1, 2, 3});
    QVERIFY(SessionLayoutController::reorderImagesRotateLeft(session, &left));
    QCOMPARE(imageIndicesOf(left), QVector<int>({1, 2, 3, 0}));

    auto right = makeCells({0, 1, 2, 3});
    QVERIFY(SessionLayoutController::reorderImagesRotateRight(session, &right));
    QCOMPARE(imageIndicesOf(right), QVector<int>({3, 0, 1, 2}));
  }

  // 回しても意味が無い場合は何もしないこと。
  void refusesToRotateASingleCell() {
    const auto session = makeSession(1, 1);
    auto cells = makeCells({0});

    QVERIFY(!SessionLayoutController::reorderImagesRotateLeft(session, &cells));
    QVERIFY(!SessionLayoutController::reorderImagesRotateRight(session, &cells));
    QCOMPARE(imageIndicesOf(cells), QVector<int>({0}));
  }

  // 格子を広げるとセルが足りなくなる。足りない分を作ること。
  void growsTheCellCacheWhenTheGridGetsBigger() {
    auto session = makeSession(1, 1);
    auto cells = makeCells({0});

    QVERIFY(SessionLayoutController::applyLayout(&session, &cells, 2, 3));
    QCOMPARE(session.viewer.layout.rows, 2);
    QCOMPARE(session.viewer.layout.cols, 3);
    QCOMPARE(cells.size(), 6);
    QCOMPARE(session.cells.size(), 6);
    // 元からあった割り当ては残す。
    QCOMPARE(cells.at(0).imageIndex, 0);
    // 増えた分は空にする。
    QCOMPARE(cells.at(5).imageIndex, -1);
  }

  // 成り立たない格子は受け付けないこと。
  void rejectsANonPositiveGrid() {
    auto session = makeSession(2, 2);
    auto cells = makeCells({0, 1, 2, 3});

    QVERIFY(!SessionLayoutController::applyLayout(&session, &cells, 0, 2));
    QVERIFY(!SessionLayoutController::applyLayout(&session, &cells, 2, -1));
    QCOMPARE(session.viewer.layout.rows, 2);
  }

  // 並び順は変わったときだけ真を返すこと。呼び側は保存の要否に使う。
  void reportsWhetherTheLayoutOrderChanged() {
    auto session = makeSession(2, 2);
    session.viewer.layout.order = "row-major";

    QVERIFY(!SessionLayoutController::applyLayoutOrder(&session, "row-major"));
    QVERIFY(!SessionLayoutController::applyLayoutOrder(&session, QString()));
    QVERIFY(SessionLayoutController::applyLayoutOrder(&session, "column-major"));
    QCOMPARE(session.viewer.layout.order, QString("column-major"));
  }

  // 同じ画像を 2 つのセルへ割り当てさせないこと。理由も返す。
  void rejectsAssigningOneImageToTwoCells() {
    const auto session = makeSession(1, 2);
    const auto cells = makeCells({0, 1});
    QVector<CellConfig> newCells;
    QString errorMessage;

    QVERIFY(!SessionLayoutController::applyCustomImageOrder(session, cells, {1, 1}, &newCells,
                                                            &errorMessage));
    QVERIFY2(errorMessage.contains("Image 2"), qPrintable(errorMessage));
  }

  // 空のセルは重なってもよいこと。全てのセルを埋める必要は無い。
  void allowsSeveralEmptyCells() {
    const auto session = makeSession(1, 2);
    const auto cells = makeCells({0, 1});
    QVector<CellConfig> newCells;

    QVERIFY(SessionLayoutController::applyCustomImageOrder(session, cells, {-1, -1}, &newCells,
                                                           nullptr));
    QCOMPARE(imageIndicesOf(newCells), QVector<int>({-1, -1}));
  }

  // セルの数と割り当ての数が合わない場合は受け付けないこと。
  void rejectsAnAssignmentOfTheWrongSize() {
    const auto session = makeSession(1, 2);
    const auto cells = makeCells({0, 1});
    QVector<CellConfig> newCells;

    QVERIFY(
        !SessionLayoutController::applyCustomImageOrder(session, cells, {0}, &newCells, nullptr));
  }

  // セルの数ごとの格子: 積がちょうどその数になる、行と列のすべての組。行の少ない順。
  void listsEveryGridForACellCount() {
    const auto describe = [](int cellCount) {
      QStringList grids;
      for (const auto &layout : SessionLayoutController::layoutsForCellCount(cellCount)) {
        grids.push_back(QString("%1x%2").arg(layout.rows).arg(layout.cols));
      }
      return grids;
    };
    QCOMPARE(describe(1), QStringList({"1x1"}));
    QCOMPARE(describe(2), QStringList({"1x2", "2x1"}));
    QCOMPARE(describe(5), QStringList({"1x5", "5x1"}));  // 素数は、1 行か 1 列だけ
    QCOMPARE(describe(6), QStringList({"1x6", "2x3", "3x2", "6x1"}));
    QCOMPARE(describe(12), QStringList({"1x12", "2x6", "3x4", "4x3", "6x2", "12x1"}));
    QCOMPARE(describe(16), QStringList({"1x16", "2x8", "4x4", "8x2", "16x1"}));
    // 1 から上限までのどの数にも、少なくとも 1 つの格子がある。範囲の外は、空。
    for (int count = 1; count <= SessionLayoutController::kMaxCellCount; ++count) {
      QVERIFY(!describe(count).isEmpty());
    }
    QVERIFY(describe(0).isEmpty());
    QVERIFY(describe(SessionLayoutController::kMaxCellCount + 1).isEmpty());
  }

  // セルを 1 つ増やす広げ方: 列が 4 に満たなければ列、次に行。上限を超える方は避ける。
  void growsTheGridByOneCellWithinTheLimit() {
    const auto grow = [](int rows, int cols) {
      heiran::domain::ViewerLayout layout;
      layout.rows = rows;
      layout.cols = cols;
      const bool grown = SessionLayoutController::growLayout(&layout);
      return grown ? QString("%1x%2").arg(layout.rows).arg(layout.cols) : QString("none");
    };
    QCOMPARE(grow(1, 1), QString("1x2"));
    QCOMPARE(grow(2, 2), QString("2x3"));
    QCOMPARE(grow(2, 4), QString("3x4"));
    QCOMPARE(grow(4, 3), QString("4x4"));
    QCOMPARE(grow(1, 4), QString("2x4"));
    // 4 を超える横長の格子は、列を増やせる間は、列を増やす。
    QCOMPARE(grow(1, 5), QString("1x6"));
    QCOMPARE(grow(2, 7), QString("2x8"));
    // 上限 (16) の格子は、広げられない。
    QCOMPARE(grow(4, 4), QString("none"));
    QCOMPARE(grow(1, 16), QString("none"));
    QCOMPARE(grow(16, 1), QString("none"));
    // 列が増やせず、行を増やすと上限を超えるときも、広げられない。
    QCOMPARE(grow(3, 5), QString("none"));
  }
};

QTEST_APPLESS_MAIN(SessionLayoutControllerTests)

#include "SessionLayoutControllerTests.moc"
