#include "heiran/ui/PrefetchPlanner.hpp"

#include <QtTest>

class PrefetchPlannerTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 何も抱えていなければ見積もれない。上限まで読んで、抱えてから絞る。
  void usesTheMaximumRadiusBeforeAnythingIsCached() {
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(0, 0, 1024, 1, 4), 4);
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(3, 0, 1024, 1, 4), 4);
  }

  // 予算に入る枚数から距離が決まること。
  //
  // 1 ページ 100 の予算 1000 なら 10 ページ入る。現在ページを除いた 9 を前後へ
  // 振り分けて 4。
  void derivesTheRadiusFromHowManyPagesFit() {
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(10, 1000, 1000, 1, 8), 4);
  }

  // 上限を超えないこと。予算が広くても際限なく読んではいけない。
  void neverExceedsTheMaximumRadius() {
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(1, 10, 1'000'000, 1, 4), 4);
  }

  // 1 ページも入らない場合でも隣までは読むこと。
  //
  // ここを 0 にすると、ページ送りが毎回デコード待ちになる。入れ替わりを承知で
  // 1 枚先を持つ方が速い。
  void keepsAtLeastTheNeighbouringPage() {
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(1, 1000, 500, 1, 4), 1);
  }

  // セルの数だけ 1 ページの大きさが増えること。
  //
  // セルごとに別の画像を出す場合、1 ページで複数のエントリを使う。ここを
  // 見落とすと予算の数倍を読もうとする。
  void accountsForOneEntryPerCell() {
    // 1 エントリ 100、予算 1000。1 セルなら 10 ページ入って距離 4。
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(10, 1000, 1000, 1, 8), 4);
    // 4 セルなら 1 ページ 400 で 2 ページしか入らず、距離は 1 まで縮む。
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(10, 1000, 1000, 4, 8), 1);
  }

  // 予算が無ければ見積もれない。上限を返して落ちないこと。
  void toleratesAnEmptyBudget() {
    QCOMPARE(heiran::ui::prefetchRadiusForBudget(10, 1000, 0, 1, 4), 4);
  }

  // 近い順に並ぶこと。遠い方を先に読むと、いま要るページの到着が遅れる。
  void ordersPagesNearestFirst() {
    const QVector<int> pages = heiran::ui::prefetchPageOrder(5, 20, 3);
    QCOMPARE(pages, QVector<int>({4, 6, 3, 7, 2, 8}));
  }

  // 範囲の外は含めないこと。先頭と末尾で片側だけになる。
  void skipsPagesOutsideTheRange() {
    QCOMPARE(heiran::ui::prefetchPageOrder(0, 3, 3), QVector<int>({1, 2}));
    QCOMPARE(heiran::ui::prefetchPageOrder(2, 3, 3), QVector<int>({1, 0}));
  }

  // 読む必要が無い場合は空を返すこと。
  void returnsNothingWhenThereIsNothingToPrefetch() {
    QVERIFY(heiran::ui::prefetchPageOrder(0, 0, 3).isEmpty());
    QVERIFY(heiran::ui::prefetchPageOrder(0, 10, 0).isEmpty());
  }
};

QTEST_APPLESS_MAIN(PrefetchPlannerTests)

#include "PrefetchPlannerTests.moc"
