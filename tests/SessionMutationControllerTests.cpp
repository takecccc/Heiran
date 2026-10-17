#include "heiran/ui/SessionMutationController.hpp"

#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;
using heiran::ui::SessionMutationController;

namespace {

ImageSet makeImageSet(const QString &name, double scale = 1.0) {
  ImageSet imageSet;
  imageSet.name = name;
  imageSet.scale = scale;
  return imageSet;
}

CellConfig makeCell(int imageIndex, double scale = 1.0) {
  CellConfig cell;
  cell.imageIndex = imageIndex;
  cell.scale = scale;
  return cell;
}

SessionModel makeSession() {
  SessionModel session;
  session.images = {makeImageSet("a", 2.0), makeImageSet("b", 3.0)};
  session.cells = {makeCell(0, 2.0), makeCell(1, 3.0)};
  return session;
}

}  // namespace

class SessionMutationControllerTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void applyImageSetEditReplacesImagesAndCells() {
    SessionModel session = makeSession();
    SessionMutationController::applyImageSetEdit(&session, {makeImageSet("c")}, {makeCell(0)});

    QCOMPARE(session.images.size(), 1);
    QCOMPARE(session.images.at(0).name, QString("c"));
    QCOMPARE(session.cells.size(), 1);
  }

  // セルの倍率は、渡したままにすること。画像セットの既定の倍率との揃えは、ダイアログが、表を編集する時に行う
  // (セルごとの倍率を、あとから上書きしない)。
  void applyImageSetEditKeepsTheCellScales() {
    SessionModel session = makeSession();
    SessionMutationController::applyImageSetEdit(&session,
                                                 {makeImageSet("a", 4.0), makeImageSet("b", 0.5)},
                                                 {makeCell(1, 9.0), makeCell(0, 7.0), makeCell(-1, 3.0)});

    QCOMPARE(session.cells.at(0).scale, 9.0);
    QCOMPARE(session.cells.at(1).scale, 7.0);
    QCOMPARE(session.cells.at(2).scale, 3.0);
  }

  // 画像セットの入れ替えでパスを解決し直すこと。古い解決結果を残さない。
  void applyImageSetEditResolvesPathsAgain() {
    SessionModel session = makeSession();
    session.images[0].resolvedPaths = {"stale.png"};

    ImageSet replacement = makeImageSet("c");
    replacement.resolvedPaths = {"stale.png"};
    replacement.path = "no-such-dir/*.png";
    SessionMutationController::applyImageSetEdit(&session, {replacement}, {makeCell(0)});

    QVERIFY(session.images.at(0).resolvedPaths.isEmpty());
  }

  // ---- ドロップ ----

  // 複数の画像をドロップしたとき、セルが足りなければ格子を広げて全部を読み込むこと。
  // 広げないと、1 枚だけ開いた直後 (1 セル) に何枚ドロップしても 1 枚しか入らない。
  void growsTheLayoutToFitSeveralDroppedFiles() {
    SessionModel session;
    session.cells = {makeCell(-1)};
    QVector<CellConfig> cache = session.cells;

    const auto result = SessionMutationController::applyDroppedItems(
        &session, &cache, 0, {"/d/a.png", "/d/b.png", "/d/c.png"}, 0);

    QVERIFY(result.changed);
    QVERIFY(result.layoutChanged);
    // 3 枚は 2x2。空きが 1 つ残る。
    QCOMPARE(session.viewer.layout.rows * session.viewer.layout.cols, 4);
    QCOMPARE(session.images.size(), 3);
    QVERIFY(session.cells.at(0).imageIndex >= 0);
    QVERIFY(session.cells.at(1).imageIndex >= 0);
    QVERIFY(session.cells.at(2).imageIndex >= 0);
    QCOMPARE(session.cells.at(3).imageIndex, -1);
    QVERIFY2(result.statusMessage.contains("layout is now"), qPrintable(result.statusMessage));
  }

  // 足りているときは格子を変えないこと。
  void keepsTheLayoutWhenTheCellsAreEnough() {
    SessionModel session;
    session.viewer.layout.rows = 2;
    session.viewer.layout.cols = 2;
    session.cells = {makeCell(-1), makeCell(-1), makeCell(-1), makeCell(-1)};
    QVector<CellConfig> cache = session.cells;

    const auto result = SessionMutationController::applyDroppedItems(&session, &cache, 0,
                                                                     {"/d/a.png", "/d/b.png"}, 0);

    QVERIFY(result.changed);
    QVERIFY(!result.layoutChanged);
    QCOMPARE(session.viewer.layout.rows, 2);
    QCOMPARE(session.viewer.layout.cols, 2);
  }

  // 1 枚のドロップでは広げないこと。置き換えるだけ。
  void doesNotGrowForASingleDroppedFile() {
    SessionModel session;
    session.cells = {makeCell(-1)};
    QVector<CellConfig> cache = session.cells;

    const auto result =
        SessionMutationController::applyDroppedItems(&session, &cache, 0, {"/d/a.png"}, 0);

    QVERIFY(!result.layoutChanged);
    QCOMPARE(session.viewer.layout.rows * session.viewer.layout.cols, 1);
  }

  // ドロップ先のセルから数えて足りるかを見ること。後ろのセルへドロップすると、
  // 前に空きがあっても足りなくなる。
  void countsFromTheDroppedCell() {
    SessionModel session;
    session.viewer.layout.rows = 1;
    session.viewer.layout.cols = 2;
    session.cells = {makeCell(-1), makeCell(-1)};
    QVector<CellConfig> cache = session.cells;

    // 2 セル目へ 2 枚。2 セル目と、広げた先の 3 セル目へ入る。
    const auto result = SessionMutationController::applyDroppedItems(&session, &cache, 1,
                                                                     {"/d/a.png", "/d/b.png"}, 0);

    QVERIFY(result.layoutChanged);
    QVERIFY(session.viewer.layout.rows * session.viewer.layout.cols >= 3);
    QVERIFY(session.cells.at(1).imageIndex >= 0);
    QVERIFY(session.cells.at(2).imageIndex >= 0);
  }

  // 広げるのは上限まで。数十枚をドロップして、数十セルを同時に読み込む格子にしない。
  // 超えた分は無視して知らせる。
  void capsTheGrowthAtTheLimit() {
    SessionModel session;
    session.cells = {makeCell(-1)};
    QVector<CellConfig> cache = session.cells;
    QStringList paths;
    for (int index = 0; index < 20; ++index) {
      paths.push_back(QString("/d/%1.png").arg(index));
    }

    const auto result = SessionMutationController::applyDroppedItems(&session, &cache, 0, paths, 0);

    QVERIFY(result.layoutChanged);
    QCOMPARE(session.viewer.layout.rows * session.viewer.layout.cols,
             SessionMutationController::kMaxCellsGrownByDrop);
    QCOMPARE(session.images.size(), SessionMutationController::kMaxCellsGrownByDrop);
    QVERIFY2(result.statusMessage.contains("Ignored 4"), qPrintable(result.statusMessage));
  }

  // ---- クリア ----

  void clearsEveryCellButKeepsTheImageSets() {
    SessionModel session = makeSession();
    QVector<CellConfig> cache = session.cells;

    QVERIFY(SessionMutationController::clearAllCells(&session, &cache));

    for (const auto &cell : session.cells) {
      QCOMPARE(cell.imageIndex, -1);
    }
    for (const auto &cell : cache) {
      QCOMPARE(cell.imageIndex, -1);
    }
    // 画像セットは残る。右クリックの Assign Image で選び直せる。
    QCOMPARE(session.images.size(), 2);
  }

  // 何も割り当てていなければ、変更なしと答えること。未保存の印を付けない根拠になる。
  void reportsNothingToClear() {
    SessionModel session = makeSession();
    for (auto &cell : session.cells) {
      cell.imageIndex = -1;
    }
    QVector<CellConfig> cache = session.cells;

    QVERIFY(!SessionMutationController::clearAllCells(&session, &cache));
  }

  void labelVisibilityReportsWhetherItChanged() {
    SessionModel session = makeSession();
    QVERIFY(SessionMutationController::applyLabelVisibility(&session, 0, true));
    QVERIFY(session.images.at(0).labelVisible);
    QVERIFY(!SessionMutationController::applyLabelVisibility(&session, 0, true));
    QVERIFY(!SessionMutationController::applyLabelVisibility(&session, 9, true));
  }

  // 同じ画像を割り当て直しても変更として扱わないこと。未保存の印が付いてしまう。
  void assignImageIndexToCellIgnoresNoChange() {
    SessionModel session = makeSession();
    QVector<CellConfig> cache = session.cells;
    QVERIFY(!SessionMutationController::assignImageIndexToCell(&session, &cache, 0, 0));
    QVERIFY(!SessionMutationController::assignImageIndexToCell(&session, &cache, 0, 9));
    QVERIFY(!SessionMutationController::assignImageIndexToCell(&session, &cache, 9, 0));
  }

  void assignImageIndexToCellCopiesScaleAndUpdatesTheCache() {
    SessionModel session = makeSession();
    QVector<CellConfig> cache;
    QVERIFY(SessionMutationController::assignImageIndexToCell(&session, &cache, 0, 1));

    QCOMPARE(session.cells.at(0).imageIndex, 1);
    QCOMPARE(session.cells.at(0).scale, 3.0);
    QCOMPARE(cache.size(), 2);
    QCOMPARE(cache.at(0).imageIndex, 1);
  }
};

QTEST_APPLESS_MAIN(SessionMutationControllerTests)

#include "SessionMutationControllerTests.moc"
