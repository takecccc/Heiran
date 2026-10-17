#include "heiran/ui/TransientMessageWidget.hpp"

#include <QApplication>
#include <QWidget>
#include <QtTest>

using heiran::ui::TransientMessageWidget;

class TransientMessageWidgetTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void isHiddenUntilAMessageArrives() {
    QWidget parent;
    TransientMessageWidget widget(&parent);
    QVERIFY(!widget.isVisibleTo(&parent));
  }

  void showsTheMessageInTheCorner() {
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    TransientMessageWidget widget(&parent);

    widget.showMessage("3 / 10  photo.png");

    QVERIFY(widget.isVisible());
    QCOMPARE(widget.text(), QString("3 / 10  photo.png"));
    // 左上の隅にあり、親の中に収まっている。
    QVERIFY(widget.geometry().left() >= 0 && widget.geometry().top() >= 0);
    QVERIFY(parent.rect().contains(widget.geometry()));
    QVERIFY(widget.geometry().left() < 100 && widget.geometry().top() < 100);
  }

  // 一定の時間で自分で消えること。出しっぱなしだと画像を隠し続ける。
  void hidesItselfAfterTheDuration() {
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    TransientMessageWidget widget(&parent);

    widget.showMessage("message", 60);
    QVERIFY(widget.isVisible());

    QTRY_VERIFY_WITH_TIMEOUT(!widget.isVisible(), 2000);
  }

  // 出ている最中に別のメッセージが来たら、内容を差し替えて消えるまでの時間を延ばすこと。
  // ページを続けて送るとき、最初のメッセージの時間で消えてしまうと最後の数字が読めない。
  void extendsTheTimeWhenTheMessageIsReplaced() {
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    TransientMessageWidget widget(&parent);

    widget.showMessage("first", 300);
    QTest::qWait(200);
    widget.showMessage("second", 300);
    QTest::qWait(200);

    // 最初の 300 ms は過ぎているが、差し替えたので残っている。
    QVERIFY(widget.isVisible());
    QCOMPARE(widget.text(), QString("second"));
    QTRY_VERIFY_WITH_TIMEOUT(!widget.isVisible(), 2000);
  }

  // マウスを通すこと。出ている間も画像のパンやズームを妨げない。
  void letsTheMouseThrough() {
    QWidget parent;
    TransientMessageWidget widget(&parent);
    QVERIFY(widget.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(widget.focusPolicy(), Qt::NoFocus);
  }
};

QTEST_MAIN(TransientMessageWidgetTests)

#include "TransientMessageWidgetTests.moc"
