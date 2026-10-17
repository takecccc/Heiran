#include "heiran/ui/WelcomeWidget.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

using heiran::ui::WelcomeWidget;

class WelcomeWidgetTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 親の全体へ広がり、親の大きさに従う。
  void followsTheParentSize() {
    QWidget parent;
    parent.resize(500, 300);
    WelcomeWidget welcome(&parent);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QCOMPARE(welcome.geometry(), QRect(0, 0, 500, 300));
    parent.resize(800, 400);
    QCOMPARE(welcome.geometry(), QRect(0, 0, 800, 400));
  }

  // 開くボタン。
  void offersTheOpenButton() {
    QWidget parent;
    WelcomeWidget welcome(&parent);
    QSignalSpy spy(&welcome, &WelcomeWidget::openImageRequested);
    auto *button = welcome.findChild<QPushButton *>("welcomeOpen");
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(spy.count(), 1);
  }

  // 最近開いた画像: フォルダ名とファイル名で出し、全体の場所は説明へ。数は絞る。押すと、そのパスを返す。
  void listsTheRecentImages() {
    QWidget parent;
    WelcomeWidget welcome(&parent);
    QVERIFY(welcome.findChildren<QPushButton *>("welcomeRecent").isEmpty());
    QVERIFY(welcome.findChild<QWidget *>("welcomeRecentList")->isHidden());

    QStringList paths;
    for (int index = 0; index < 8; ++index) {
      paths.push_back(QString("C:/photos/shot%1.png").arg(index));
    }
    welcome.setRecentImages(paths);
    const auto buttons = welcome.findChildren<QPushButton *>("welcomeRecent");
    QCOMPARE(buttons.size(), WelcomeWidget::kMaxRecentImages);
    QCOMPARE(buttons.first()->text(), QString("photos/shot0.png"));
    QVERIFY(buttons.first()->toolTip().contains("photos"));

    QSignalSpy spy(&welcome, &WelcomeWidget::recentImageRequested);
    buttons.at(1)->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("C:/photos/shot1.png"));

    // 空へ戻すと、一覧は消える。
    welcome.setRecentImages({});
    QVERIFY(welcome.findChildren<QPushButton *>("welcomeRecent").isEmpty());
  }

  // ファイルのドロップを、案内の上でも受ける。ローカルのファイルが無ければ、受けない。
  void acceptsDroppedFiles() {
    QWidget parent;
    WelcomeWidget welcome(&parent);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QSignalSpy spy(&welcome, &WelcomeWidget::filesDropped);

    QMimeData files;
    files.setUrls({QUrl::fromLocalFile("C:/photos/a.png"), QUrl::fromLocalFile("C:/photos/b.png")});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&welcome, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&welcome, &drop);
    QCOMPARE(spy.count(), 1);
    QVERIFY(drop.isAccepted());
    QCOMPARE(spy.first().first().toStringList().size(), 2);

    QMimeData text;
    text.setText("not a file");
    QDropEvent textDrop(QPointF(10, 10), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&welcome, &textDrop);
    QVERIFY(!textDrop.isAccepted());
    QCOMPARE(spy.count(), 1);
  }
};

QTEST_MAIN(WelcomeWidgetTests)
#include "WelcomeWidgetTests.moc"
