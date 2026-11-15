#include "heiran/infra/FavoriteExporter.hpp"
#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/ui/FavoritesController.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/PageNavigationFilter.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

QStringList createImages(const QString &directory, int count) {
  QStringList paths;
  for (int index = 0; index < count; ++index) {
    const auto path = QDir(directory).filePath(QString("image%1.png").arg(index));
    QImage image(24, 16, QImage::Format_RGBA8888);
    image.fill(QColor(30 + index * 20, 80, 120));
    if (!image.save(path))
      return {};
    paths.push_back(path);
  }
  return paths;
}
}  // namespace

class FavoritesTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void marksTheImageInTheRightClickedCell() {
    QTemporaryDir leftDirectory, rightDirectory, metadata;
    const auto left = createImages(leftDirectory.path(), 1);
    const auto right = createImages(rightDirectory.path(), 1);
    QCOMPARE(left.size(), 1);
    QCOMPARE(right.size(), 1);
    const auto database = metadata.filePath("favorites.json");
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({left.first(), right.first()});
    window.show();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 2);
    QTRY_VERIFY(!cells.last()->currentFilePath().isEmpty());
    auto *canvas = cells.last()->findChild<QWidget *>("imageCanvas");
    QVERIFY(canvas);
    bool offered = false;
    QTimer::singleShot(0, [&]() {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      for (auto *action : menu->actions()) {
        if (action->text() == "Favorite" && action->isEnabled()) {
          offered = action->isCheckable() && !action->isChecked();
          menu->setActiveAction(action);
          QTest::keyClick(menu, Qt::Key_Return);
          return;
        }
      }
      menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5),
                            canvas->mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(canvas, &event);
    QVERIFY(offered);
    heiran::infra::FavoriteStore saved(database);
    QVERIFY(saved.load());
    QVERIFY(saved.contains(right.first()));
    QVERIFY(!saved.contains(left.first()));
    QVERIFY(!window.isWindowModified());
  }

  void storesMarksAndMergesOtherWriters() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto database = directory.filePath("favorites.json");
    const auto a = directory.filePath("a.png");
    const auto b = directory.filePath("b.png");
    heiran::infra::FavoriteStore first(database), second(database);
    QVERIFY(first.load());
    QVERIFY(second.load());
    QVERIFY(first.setFavorite(a, true));
    QVERIFY(second.setFavorite(b, true));
    QVERIFY(first.load());
    QVERIFY(first.contains(a));
    QVERIFY(first.contains(b));
    QVERIFY(first.setFavorite(a, false));
    QVERIFY(second.load());
    QVERIFY(!second.contains(a));
    QVERIFY(second.contains(b));
#ifdef Q_OS_WIN
    QVERIFY(second.contains(b.toUpper()));
#endif
  }

  void preservesCorruptAndUnsupportedDatabases() {
    QTemporaryDir directory;
    for (const QByteArray data : {QByteArray("{"), QByteArray("{\"version\":2,\"files\":[]}")}) {
      const auto database = directory.filePath("favorites.json");
      QVERIFY(writeFile(database, data));
      heiran::infra::FavoriteStore store(database);
      QString error;
      QVERIFY(!store.load(&error));
      QVERIFY(!error.isEmpty());
      QVERIFY(!store.setFavorite(directory.filePath("a.png"), true, &error));
      QCOMPARE(readFile(database), data);
    }
  }

  void copiesBytesWithoutOverwritingOrDuplicatingSources() {
    QTemporaryDir first, second, destination;
    const auto a = first.filePath("same.png"), b = second.filePath("same.png");
    QVERIFY(writeFile(a, QByteArray("original-a\0pixels", 17)));
    QVERIFY(writeFile(b, "original-b"));
    QVERIFY(writeFile(destination.filePath("same.png"), "existing"));
    const std::atomic_bool cancel(false);
    const auto result = heiran::infra::copyFavoriteFiles({a, b, a}, destination.path(), cancel);
    QCOMPARE(result.copiedPaths.size(), 2);
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(readFile(destination.filePath("same.png")), QByteArray("existing"));
    QCOMPARE(readFile(result.copiedPaths.first()), readFile(a));
    QCOMPARE(readFile(result.copiedPaths.last()), readFile(b));
    QCOMPARE(readFile(a), QByteArray("original-a\0pixels", 17));
    QVERIFY(QDir(destination.path())
                .entryList({".heiran-copy-*"}, QDir::Files | QDir::Hidden)
                .isEmpty());
    const auto sameDirectory = heiran::infra::copyFavoriteFiles({a}, first.path(), cancel);
    QCOMPARE(sameDirectory.skipped, 1);
    QVERIFY(sameDirectory.copiedPaths.isEmpty());
  }

  void cancelsCopiesAndReportsMissingFiles() {
    QTemporaryDir directory, destination;
    std::atomic_bool cancel(true);
    const auto missing = directory.filePath("missing.png");
    const auto cancelled = heiran::infra::copyFavoriteFiles({missing}, destination.path(), cancel);
    QVERIFY(cancelled.cancelled);
    QVERIFY(cancelled.copiedPaths.isEmpty());
    cancel.store(false);
    const auto failed = heiran::infra::copyFavoriteFiles({missing}, destination.path(), cancel);
    QCOMPARE(failed.errors.size(), 1);
    QVERIFY(QDir(destination.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
  }

  void normalizesPageFiltersAndKeepsNativePageNumbers() {
    heiran::ui::PageNavigationFilter filter;
    QCOMPARE(filter.count(5), 5);
    filter.setPages(QVector<int>{4, 1, 1, -1, 100});
    QCOMPARE(filter.count(5), 2);
    QCOMPARE(filter.pageAt(0, 5), 1);
    QCOMPARE(filter.pageAt(1, 5), 4);
    QCOMPARE(filter.indexOf(2, 5), 1);
    filter.setPages(QVector<int>{});
    QCOMPARE(filter.count(5), 0);
    QCOMPARE(filter.pageAt(0, 5), -1);
  }

  void marksFiltersAndRestoresWithoutChangingTheSession() {
    QTemporaryDir directory;
    const auto paths = createImages(directory.path(), 4);
    QCOMPARE(paths.size(), 4);
    const auto database = directory.filePath("favorites.json");
    {
      heiran::ui::MainWindow window(nullptr, database);
      window.openImageFiles({paths.first()});
      auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
      auto *mark = window.findChild<QAction *>("favoriteAction");
      auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
      auto *selector = window.findChild<QComboBox *>("pageSelector");
      QVERIFY(grid && mark && filter && selector);
      mark->trigger();
      grid->setCurrentPage(2);
      mark->trigger();
      QVERIFY(!window.isWindowModified());
      filter->trigger();
      QCOMPARE(selector->count(), 2);
      QCOMPARE(selector->itemData(1).toInt(), 2);
      grid->previousPage();
      QCOMPARE(grid->currentPage(), 0);
      grid->nextPage();
      QCOMPARE(grid->currentPage(), 2);
      grid->stepPages(-20);
      QCOMPARE(grid->currentPage(), 0);
      selector->setCurrentIndex(1);
      QCOMPARE(grid->currentPage(), 2);
      auto *compact = window.findChild<QComboBox *>("compactPageSelector");
      compact->setCurrentIndex(0);
      QCOMPARE(grid->currentPage(), 0);
      auto *headers = window.findChild<QAction *>("cellHeadersAction");
      headers->setChecked(false);
      window.resize(800, 500);
      window.show();
      QVERIFY(QTest::qWaitForWindowExposed(&window));
      auto *badge = window.findChild<QLabel *>("favoriteBadge");
      QVERIFY(badge && badge->isVisible());
      mark->trigger();
      QCOMPARE(grid->currentPage(), 2);
      mark->trigger();
      QCOMPARE(grid->navigationPageCount(), 0);
      QVERIFY(!mark->isEnabled());
      QVERIFY(grid->targetCellFilePath().isEmpty());
      for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
        QCOMPARE(cell->cellStatus(), heiran::ui::CellStatus::Empty);
      }
      filter->trigger();
      QCOMPARE(grid->navigationPageCount(), 4);
      mark->trigger();
      QCOMPARE(grid->sessionSnapshot().images.first().resolvedPaths, paths);
      QVERIFY(!window.isWindowModified());
      headers->setChecked(true);
    }
    heiran::ui::MainWindow reopened(nullptr, database);
    reopened.openImageFiles({paths.first()});
    QVERIFY(reopened.findChild<QAction *>("favoriteAction")->isChecked());
    QVERIFY(reopened.findChild<heiran::ui::FavoritesController *>()->favoriteFiles().contains(
        paths.first()));
  }

  void preservesComparisonPageAlignmentAndCopyScope() {
    QTemporaryDir a, b, metadata;
    const auto left = createImages(a.path(), 3), right = createImages(b.path(), 3);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    QVERIFY(store.setFavorite(right.at(1), true));
    QVERIFY(store.setFavorite(metadata.filePath("outside.png"), true));
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({left.first(), right.first()});
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    window.findChild<QAction *>("favoritesOnlyAction")->trigger();
    QCOMPARE(grid->navigationPageCount(), 1);
    QCOMPARE(grid->currentPage(), 1);
    QCOMPARE(grid->sessionSnapshot().images.first().resolvedPaths, left);
    QCOMPARE(grid->sessionSnapshot().images.last().resolvedPaths, right);
    const auto favorites = window.findChild<heiran::ui::FavoritesController *>()->favoriteFiles();
    QCOMPARE(favorites, QStringList{right.at(1)});
    QVERIFY(!window.isWindowModified());
  }
};

QTEST_MAIN(FavoritesTests)
#include "FavoritesTests.moc"
