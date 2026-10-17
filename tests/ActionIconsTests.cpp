// 項目のアイコン (ActionIcons)。

#include "heiran/ui/ActionIcons.hpp"

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QPalette>
#include <QtTest>

using heiran::ui::actionIconNames;
using heiran::ui::applyActionIcons;
using heiran::ui::themedIcon;

namespace {

int opaquePixelCount(const QImage &image) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      count += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
    }
  }
  return count;
}

}  // namespace

class ActionIconsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 対応表のアイコンが、すべて資源に入っていること。名前の書き間違いや、入れ忘れを防ぐ。
  void hasAResourceForEveryMappedIcon() {
    const auto &names = actionIconNames();
    QVERIFY(!names.isEmpty());
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
      QVERIFY2(QFile::exists(QString(":/icons/%1.svg").arg(it.value())),
               qPrintable(QString("%1 -> %2").arg(it.key(), it.value())));
    }
  }

  // 描くと、形が出ること (空の画像ではない)。
  void rendersAShape() {
    const QImage image = themedIcon("arrow-left").pixmap(QSize(24, 24)).toImage();
    QVERIFY(opaquePixelCount(image) > 20);
  }

  // 無効のときは、通常と違う色で描くこと。
  void drawsDisabledIconsInAnotherColor() {
    const QIcon icon = themedIcon("arrow-left");
    const QImage normal = icon.pixmap(QSize(24, 24), QIcon::Normal).toImage();
    const QImage disabled = icon.pixmap(QSize(24, 24), QIcon::Disabled).toImage();
    QVERIFY(normal != disabled);
  }

  // チェック中は、強調色で描くこと。
  void drawsCheckedIconsInAnotherColor() {
    const QIcon icon = themedIcon("scan-line");
    const QImage off = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::Off).toImage();
    const QImage on = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::On).toImage();
    QVERIFY(off != on);
  }

  // 対応表に従って付くこと。もともとアイコンを持つ項目は、上書きしない。表にない項目は付けない。
  void appliesIconsByText() {
    QMenu menu;
    auto *copy = menu.addAction("Copy Image");
    auto *other = menu.addAction("Something Else");
    auto *own = menu.addAction("Save");
    own->setIcon(themedIcon("x"));
    const QIcon keptIcon = own->icon();

    applyActionIcons(&menu);

    QVERIFY(!copy->icon().isNull());
    QVERIFY(other->icon().isNull());
    QCOMPARE(own->icon().cacheKey(), keptIcon.cacheKey());
  }

  // 下位のメニューの項目にも付くこと。
  void appliesIconsToSubmenus() {
    QMenu menu;
    auto *submenu = menu.addMenu("Scale");
    QVERIFY(submenu->menuAction()->icon().isNull());

    applyActionIcons(&menu);

    QVERIFY(!submenu->menuAction()->icon().isNull());
  }
};

QTEST_MAIN(ActionIconsTests)

#include "ActionIconsTests.moc"
