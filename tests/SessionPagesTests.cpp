#include "heiran/domain/SessionPages.hpp"

#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;

namespace {

ImageSet makeImageSet(const QStringList &paths, int pageOffset = 0) {
  ImageSet imageSet;
  imageSet.pageOffset = pageOffset;
  for (const QString &path : paths) {
    imageSet.resolvedPaths.push_back(path);
  }
  return imageSet;
}

SessionModel makeSession(const QVector<ImageSet> &images, const QVector<int> &cellImageIndices) {
  SessionModel session;
  session.images = images;
  for (const int imageIndex : cellImageIndices) {
    CellConfig cell;
    cell.imageIndex = imageIndex;
    session.cells.push_back(cell);
  }
  return session;
}

}  // namespace

class SessionPagesTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void imageSetForCellReturnsTheAssignedSet() {
    const auto session = makeSession({makeImageSet({"a"}), makeImageSet({"b"})}, {1, 0});
    QCOMPARE(heiran::domain::imageSetForCell(session, 0), &session.images.at(1));
    QCOMPARE(heiran::domain::imageSetForCell(session, 1), &session.images.at(0));
  }

  // 割り当て無し、画像セットの範囲外、セルの範囲外のいずれも nullptr になること。
  void imageSetForCellRejectsInvalidAssignments() {
    const auto session = makeSession({makeImageSet({"a"})}, {-1, 5});
    QVERIFY(heiran::domain::imageSetForCell(session, 0) == nullptr);
    QVERIFY(heiran::domain::imageSetForCell(session, 1) == nullptr);
    QVERIFY(heiran::domain::imageSetForCell(session, -1) == nullptr);
    QVERIFY(heiran::domain::imageSetForCell(session, 2) == nullptr);
  }

  // pageOffset より前と末尾より後ろは空になること。
  void pagePathHonorsTheOffsetAndTheEnd() {
    const auto imageSet = makeImageSet({"a", "b"}, 2);
    QVERIFY(heiran::domain::pagePathInImageSet(imageSet, 1).isEmpty());
    QCOMPARE(heiran::domain::pagePathInImageSet(imageSet, 2), QString("a"));
    QCOMPARE(heiran::domain::pagePathInImageSet(imageSet, 3), QString("b"));
    QVERIFY(heiran::domain::pagePathInImageSet(imageSet, 4).isEmpty());
  }

  // 最も後ろまで続く画像セットで決まること。offset を含めて数える。
  void totalPageCountFollowsTheLongestImageSet() {
    const auto session =
        makeSession({makeImageSet({"a", "b", "c"}), makeImageSet({"d"}, 4)}, {0, 1});
    QCOMPARE(heiran::domain::totalPageCount(session), 5);
  }

  // ファイルを解決できなかった画像セットは数えないこと。offset だけ残しても 0 ページ。
  void totalPageCountIgnoresImageSetsWithoutFiles() {
    const auto session = makeSession({makeImageSet({}, 7), makeImageSet({"a"})}, {0, 1});
    QCOMPARE(heiran::domain::totalPageCount(session), 1);
  }

  // 割り当てのない画像セットは、どのセルも見ないため数えないこと。
  void totalPageCountIgnoresUnassignedImageSets() {
    const auto session = makeSession({makeImageSet({"a"}), makeImageSet({"b", "c", "d"})}, {0});
    QCOMPARE(heiran::domain::totalPageCount(session), 1);
  }

  void cellPageCarriesTheImageSetAndThePath() {
    const auto session = makeSession({makeImageSet({"a", "b"}, 1)}, {0});
    const auto cellPage = heiran::domain::cellPageAt(session, 0, 2);
    QVERIFY(cellPage.hasFile());
    QCOMPARE(cellPage.path, QString("b"));
    QCOMPARE(cellPage.imageSet, &session.images.at(0));
  }

  // そのページを持たない場合は、画像セットは分かるがファイルは無いこと。
  void cellPageWithoutAFileStillKnowsTheImageSet() {
    const auto session = makeSession({makeImageSet({"a"})}, {0});
    const auto cellPage = heiran::domain::cellPageAt(session, 0, 5);
    QVERIFY(!cellPage.hasFile());
    QCOMPARE(cellPage.imageSet, &session.images.at(0));
  }

  void cellPageWithoutAnAssignmentHasNothing() {
    const auto session = makeSession({makeImageSet({"a"})}, {-1});
    const auto cellPage = heiran::domain::cellPageAt(session, 0, 0);
    QVERIFY(!cellPage.hasFile());
    QVERIFY(cellPage.imageSet == nullptr);
  }

  // ファイルが何ページ目かを引けること。名前を変えると並びが変わるため、変えた後に
  // 同じファイルを表示し続けるのに使う。pageOffset を含めた全体のページ番号で返す。
  void pageOfFileFindsTheFile() {
    const auto session = makeSession({makeImageSet({"/d/a.png", "/d/b.png", "/d/c.png"}, 2)}, {0});
    QCOMPARE(heiran::domain::pageOfFile(session, 0, "/d/a.png"), 2);
    QCOMPARE(heiran::domain::pageOfFile(session, 0, "/d/c.png"), 4);
  }

  void pageOfFileReturnsMinusOneWhenTheFileIsNotThere() {
    const auto session = makeSession({makeImageSet({"/d/a.png"})}, {0});
    QCOMPARE(heiran::domain::pageOfFile(session, 0, "/d/missing.png"), -1);
    QCOMPARE(heiran::domain::pageOfFile(session, 0, QString()), -1);
    QCOMPARE(heiran::domain::pageOfFile(session, 5, "/d/a.png"), -1);
  }

  // 区切りの違いや、余分な ./ があっても同じファイルとみなすこと。
  void pageOfFileIgnoresPathSpelling() {
    const auto session = makeSession({makeImageSet({"/d/sub/a.png"})}, {0});
    QCOMPARE(heiran::domain::pageOfFile(session, 0, "/d/./sub//a.png"), 0);
  }

  void totalPageCountIsZeroWithoutCells() {
    QCOMPARE(heiran::domain::totalPageCount(SessionModel{}), 0);
  }
};

QTEST_APPLESS_MAIN(SessionPagesTests)

#include "SessionPagesTests.moc"
