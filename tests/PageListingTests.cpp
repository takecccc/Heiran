#include "heiran/ui/PageListing.hpp"

#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;
using heiran::ui::PageListing;

namespace {

ImageSet makeImageSet(const QStringList &paths, int pageOffset = 0) {
  ImageSet imageSet;
  imageSet.pageOffset = pageOffset;
  for (const QString &path : paths) {
    imageSet.resolvedPaths.push_back(path);
  }
  return imageSet;
}

// 1 x cellCount の格子で、セルごとに指定の画像セットを割り当てる。
SessionModel makeSession(const QVector<ImageSet> &images, const QVector<int> &cellImageIndices) {
  SessionModel session;
  session.viewer.layout.rows = 1;
  session.viewer.layout.cols = static_cast<int>(cellImageIndices.size());
  session.images = images;
  for (const int imageIndex : cellImageIndices) {
    CellConfig cell;
    cell.imageIndex = imageIndex;
    session.cells.push_back(cell);
  }
  return session;
}

}  // namespace

class PageListingTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void usesTheFirstCellThatHasThePage() {
    const auto session = makeSession(
        {makeImageSet({"a0.png", "a1.png"}), makeImageSet({"b0.png", "b1.png"})}, {0, 1});
    QCOMPARE(PageListing::pagePath(session, 1), QString("a1.png"));
  }

  // 先頭のセルがそのページを持たない場合、次のセルの絵を使うこと。
  void fallsThroughToALaterCell() {
    const auto session =
        makeSession({makeImageSet({"a0.png"}), makeImageSet({"b0.png", "b1.png"})}, {0, 1});
    QCOMPARE(PageListing::pagePath(session, 1), QString("b1.png"));
  }

  // pageOffset だけ後ろへずれること。オフセットより前のページは持たない。
  void honorsPageOffset() {
    const auto session = makeSession({makeImageSet({"a0.png", "a1.png"}, 2)}, {0});
    QVERIFY(PageListing::pagePath(session, 1).isEmpty());
    QCOMPARE(PageListing::pagePath(session, 2), QString("a0.png"));
    QCOMPARE(PageListing::pagePath(session, 3), QString("a1.png"));
    QVERIFY(PageListing::pagePath(session, 4).isEmpty());
  }

  // 格子の外にあるセルは見えていないため、代表にしないこと。
  void ignoresCellsOutsideTheLayout() {
    auto session = makeSession({makeImageSet({"a0.png"}), makeImageSet({"b0.png"})}, {0, 1});
    session.viewer.layout.cols = 1;
    QCOMPARE(PageListing::pagePath(session, 0), QString("a0.png"));

    session.cells[0].imageIndex = -1;
    QVERIFY(PageListing::pagePath(session, 0).isEmpty());
  }

  // 割り当て無しや範囲外の画像セットで落ちないこと。
  void skipsCellsWithoutAValidImageSet() {
    const auto session = makeSession({makeImageSet({"a0.png"})}, {-1, 5, 0});
    QCOMPARE(PageListing::pagePath(session, 0), QString("a0.png"));
  }

  void listsEveryPage() {
    const auto session = makeSession({makeImageSet({"a0.png", "a1.png"})}, {0});
    QCOMPARE(PageListing::pagePaths(session, 2), QStringList({"a0.png", "a1.png"}));
    QVERIFY(PageListing::pagePaths(session, 0).isEmpty());
  }

  // ファイルの無いページも並びから抜かないこと。番号がずれる。
  void keepsEmptyEntriesForMissingPages() {
    const auto session = makeSession({makeImageSet({"a0.png"})}, {0});
    QCOMPARE(PageListing::pagePaths(session, 2), QStringList({"a0.png", ""}));
  }

  void displaysPathsRelativeToTheSessionDirectory() {
    auto session = makeSession({makeImageSet({"/work/img/a0.png"})}, {0});
    session.sessionDirectory = "/work";
    QCOMPARE(PageListing::displayTexts(session, 1), QStringList({"1: img/a0.png"}));
  }

  void displaysAbsolutePathsWithoutASessionDirectory() {
    const auto session = makeSession({makeImageSet({"/work/img/a0.png"})}, {0});
    QCOMPARE(PageListing::displayTexts(session, 1), QStringList({"1: /work/img/a0.png"}));
  }

  // ファイルの無いページは番号だけを出すこと。
  void displaysOnlyTheNumberForMissingPages() {
    const auto session = makeSession({makeImageSet({"a0.png"})}, {0});
    QCOMPARE(PageListing::displayTexts(session, 2), QStringList({"1: a0.png", "2"}));
  }
};

QTEST_APPLESS_MAIN(PageListingTests)

#include "PageListingTests.moc"
