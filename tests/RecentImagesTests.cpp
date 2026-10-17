#include "heiran/ui/RecentImages.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QtTest>

using heiran::ui::RecentImages;

class RecentImagesTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() {
    // 利用者の設定を書き換えないよう、テスト専用の保存先を使う。
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("RecentImagesTests");
    QSettings().clear();
  }
  void cleanupTestCase() { QSettings().clear(); }

  void putsTheNewestFirst() {
    QStringList recent;
    recent = RecentImages::pushed(recent, "/a.png");
    recent = RecentImages::pushed(recent, "/b.png");
    QCOMPARE(recent, QStringList({"/b.png", "/a.png"}));
  }

  // 開き直したものは先頭へ移すだけで、重複を作らないこと。
  void movesAnExistingEntryToTheFront() {
    const QStringList recent = {"/c.png", "/b.png", "/a.png"};
    QCOMPARE(RecentImages::pushed(recent, "/a.png"), QStringList({"/a.png", "/c.png", "/b.png"}));
  }

  // 綴りが違っても同じファイルなら重複にしないこと。
  void treatsDifferentSpellingsOfOnePathAsTheSame() {
    const QStringList recent = {"/d/sub/a.png"};
    QCOMPARE(RecentImages::pushed(recent, "/d/./sub//a.png").size(), 1);
  }

#if defined(Q_OS_WIN)
  // Windows では大文字小文字を区別しないこと。
  void ignoresCaseOnWindows() {
    const QStringList recent = {"C:/Photos/A.PNG"};
    QCOMPARE(RecentImages::pushed(recent, "c:/photos/a.png").size(), 1);
  }
#endif

  void dropsTheOldestBeyondTheLimit() {
    QStringList recent;
    for (int index = 0; index < 5; ++index) {
      recent = RecentImages::pushed(recent, QString("/%1.png").arg(index), 3);
    }
    QCOMPARE(recent, QStringList({"/4.png", "/3.png", "/2.png"}));
  }

  void ignoresAnEmptyPath() {
    const QStringList recent = {"/a.png"};
    QCOMPARE(RecentImages::pushed(recent, QString()), recent);
  }

  void removesAnEntry() {
    const QStringList recent = {"/c.png", "/b.png", "/a.png"};
    QCOMPARE(RecentImages::removed(recent, "/b.png"), QStringList({"/c.png", "/a.png"}));
    QCOMPARE(RecentImages::removed(recent, "/missing.png"), recent);
  }

  void keepsTheListAcrossSaveAndLoad() {
    RecentImages::save({"/b.png", "/a.png"});
    QCOMPARE(RecentImages::load(), QStringList({"/b.png", "/a.png"}));
  }

  // 保存する数を上限に収めること。設定が手で書き換えられていても膨らまない。
  void limitsWhatItLoads() {
    QStringList many;
    for (int index = 0; index < 30; ++index) {
      many.push_back(QString("/%1.png").arg(index));
    }
    QSettings().setValue("recent_images", many);
    QCOMPARE(RecentImages::load().size(), RecentImages::kMaxEntries);
  }
};

QTEST_MAIN(RecentImagesTests)

#include "RecentImagesTests.moc"
