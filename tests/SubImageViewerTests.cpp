// ファイルの中の画像 (フレーム / ページ) を、ビューアが扱えること。
//
// ページ (マルチページ TIFF) は独立した画像で手でめくり、フレーム (アニメーション GIF) は
// 時間の流れで再生する。ファイルの連番 (currentPage) とは別の軸で、全セルが 1 つの番号を共有する。

#include "MultiFrameFixtures.hpp"
#include "heiran/domain/SessionModel.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using heiran::domain::ImageSet;
using heiran::domain::SessionModel;
using heiran::infra::SubImageKind;
using heiran::test::makeGif;
using heiran::test::makeTiff;
using heiran::test::writeFile;

namespace {

// 既に書き出してあるファイルを並べた画像セット。
ImageSet makeFileImageSet(const QString &name, const QStringList &paths) {
  ImageSet imageSet;
  imageSet.name = name;
  imageSet.label = name;
  imageSet.path = name;
  for (const QString &path : paths) {
    imageSet.resolvedPaths.push_back(path);
  }
  return imageSet;
}

SessionModel makeSession(const QVector<ImageSet> &images, int rows, int cols,
                         const QVector<int> &cellImageIndexes) {
  SessionModel session;
  session.viewer.layout.rows = rows;
  session.viewer.layout.cols = cols;
  session.images = images;
  for (const int imageIndex : cellImageIndexes) {
    session.cells.push_back({.imageIndex = imageIndex});
  }
  return session;
}

bool waitForCellImages(heiran::ui::ViewerGridWidget *grid) {
  const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
  return QTest::qWaitFor(
      [&cells]() {
        for (const auto *cell : cells) {
          if (cell->cellStatus() == heiran::ui::CellStatus::Loading) {
            return false;
          }
        }
        return true;
      },
      5000);
}

QColor shownColor(heiran::ui::ViewerGridWidget *grid, int cellIndex = 0) {
  return grid->findChildren<heiran::ui::ImageViewWidget *>()
      .at(cellIndex)
      ->currentImage()
      .pixelColor(0, 0);
}

QSize shownSize(heiran::ui::ViewerGridWidget *grid, int cellIndex = 0) {
  return grid->findChildren<heiran::ui::ImageViewWidget *>().at(cellIndex)->currentImage().size();
}

}  // namespace

class SubImageViewerTests : public QObject {
  Q_OBJECT

private:
  // 1 セルのビューアへ、指定のファイルを 1 つの画像セットとして開く。
  void openFiles(heiran::ui::ViewerGridWidget *grid, const QStringList &paths) {
    grid->resize(400, 300);
    grid->setSession(makeSession({makeFileImageSet("files", paths)}, 1, 1, {0}));
    grid->show();
    QVERIFY(QTest::qWaitForWindowExposed(grid));
    QVERIFY(waitForCellImages(grid));
  }

  // アニメーション PNG。色ごとの全面のフレームを並べる。delayCs は 1/100 秒。
  QString apng(const QString &name, const QVector<QColor> &colors, int delayCs = 20) {
    QVector<heiran::test::ApngFixtureFrame> frames;
    for (const QColor &color : colors) {
      QImage image(4, 4, QImage::Format_RGBA8888);
      image.fill(color);
      heiran::test::ApngFixtureFrame frame{image};
      frame.delayNumerator = static_cast<quint16>(delayCs);
      frame.delayDenominator = 100;
      frames.push_back(frame);
    }
    return writeFile(QDir(directory_.path()), name, heiran::test::makeApng(4, 4, frames));
  }

  // 後から作る形を、小さなファイルでも通す。境目を 0 にして、抜けたら戻す。
  struct StreamingEverything {
    StreamingEverything() { heiran::infra::setAnimationStreamingThresholdBytes(0); }
    ~StreamingEverything() {
      heiran::infra::setAnimationStreamingThresholdBytes(128LL * 1024 * 1024);
    }
  };

  QString gif(const QString &name, const QVector<int> &colors, const QVector<int> &delaysCs) {
    return writeFile(QDir(directory_.path()), name, makeGif(4, 4, colors, delaysCs));
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // ---- フレーム ----

  // アニメーション GIF のフレームを、番号で切り替えて見られること。
  void showsTheFrameOfAnAnimation() {
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("frames.gif", {0, 1, 2}, {20, 20, 20})});

    QVERIFY(grid.hasAnimation());
    QCOMPARE(grid.subImageCount(), 3);
    QCOMPARE(grid.subImageKind(), SubImageKind::Frames);
    QCOMPARE(shownColor(&grid), QColor(255, 0, 0));

    grid.setSubImageIndex(1);
    QCOMPARE(shownColor(&grid), QColor(0, 255, 0));
    grid.setSubImageIndex(2);
    QCOMPARE(shownColor(&grid), QColor(0, 0, 255));
  }

  // アニメーション PNG も、GIF と同じようにフレームで切り替えて見られること。
  void showsTheFrameOfAnAnimatedPng() {
    QImage red(4, 4, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QImage green(4, 4, QImage::Format_RGBA8888);
    green.fill(Qt::green);
    const QString path = writeFile(QDir(directory_.path()), "anim.png",
                                   heiran::test::makeApng(4, 4, {{red}, {green}}));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {path});

    QVERIFY(grid.hasAnimation());
    QCOMPARE(grid.subImageCount(), 2);
    QCOMPARE(shownColor(&grid), QColor(255, 0, 0));
    grid.setSubImageIndex(1);
    QCOMPARE(shownColor(&grid), QColor(0, 255, 0));
  }

  // アニメーションは範囲を超えたら巻き戻ること。ページと違い、時間の流れは輪になる。
  void wrapsTheFramesOfAnAnimation() {
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("wrap.gif", {0, 1, 2}, {20, 20, 20})});

    grid.stepSubImage(-1);
    QCOMPARE(grid.subImageIndex(), 2);
    QCOMPARE(shownColor(&grid), QColor(0, 0, 255));
    grid.stepSubImage(1);
    QCOMPARE(grid.subImageIndex(), 0);
    QCOMPARE(shownColor(&grid), QColor(255, 0, 0));
  }

  // 状態欄にいまのフレームが出ること。全画面ではこれが唯一の手掛かりになる。
  void reportsTheFrameInTheStatus() {
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("status.gif", {0, 1, 2}, {20, 20, 20})});

    QSignalSpy statusSpy(&grid, &heiran::ui::ViewerGridWidget::statusChanged);
    grid.setSubImageIndex(1);

    QVERIFY(statusSpy.count() > 0);
    QVERIFY2(statusSpy.last().at(0).toString().contains("Frame 2 / 3"),
             qPrintable(statusSpy.last().at(0).toString()));
    QCOMPARE(grid.subImageStatusText(), QString("Frame 2 / 3"));
  }

  // 再生するとフレームが進み、止めると止まること。
  void playsAndStopsAnAnimation() {
    heiran::ui::ViewerGridWidget grid;
    // 11 (1/100 秒) = 110 ms。10 ms 以下は 100 ms として扱うため、それより長い値にする。
    openFiles(&grid, {gif("play.gif", {0, 1, 2}, {11, 11, 11})});
    QCOMPARE(grid.subImageIndex(), 0);

    grid.setAnimationPlaying(true);
    QVERIFY(grid.isAnimationPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() != 0, 2000);

    grid.setAnimationPlaying(false);
    const int stopped = grid.subImageIndex();
    QTest::qWait(400);
    QCOMPARE(grid.subImageIndex(), stopped);
  }

  // セッションを作り直しても、再生の指定は残ること。レイアウトの変更やドロップでも
  // 作り直しが走る。ここで止めると、画像を直接開いた後の編集で再生が止まる。
  // 先頭のフレームへは戻る。
  void keepsThePlaybackFlagWhenTheSessionIsRebuilt() {
    heiran::ui::ViewerGridWidget grid;
    const QString path = gif("rebuild.gif", {0, 1, 2}, {11, 11, 11});
    openFiles(&grid, {path});
    grid.setSubImageIndex(2);
    grid.setAnimationPlaying(true);

    grid.setSession(makeSession({makeFileImageSet("files", {path})}, 1, 1, {0}));

    QVERIFY(grid.isAnimationPlaying());
    QCOMPARE(grid.subImageIndex(), 0);
  }

  // キャッシュから追い出されても、表示中のアニメーションが動き続けること。
  //
  // アニメーションは全フレームを持ち、1 つが数百 MB になる。フォルダ内の他のアニメーションを
  // 先読みしただけで予算を超え、表示中のものが追い出されて再生が止まっていた。
  void keepsPlayingAnAnimationEvictedFromTheCache() {
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("evicted.gif", {0, 1, 2}, {11, 11, 11})});
    QVERIFY(grid.hasAnimation());

    // 予算を 1 バイトにして、全部を追い出す。
    grid.setDecodeCacheBudgetBytes(1);
    QCOMPARE(grid.decodeCacheEntryCount(), 0);

    QVERIFY(grid.hasAnimation());
    QCOMPARE(grid.subImageCount(), 3);
    grid.setAnimationPlaying(true);
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() != 0, 2000);
    grid.setSubImageIndex(2);
    QCOMPARE(shownColor(&grid), QColor(0, 0, 255));
  }

  // アニメーションを見せている間は先読みしないこと。1 つが数百 MB になり、隣のファイルを
  // 読んでは捨てる無駄になる (実物で、隣の APNG を読むたびに表示中のものが追い出された)。
  void doesNotPrefetchWhileAnAnimationIsShown() {
    QImage still(4, 4, QImage::Format_RGB32);
    still.fill(Qt::yellow);
    const QString png = QDir(directory_.path()).filePath("neighbor.png");
    QVERIFY(still.save(png));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("current.gif", {0, 1, 2}, {20, 20, 20}), png});
    QVERIFY(grid.hasAnimation());

    // 先読みは非同期に走る。走るなら届くだけの時間を待つ。
    QTest::qWait(600);

    QCOMPARE(grid.decodeCacheEntryCount(), 1);
  }

  // 静止画では、これまでどおり先読みすること。
  void stillPrefetchesNeighborsOfAStillImage() {
    QImage first(4, 4, QImage::Format_RGB32);
    first.fill(Qt::red);
    QImage second(4, 4, QImage::Format_RGB32);
    second.fill(Qt::green);
    const QString a = QDir(directory_.path()).filePath("prefetch-a.png");
    const QString b = QDir(directory_.path()).filePath("prefetch-b.png");
    QVERIFY(first.save(a));
    QVERIFY(second.save(b));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {a, b});

    QTRY_COMPARE_WITH_TIMEOUT(grid.decodeCacheEntryCount(), 2, 3000);
  }

  // ---- 後から作るアニメーション (大きな APNG) ----
  //
  // 全部を展開せず、圧縮したまま持って、要るフレームだけを背景で作る。境目を 0 にして、
  // 小さなファイルでその経路を通す。

  // 後から作るアニメーションのフレームを、番号で切り替えて見られること。
  // 作り終えるまで少しかかるため、届くまで待つ。
  void showsTheFrameOfAStreamedAnimation() {
    const StreamingEverything streaming;
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {apng("streamed.png", {Qt::red, Qt::green, Qt::blue, Qt::white})});

    QVERIFY(grid.hasAnimation());
    QCOMPARE(grid.subImageCount(), 4);
    QCOMPARE(shownColor(&grid), QColor(255, 0, 0));

    grid.setSubImageIndex(2);
    QTRY_COMPARE(shownColor(&grid), QColor(0, 0, 255));
    grid.setSubImageIndex(1);
    QTRY_COMPARE(shownColor(&grid), QColor(0, 255, 0));
    // 折り返し。
    grid.setSubImageIndex(4);
    QTRY_COMPARE(shownColor(&grid), QColor(255, 0, 0));
  }

  // 再生するとフレームが進み、何周もできること。
  void playsAStreamedAnimationInALoop() {
    const StreamingEverything streaming;
    heiran::ui::ViewerGridWidget grid;
    // 11 (1/100 秒) = 110 ms。
    openFiles(&grid, {apng("looping.png", {Qt::red, Qt::green, Qt::blue}, 11)});

    grid.setAnimationPlaying(true);

    // 3 コマを 1 周して、先頭へ戻ってくる。
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() == 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() == 0, 3000);
    // 表示中の絵が、番号と合っていること。
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() == 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(shownColor(&grid), QColor(0, 255, 0), 1000);
  }

  // GIF も、境目を超えたら再生しながら貯める形になり、何周も正しく再生できること。
  // 1 周目は順に復号し、2 周目からは貯めたものを伸ばす。どちらでも絵は番号と合う。
  void playsAStreamedGifForSeveralLoops() {
    const StreamingEverything streaming;
    heiran::ui::ViewerGridWidget grid;
    const QVector<QColor> colors = {Qt::red, Qt::green, Qt::blue};
    openFiles(&grid, {gif("streamed.gif", {0, 1, 2}, {11, 11, 11})});
    QVERIFY(grid.hasAnimation());
    QCOMPARE(grid.subImageCount(), 3);

    grid.setAnimationPlaying(true);

    int loops = 0;
    int lastIndex = 0;
    QElapsedTimer timer;
    timer.start();
    while (loops < 2 && timer.elapsed() < 6000) {
      QApplication::processEvents();
      const int index = grid.subImageIndex();
      if (index != lastIndex) {
        if (index == 0) {
          ++loops;
        }
        lastIndex = index;
        QTRY_COMPARE_WITH_TIMEOUT(shownColor(&grid), colors.at(index), 1500);
      }
    }
    QCOMPARE(loops, 2);
  }

  // 作り終えていないフレームがあるとき、絵を止めたまま番号だけが進まないこと。
  // 大きなフレームは作るのに時間がかかる。番号と絵がずれ続けると、何を見ているか分からない。
  // ここでは、番号が進んだ時点で、その番号の絵が出ていることを、何周も見て確かめる。
  void keepsTheShownPictureInStepWithTheIndex() {
    const StreamingEverything streaming;
    heiran::ui::ViewerGridWidget grid;
    const QVector<QColor> colors = {Qt::red,    Qt::green, Qt::blue,
                                    Qt::yellow, Qt::cyan,  Qt::magenta};
    openFiles(&grid, {apng("sync.png", colors, 11)});
    grid.setAnimationPlaying(true);

    int checked = 0;
    int lastIndex = -1;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2500) {
      QApplication::processEvents();
      const int index = grid.subImageIndex();
      if (index != lastIndex) {
        lastIndex = index;
        // 絵は、番号が変わった後、作り終えた時点で追い付く。追い付くまで待って合わせる。
        QTRY_COMPARE_WITH_TIMEOUT(shownColor(&grid), colors.at(index), 1500);
        ++checked;
      }
    }
    QVERIFY2(checked >= 5, qPrintable(QString::number(checked)));
  }

  // キャッシュから追い出されても、後から作るアニメーションが動き続けること。
  void keepsPlayingAStreamedAnimationEvictedFromTheCache() {
    const StreamingEverything streaming;
    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {apng("streamed-evicted.png", {Qt::red, Qt::green, Qt::blue}, 11)});

    grid.setDecodeCacheBudgetBytes(1);
    QCOMPARE(grid.decodeCacheEntryCount(), 0);
    grid.setAnimationPlaying(true);

    QVERIFY(grid.hasAnimation());
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() != 0, 3000);
  }

  // 別のファイルへ移って戻ったとき、先頭のフレームが出ること。
  // 後から作るフレームの窓は、先頭を落としていることがある。結果そのものが持つ先頭で見せる。
  void showsTheFirstFrameAgainAfterLeavingAndReturning() {
    const StreamingEverything streaming;
    QImage still(4, 4, QImage::Format_RGB32);
    still.fill(Qt::yellow);
    const QString png = QDir(directory_.path()).filePath("stream-neighbor.png");
    QVERIFY(still.save(png));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {apng("stream-away.png", {Qt::red, Qt::green, Qt::blue, Qt::white}, 20), png});
    // 先頭から離れた所まで進める。
    grid.setSubImageIndex(3);
    QTRY_COMPARE(shownColor(&grid), QColor(255, 255, 255));

    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));
    QCOMPARE(shownColor(&grid), QColor(255, 255, 0));
    grid.setCurrentPage(0);
    QVERIFY(waitForCellImages(&grid));

    QCOMPARE(grid.subImageIndex(), 0);
    QCOMPARE(shownColor(&grid), QColor(255, 0, 0));
  }

  // 別のファイルへ移っても、前のアニメーションの通知で、いまの絵が上書きされないこと。
  void ignoresNotificationsOfAnAnimationThatWasLeft() {
    const StreamingEverything streaming;
    QImage still(4, 4, QImage::Format_RGB32);
    still.fill(Qt::yellow);
    const QString png = QDir(directory_.path()).filePath("left-behind.png");
    QVERIFY(still.save(png));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {apng("left.png", {Qt::red, Qt::green, Qt::blue}, 20), png});
    grid.setSubImageIndex(2);
    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));

    // 作る側が遅れて通知を返しても、静止画のままであること。
    QTest::qWait(300);

    QCOMPARE(shownColor(&grid), QColor(255, 255, 0));
    QVERIFY(!grid.hasAnimation());
  }

  // 別のファイルへ移ったら、フレームは先頭へ戻ること。前のファイルの番号を引き継ぐと、
  // 次のファイルが途中のフレームから始まる。
  void resetsTheSubImageWhenTheFileChanges() {
    QImage still(4, 4, QImage::Format_RGB32);
    still.fill(Qt::yellow);
    const QString png = QDir(directory_.path()).filePath("b.png");
    QVERIFY(still.save(png));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {gif("a.gif", {0, 1, 2}, {20, 20, 20}), png});
    grid.setSubImageIndex(2);
    QCOMPARE(grid.subImageIndex(), 2);

    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));

    QCOMPARE(grid.subImageIndex(), 0);
    QCOMPARE(grid.subImageCount(), 1);
    QVERIFY(grid.subImageStatusText().isEmpty());
    QCOMPARE(shownColor(&grid), QColor(255, 255, 0));
  }

  // アニメーションの無いファイルでは何も動かないこと。再生の指定は残る。
  void doesNotAnimateAStillImage() {
    QImage still(4, 4, QImage::Format_RGB32);
    still.fill(Qt::yellow);
    const QString png = QDir(directory_.path()).filePath("still.png");
    QVERIFY(still.save(png));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {png});

    grid.setAnimationPlaying(true);
    QTest::qWait(300);

    QVERIFY(grid.isAnimationPlaying());
    QVERIFY(!grid.hasAnimation());
    QCOMPARE(grid.subImageIndex(), 0);
    QCOMPARE(grid.subImageKind(), SubImageKind::None);
  }

  // 長さの違うアニメーションを並べたとき、短い方が巻き戻って続くこと。
  void loopsTheShorterAnimationBesideALongerOne() {
    const QString longer = gif("long.gif", {0, 1, 2}, {20, 20, 20});
    const QString shorter = gif("short.gif", {2, 0}, {20, 20});

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(makeSession(
        {makeFileImageSet("long", {longer}), makeFileImageSet("short", {shorter})}, 1, 2, {0, 1}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    QCOMPARE(grid.subImageCount(), 3);

    grid.setSubImageIndex(2);

    // 長い方は 3 コマ目 (青)。短い方は 2 コマしかなく、2 % 2 = 0 コマ目 (青) へ巻き戻る。
    QCOMPARE(shownColor(&grid, 0), QColor(0, 0, 255));
    QCOMPARE(shownColor(&grid, 1), QColor(0, 0, 255));
    grid.setSubImageIndex(1);
    QCOMPARE(shownColor(&grid, 0), QColor(0, 255, 0));
    QCOMPARE(shownColor(&grid, 1), QColor(255, 0, 0));
  }

  // ---- 再生の同期方式 ----

  // 速さも長さも違う 2 つのアニメーションを並べる。左は速い 3 コマ (赤緑青)、
  // 右は遅い 2 コマ (青赤)。
  void openFastBesideSlow(heiran::ui::ViewerGridWidget *grid) {
    const QString fast = gif("sync_fast.gif", {0, 1, 2}, {11, 11, 11});
    const QString slow = gif("sync_slow.gif", {2, 0}, {200, 200});
    grid->resize(600, 300);
    grid->setSession(makeSession(
        {makeFileImageSet("fast", {fast}), makeFileImageSet("slow", {slow})}, 1, 2, {0, 1}));
    grid->show();
    QVERIFY(QTest::qWaitForWindowExposed(grid));
    QVERIFY(waitForCellImages(grid));
  }

  // 同期で固定すると、全セルが同じ番号で進むこと。先頭の速さに全体が従い、短い方は巻き戻る。
  void keepsEveryCellOnTheSameFrameWhenLocked() {
    heiran::ui::ViewerGridWidget grid;
    openFastBesideSlow(&grid);
    grid.setAnimationSyncMode(heiran::ui::AnimationSyncMode::Locked);
    QVERIFY(!grid.playsAnimationsIndependently());

    grid.setAnimationPlaying(true);
    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() == 1, 2000);

    // 右は 2 コマなので、1 コマ目は赤。遅い方の表示時間 (2 秒) には従わない。
    QCOMPARE(shownColor(&grid, 0), QColor(0, 255, 0));
    QCOMPARE(shownColor(&grid, 1), QColor(255, 0, 0));
  }

  // 独立にすると、速い方だけが進み、遅い方はそのまま残ること。
  void playsEachCellAtItsOwnSpeedWhenIndependent() {
    heiran::ui::ViewerGridWidget grid;
    openFastBesideSlow(&grid);
    grid.setAnimationSyncMode(heiran::ui::AnimationSyncMode::Independent);
    QVERIFY(grid.playsAnimationsIndependently());

    grid.setAnimationPlaying(true);
    // 速い方は進み、遅い方はまだ最初のコマ (青) のまま。
    QTRY_VERIFY_WITH_TIMEOUT(shownColor(&grid, 0) != QColor(255, 0, 0), 2000);
    QCOMPARE(shownColor(&grid, 1), QColor(0, 0, 255));
  }

  // 独立に再生していても、コマ送りは全セルを同じ番号へそろえること。
  void alignsEveryCellWhenSteppingWhilePlayingIndependently() {
    heiran::ui::ViewerGridWidget grid;
    openFastBesideSlow(&grid);
    grid.setAnimationSyncMode(heiran::ui::AnimationSyncMode::Independent);
    grid.setAnimationPlaying(true);
    QTRY_VERIFY_WITH_TIMEOUT(shownColor(&grid, 0) != QColor(255, 0, 0), 2000);
    grid.setAnimationPlaying(false);

    // 左の番号を基準に、右も同じ番号へ。いま左が n コマ目なら、n + 1 コマ目へそろう。
    const int base = grid.subImageIndex();
    grid.stepSubImage(1);
    const int next = (base + 1) % 3;
    QCOMPARE(grid.subImageIndex(), next);
    const QColor leftColors[] = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
    const QColor rightColors[] = {QColor(0, 0, 255), QColor(255, 0, 0)};
    QCOMPARE(shownColor(&grid, 0), leftColors[next]);
    QCOMPARE(shownColor(&grid, 1), rightColors[next % 2]);
  }

  // Auto は、フレーム数が同じなら同期し、違えば独立にすること。
  void resolvesAutoByTheFrameCounts() {
    {
      heiran::ui::ViewerGridWidget grid;
      openFastBesideSlow(&grid);  // 3 コマと 2 コマ。
      QCOMPARE(grid.animationSyncMode(), heiran::ui::AnimationSyncMode::Auto);
      QVERIFY(grid.playsAnimationsIndependently());
    }
    {
      const QString first = gif("auto_a.gif", {0, 1, 2}, {11, 11, 11});
      const QString second = gif("auto_b.gif", {2, 1, 0}, {30, 30, 30});
      heiran::ui::ViewerGridWidget grid;
      grid.resize(600, 300);
      grid.setSession(makeSession({makeFileImageSet("a", {first}), makeFileImageSet("b", {second})},
                                  1, 2, {0, 1}));
      grid.show();
      QVERIFY(QTest::qWaitForWindowExposed(&grid));
      QVERIFY(waitForCellImages(&grid));
      QVERIFY(!grid.playsAnimationsIndependently());
    }
  }

  // 再生中に方式を切り替えても、止まらないこと。同期へ戻るときは番号をそろえる。
  void keepsPlayingWhenTheSyncModeChanges() {
    heiran::ui::ViewerGridWidget grid;
    openFastBesideSlow(&grid);
    grid.setAnimationSyncMode(heiran::ui::AnimationSyncMode::Independent);
    grid.setAnimationPlaying(true);
    QTRY_VERIFY_WITH_TIMEOUT(shownColor(&grid, 0) != QColor(255, 0, 0), 2000);

    grid.setAnimationSyncMode(heiran::ui::AnimationSyncMode::Locked);
    const int index = grid.subImageIndex();
    const QColor leftColors[] = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
    const QColor rightColors[] = {QColor(0, 0, 255), QColor(255, 0, 0)};
    QCOMPARE(shownColor(&grid, 0), leftColors[index]);
    QCOMPARE(shownColor(&grid, 1), rightColors[index % 2]);

    QTRY_VERIFY_WITH_TIMEOUT(grid.subImageIndex() != index, 2000);
  }

  // ---- ページ ----

  // マルチページ TIFF のページを、番号で切り替えて見られること。
  // ページは独立した画像で、大きさも色も違う。
  void showsThePageOfAMultiPageTiff() {
    const QString tiff = writeFile(QDir(directory_.path()), "pages.tif",
                                   makeTiff({{QSize(10, 8), QColor(255, 0, 0)},
                                             {QSize(20, 10), QColor(0, 255, 0)},
                                             {QSize(6, 6), QColor(0, 0, 255)}}));
    QVERIFY(!tiff.isEmpty());

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {tiff});

    QCOMPARE(grid.subImageKind(), SubImageKind::Pages);
    QCOMPARE(grid.subImageCount(), 3);
    QVERIFY(!grid.hasAnimation());
    QCOMPARE(shownSize(&grid), QSize(10, 8));

    // ページは要るときに読むため、届くまで待つ。
    grid.setSubImageIndex(1);
    QTRY_COMPARE(shownSize(&grid), QSize(20, 10));
    QCOMPARE(shownColor(&grid), QColor(0, 255, 0));
    QCOMPARE(grid.subImageStatusText(), QString("Subpage 2 / 3"));

    grid.setSubImageIndex(2);
    QTRY_COMPARE(shownSize(&grid), QSize(6, 6));
    QCOMPARE(shownColor(&grid), QColor(0, 0, 255));
  }

  // ページは端で止まること。アニメーションと違い、巻き戻らない。
  void stopsTheTiffPagesAtTheEnds() {
    const QString tiff = writeFile(QDir(directory_.path()), "ends.tif",
                                   makeTiff({{QSize(4, 4), Qt::red}, {QSize(4, 4), Qt::green}}));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {tiff});

    grid.stepSubImage(-1);
    QCOMPARE(grid.subImageIndex(), 0);
    grid.setSubImageIndex(99);
    QCOMPARE(grid.subImageIndex(), 1);
    grid.stepSubImage(1);
    QCOMPARE(grid.subImageIndex(), 1);
  }

  // ページは再生しないこと。手でめくるもので、時間の流れではない。
  void doesNotPlayTiffPages() {
    const QString tiff = writeFile(QDir(directory_.path()), "still.tif",
                                   makeTiff({{QSize(4, 4), Qt::red}, {QSize(4, 4), Qt::green}}));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {tiff});

    grid.setAnimationPlaying(true);
    QTest::qWait(400);

    QCOMPARE(grid.subImageIndex(), 0);
  }

  // 1 ページの TIFF は普通の画像として扱うこと。番号も再生も要らない。
  void treatsASinglePageTiffAsAnOrdinaryImage() {
    const QString tiff =
        writeFile(QDir(directory_.path()), "one.tif", makeTiff({{QSize(4, 4), Qt::red}}));

    heiran::ui::ViewerGridWidget grid;
    openFiles(&grid, {tiff});

    QCOMPARE(grid.subImageKind(), SubImageKind::None);
    QCOMPARE(grid.subImageCount(), 1);
    QVERIFY(grid.subImageStatusText().isEmpty());
  }

private:
  QTemporaryDir directory_;
};

QTEST_MAIN(SubImageViewerTests)

#include "SubImageViewerTests.moc"
