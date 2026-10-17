#include "heiran/ui/TextureBudgetLedger.hpp"

#include <QtTest>

using heiran::ui::TextureBudgetLedger;

namespace {

// 持ち主のふりをする。台帳は持ち主の実体に触れないため、アドレスだけあればよい。
struct FakeOwner {
  QStringList evicted;
};

void registerOwner(TextureBudgetLedger *ledger, FakeOwner *owner) {
  ledger->registerOwner(owner, [owner](const QString &key) { owner->evicted.push_back(key); });
}

}  // namespace

class TextureBudgetLedgerTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void countsWhatItIsGiven() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.setBudgetBytes(1000);

    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "b", 200);

    QCOMPARE(ledger.usedBytes(), qint64{300});
    QCOMPARE(ledger.entryCount(), 2);
    QVERIFY(owner.evicted.isEmpty());
  }

  // 使い直しでは数え直さないこと。同じテクスチャを二重に数えると、予算が実際より早く尽きる。
  void doesNotCountAnEntryTwice() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);

    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "a", 100);

    QCOMPARE(ledger.usedBytes(), qint64{100});
    QCOMPARE(ledger.entryCount(), 1);
  }

  void dropsTheLeastRecentlyUsedFirst() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.setBudgetBytes(250);

    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "b", 100);
    ledger.markUsed(&owner, "c", 100);

    // a が最も古い。c は描いているため残る。
    QCOMPARE(owner.evicted, QStringList({"a"}));
    QCOMPARE(ledger.usedBytes(), qint64{200});
  }

  // 使い直したものは新しい扱いになること。戻る操作で使うものを捨てないため。
  void reusingAnEntryMakesItNewer() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.setBudgetBytes(250);

    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "b", 100);
    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "c", 100);

    QCOMPARE(owner.evicted, QStringList({"b"}));
  }

  // 持ち主をまたいで、全体で最も古いものから捨てること。
  // セルごとの上限では、少ないセルの余りを他のセルが使えなかった。
  void dropsTheOldestAcrossOwners() {
    TextureBudgetLedger ledger;
    FakeOwner first;
    FakeOwner second;
    registerOwner(&ledger, &first);
    registerOwner(&ledger, &second);
    ledger.setBudgetBytes(250);

    ledger.markUsed(&first, "first-old", 100);
    ledger.markUsed(&second, "second-old", 100);
    // first が新しいものを載せる。捨てられるのは全体で最も古い first-old で、
    // second の分は残る。
    ledger.markUsed(&first, "first-new", 100);

    QCOMPARE(first.evicted, QStringList({"first-old"}));
    QVERIFY(second.evicted.isEmpty());
  }

  // 別の持ち主の分も捨てられ、その持ち主へ伝わること。消すのは持ち主の仕事になる。
  void tellsAnotherOwnerToDropItsOwnEntry() {
    TextureBudgetLedger ledger;
    FakeOwner first;
    FakeOwner second;
    registerOwner(&ledger, &first);
    registerOwner(&ledger, &second);
    ledger.setBudgetBytes(250);

    ledger.markUsed(&second, "second-old", 100);
    ledger.markUsed(&second, "second-now", 100);
    ledger.markUsed(&first, "first-now", 100);

    QCOMPARE(second.evicted, QStringList({"second-old"}));
    QVERIFY(first.evicted.isEmpty());
  }

  // 描いているものは、予算を超えていても捨てないこと。画面に出ているものを消さない。
  void neverDropsWhatEachOwnerIsDrawing() {
    TextureBudgetLedger ledger;
    FakeOwner first;
    FakeOwner second;
    registerOwner(&ledger, &first);
    registerOwner(&ledger, &second);
    ledger.setBudgetBytes(50);

    ledger.markUsed(&first, "first-now", 100);
    ledger.markUsed(&second, "second-now", 100);

    QVERIFY(first.evicted.isEmpty());
    QVERIFY(second.evicted.isEmpty());
    QCOMPARE(ledger.usedBytes(), qint64{200});
  }

  // 捨てると決めたものを使い直したら、数え直して保護すること。
  // 消すのが次の描画まで遅れるため、その間に使い直される。
  void countsAnEvictedEntryAgainWhenItIsUsedAgain() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.setBudgetBytes(150);

    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "b", 100);
    QCOMPARE(owner.evicted, QStringList({"a"}));
    QCOMPARE(ledger.usedBytes(), qint64{100});

    ledger.markUsed(&owner, "a", 100);

    // a が描いているものになり、b が捨てられる。
    QCOMPARE(owner.evicted, QStringList({"a", "b"}));
    QCOMPARE(ledger.usedBytes(), qint64{100});
  }

  void loweringTheBudgetDropsTheOverflowImmediately() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.markUsed(&owner, "a", 100);
    ledger.markUsed(&owner, "b", 100);
    ledger.markUsed(&owner, "c", 100);

    ledger.setBudgetBytes(150);

    QCOMPARE(owner.evicted, QStringList({"a", "b"}));
    QCOMPARE(ledger.usedBytes(), qint64{100});
  }

  void raisingTheBudgetDropsNothing() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.setBudgetBytes(150);
    ledger.markUsed(&owner, "a", 100);

    ledger.setBudgetBytes(10'000);

    QVERIFY(owner.evicted.isEmpty());
  }

  // 持ち主が消えたら、その分を数えないこと。消えたセルの分が残ると予算が漏れる。
  void forgetsAnOwnerThatWasUnregistered() {
    TextureBudgetLedger ledger;
    FakeOwner first;
    FakeOwner second;
    registerOwner(&ledger, &first);
    registerOwner(&ledger, &second);
    ledger.markUsed(&first, "a", 100);
    ledger.markUsed(&second, "b", 200);

    ledger.unregisterOwner(&first);

    QCOMPARE(ledger.usedBytes(), qint64{200});
    QCOMPARE(ledger.entryCount(), 1);
  }

  // 登録を外した持ち主へは、退避を伝えないこと。破棄済みのオブジェクトを呼ばない。
  void doesNotNotifyAnOwnerThatWasUnregistered() {
    TextureBudgetLedger ledger;
    FakeOwner first;
    FakeOwner second;
    registerOwner(&ledger, &first);
    registerOwner(&ledger, &second);
    ledger.setBudgetBytes(150);
    ledger.markUsed(&first, "a", 100);
    ledger.unregisterOwner(&first);

    ledger.markUsed(&second, "b", 100);
    ledger.markUsed(&second, "c", 100);

    QVERIFY(first.evicted.isEmpty());
    QCOMPARE(second.evicted, QStringList({"b"}));
  }

  // 解放しても登録は残り、あとから載せ直せること。GL のコンテキストを作り直す場合。
  void clearedOwnerCanBeUsedAgain() {
    TextureBudgetLedger ledger;
    FakeOwner owner;
    registerOwner(&ledger, &owner);
    ledger.markUsed(&owner, "a", 100);

    ledger.clearOwner(&owner);
    QCOMPARE(ledger.usedBytes(), qint64{0});

    ledger.markUsed(&owner, "a", 100);
    QCOMPARE(ledger.usedBytes(), qint64{100});
  }

  void sharedLedgerIsASingleInstance() {
    QCOMPARE(&TextureBudgetLedger::shared(), &TextureBudgetLedger::shared());
  }
};

QTEST_APPLESS_MAIN(TextureBudgetLedgerTests)

#include "TextureBudgetLedgerTests.moc"
