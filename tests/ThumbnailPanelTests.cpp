#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/ThumbnailPanelWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QImage>
#include <QListWidget>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

namespace {
QStringList createImages(const QString &directory, int count, QColor color = Qt::red) {
  QStringList paths;
  for (int index = 0; index < count; ++index) {
    QImage image(60, 40, QImage::Format_RGB32);
    image.fill(color);
    const auto path = QDir(directory).filePath(QString("image%1.png").arg(index));
    if (!image.save(path)) {
      return {};
    }
    paths.push_back(path);
  }
  return paths;
}

QColor thumbnailColor(QListWidget *list) {
  if (list->count() == 0) {
    return {};
  }
  const auto image = list->item(0)->icon().pixmap(160, 120).toImage();
  return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}
}  // namespace

class ThumbnailPanelTests : public QObject {
  Q_OBJECT
  QTemporaryDir settingsDirectory_;

private Q_SLOTS:
  void initTestCase() {
    QVERIFY(settingsDirectory_.isValid());
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("ThumbnailPanelTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
  }

  void init() { QSettings().clear(); }

  void opensFromTheToolbarAndShortcutWithoutAModalWindow() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *action = window.findChild<QAction *>("thumbnailsAction");
    auto *dock = window.findChild<QDockWidget *>("thumbnailDock");
    QVERIFY(action && dock);
    QCOMPARE(action->text(), QString("Thumbnails"));
    QVERIFY(action->isCheckable());
    QVERIFY(!action->icon().isNull());
    QVERIFY(window.findChild<QToolBar *>("navigationToolBar")->actions().contains(action));
    QVERIFY(!dock->isVisible());
    action->trigger();
    QVERIFY(dock->isVisible());
    QVERIFY(!dock->isFloating());
    QVERIFY(!(dock->features() & QDockWidget::DockWidgetFloatable));
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    QCOMPARE(dock->findChild<QListWidget *>()->count(), 0);
    QTest::keyClick(&window, Qt::Key_T);
    QVERIFY(!dock->isVisible());
    QTest::keyClick(&window, Qt::Key_T);
    QVERIFY(dock->isVisible());
    dock->close();
    QVERIFY(!action->isChecked());
  }

  void followsPageSelectionFavoritesAndFolderChanges() {
    QTemporaryDir source, nextSource, metadata;
    const auto paths = createImages(source.path(), 3);
    const auto next = createImages(nextSource.path(), 2, Qt::green);
    QCOMPARE(paths.size(), 3);
    QCOMPARE(next.size(), 2);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    auto *action = window.findChild<QAction *>("thumbnailsAction");
    action->trigger();
    auto *dock = window.findChild<QDockWidget *>("thumbnailDock");
    auto *list = dock->findChild<QListWidget *>();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(list->count(), 3);
    list->setCurrentRow(2);
    QCOMPARE(grid->currentPage(), 2);
    QVERIFY(dock->isVisible());
    grid->previousPage();
    QCOMPARE(list->currentRow(), 1);
    grid->nextPage();
    auto *mark = window.findChild<QAction *>("favoriteAction");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    mark->trigger();
    grid->setCurrentPage(0);
    mark->trigger();
    filter->trigger();
    QCOMPARE(list->count(), 2);
    list->setCurrentRow(1);
    QCOMPARE(grid->currentPage(), 2);
    mark->trigger();
    QCOMPARE(list->count(), 1);
    QCOMPARE(grid->currentPage(), 0);
    mark->trigger();
    QCOMPARE(list->count(), 0);
    filter->trigger();
    QCOMPARE(list->count(), 3);
    QVERIFY(!window.isWindowModified());
    dock->close();
    window.openImageFiles({next.first()});
    QCOMPARE(list->count(), 3);
    action->trigger();
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->currentRow(), 0);
    QTRY_COMPARE(thumbnailColor(list), QColor(Qt::green));
  }

  void discardsOldThumbnailResultsAfterReplacingTheList() {
    QTemporaryDir red, blue;
    const auto first = createImages(red.path(), 20);
    const auto second = createImages(blue.path(), 1, Qt::blue);
    QCOMPARE(first.size(), 20);
    QCOMPARE(second.size(), 1);
    heiran::ui::ThumbnailPanelWidget panel;
    QSignalSpy selections(&panel, &heiran::ui::ThumbnailPanelWidget::pageSelected);
    panel.setPages(first, 10);
    panel.setPages(second, 0);
    auto *list = panel.findChild<QListWidget *>();
    QCOMPARE(list->count(), 1);
    QCOMPARE(selections.size(), 0);
    QTRY_COMPARE(thumbnailColor(list), QColor(Qt::blue));
    panel.cancelLoading();
    panel.setPages({}, -1);
    QCoreApplication::processEvents();
    QCOMPARE(list->count(), 0);
  }

  void restoresTheDockPositionAndVisibility() {
    {
      heiran::ui::MainWindow window;
      window.show();
      auto *dock = window.findChild<QDockWidget *>("thumbnailDock");
      window.addDockWidget(Qt::RightDockWidgetArea, dock);
      window.findChild<QAction *>("thumbnailsAction")->trigger();
      QVERIFY(dock->isVisible());
      window.close();
    }
    heiran::ui::MainWindow reopened;
    reopened.show();
    auto *dock = reopened.findChild<QDockWidget *>("thumbnailDock");
    QCOMPARE(reopened.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(dock->isVisible());
    QVERIFY(!dock->isFloating());
    QVERIFY(reopened.findChild<QAction *>("thumbnailsAction")->isChecked());
  }
};

QTEST_MAIN(ThumbnailPanelTests)
#include "ThumbnailPanelTests.moc"
