#include "heiran/ui/DisplayLink.hpp"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QtTest>

class DisplayLinkTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 合図が出る場合は `isTicking()` が真になり、出ない場合は偽のままであること。
  //
  // **合図が出ること自体は要求できない。** 遠隔接続や仮想ディスプレイでは vblank を
  // 待てず、刻まないのが正しい振る舞いになる。要求できるのは、刻んでいないのに
  // 刻んでいると言わないことの方。呼び側はこの旗を見て待つかどうかを決める。
  //
  // **受け手は `link` 自身にする。** 合図は別スレッドから queued で積まれる。`this` を
  // 受け手にすると、関数を抜けた後もイベントが残り、次のテストの実行中に消えた
  // スタック変数へ書き込む。落ちる場所は書き込まれた側になり、原因から離れる。
  void reportsWhetherItIsTicking() {
#if !defined(Q_OS_WIN)
    QSKIP("the display link waits for the vertical blank, which is Windows only");
#endif
    heiran::ui::DisplayLink link;

    int tickCount = 0;
    QObject::connect(&link, &heiran::ui::DisplayLink::ticked, &link,
                     [&tickCount]() { ++tickCount; });

    QTest::qWaitFor([&tickCount]() { return tickCount > 0; }, 2000);
    if (tickCount == 0) {
      QVERIFY2(!link.isTicking(), "no tick arrived but the link claims to be ticking");
      return;
    }
    QVERIFY(link.isTicking());
  }

  // 合図の間隔が画面の更新に近いこと。
  //
  // 数だけ見ても、空回りで出しているのか待って出しているのかが分からない。
  void ticksAtRoughlyTheScreenRefreshRate() {
#if !defined(Q_OS_WIN)
    QSKIP("the display link waits for the vertical blank, which is Windows only");
#endif
    heiran::ui::DisplayLink link;

    int tickCount = 0;
    QObject::connect(&link, &heiran::ui::DisplayLink::ticked, &link,
                     [&tickCount]() { ++tickCount; });
    if (!QTest::qWaitFor([&tickCount]() { return tickCount > 0; }, 2000)) {
      QSKIP("this environment cannot wait for the vertical blank");
    }

    // 立ち上がりを外してから数える。
    tickCount = 0;
    QElapsedTimer measureTimer;
    measureTimer.start();
    QTest::qWait(500);
    const double elapsedSeconds = measureTimer.elapsed() / 1000.0;
    const double tickRate = tickCount / elapsedSeconds;

    // 合成の更新レートは画面のものと一致するとは限らないため、幅を広く取る。
    // 空回り (数千) と、待てていない状態 (0) を弾ければよい。
    QVERIFY2(tickRate > 20.0 && tickRate < 1000.0,
             qPrintable(QString("%1 ticks per second").arg(tickRate)));
    QVERIFY(link.isTicking());
  }

  // 壊さずに畳めること。待っている最中に消えるため、取り残すと落ちる。
  //
  // 合図が出るかどうかは環境による。ここで見たいのは畳めることだけなので、
  // 合図は待たない。待つと合図の出ない環境で意味が変わってしまう。
  void stopsCleanlyWhileWaiting() {
#if !defined(Q_OS_WIN)
    QSKIP("the display link waits for the vertical blank, which is Windows only");
#endif
    for (int attempt = 0; attempt < 3; ++attempt) {
      auto link = std::make_unique<heiran::ui::DisplayLink>();
      QTest::qWait(50);
      link.reset();
    }
  }

  // 共有のものは同じ実体を返すこと。セルの数だけスレッドを立ててはいけない。
  void sharesASingleInstance() {
    QCOMPARE(&heiran::ui::DisplayLink::shared(), &heiran::ui::DisplayLink::shared());
  }
};

QTEST_MAIN(DisplayLinkTests)

#include "DisplayLinkTests.moc"
