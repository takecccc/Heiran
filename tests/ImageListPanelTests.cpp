#include "heiran/infra/ImageFileDetails.hpp"
#include "heiran/ui/ImageListPanelWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QLocale>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
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

class ImageListPanelTests : public QObject {
  Q_OBJECT
  QTemporaryDir settingsDirectory_;

private Q_SLOTS:
  void initTestCase() {
    QVERIFY(settingsDirectory_.isValid());
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("ImageListPanelTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
  }

  void init() { QSettings().clear(); }

  void opensFromTheToolbarAndShortcutWithoutAModalWindow() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *action = window.findChild<QAction *>("imageListAction");
    auto *dock = window.findChild<QDockWidget *>("thumbnailDock");
    QVERIFY(action && dock);
    QCOMPARE(action->text(), QString("Image List"));
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
    auto *action = window.findChild<QAction *>("imageListAction");
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
    heiran::ui::ImageListPanelWidget panel;
    QSignalSpy selections(&panel, &heiran::ui::ImageListPanelWidget::pageSelected);
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
      window.findChild<QAction *>("imageListAction")->trigger();
      QVERIFY(dock->isVisible());
      window.close();
    }
    heiran::ui::MainWindow reopened;
    reopened.show();
    auto *dock = reopened.findChild<QDockWidget *>("thumbnailDock");
    QCOMPARE(reopened.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(dock->isVisible());
    QVERIFY(!dock->isFloating());
    QVERIFY(reopened.findChild<QAction *>("imageListAction")->isChecked());
  }

  void filtersFromThePanelAndSynchronizesTheGlobalAction() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 3);
    QCOMPARE(paths.size(), 3);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *mark = window.findChild<QAction *>("favoriteAction");
    mark->trigger();
    grid->setCurrentPage(2);
    mark->trigger();
    auto *checkbox = window.findChild<QCheckBox *>("imageListFavoritesOnly");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    checkbox->setChecked(true);
    QVERIFY(filter->isChecked());
    QCOMPARE(grid->navigationPageCount(), 2);
    grid->previousPage();
    QCOMPARE(grid->currentPage(), 0);
    window.findChild<QComboBox *>("imageListViewMode")->setCurrentIndex(1);
    auto *table = window.findChild<QTreeWidget *>("imageDetailsList");
    QCOMPARE(table->topLevelItemCount(), 2);
    table->setCurrentItem(table->topLevelItem(1));
    QCOMPARE(grid->currentPage(), 2);
    filter->trigger();
    QVERIFY(!checkbox->isChecked());
    QCOMPARE(table->topLevelItemCount(), 3);
    QVERIFY(!window.isWindowModified());
  }

  void displaysOriginalDimensionsAndFileSizesWithoutThumbnails() {
    QTemporaryDir source;
    const auto paths = createImages(source.path(), 2);
    QCOMPARE(paths.size(), 2);
    heiran::ui::ImageListPanelWidget panel;
    panel.setDetailsVisible(true);
    panel.setPages(paths, 0);
    auto *table = panel.findChild<QTreeWidget *>();
    QCOMPARE(table->columnCount(), 4);
    auto *row = table->topLevelItem(0);
    QCOMPARE(row->text(0), QFileInfo(paths.first()).fileName());
    QTRY_COMPARE(row->text(1), QString("60"));
    QCOMPARE(row->text(2), QString("40"));
    QCOMPARE(row->text(3), QLocale().formattedDataSize(QFileInfo(paths.first()).size(), 1));
    const auto placeholder =
        panel.findChild<QListWidget *>()->item(0)->icon().pixmap(160, 120).toImage();
    QCOMPARE(placeholder.pixelColor(placeholder.width() / 2, placeholder.height() / 2).alpha(), 0);
    QSignalSpy selected(&panel, &heiran::ui::ImageListPanelWidget::pageSelected);
    table->setCurrentItem(table->topLevelItem(1));
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().first().toInt(), 1);
    panel.setCurrentPage(0);
    QCOMPARE(selected.size(), 1);
    panel.setDetailsVisible(false);
    QCOMPARE(panel.findChild<QListWidget *>()->currentRow(), 0);
  }

  void handlesMissingAndCorruptFileDetails() {
    QTemporaryDir source;
    QFile corrupt(source.filePath("corrupt.png"));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("broken"), qint64(6));
    corrupt.close();
    const auto data = heiran::infra::readImageFileDetails(corrupt.fileName());
    QCOMPARE(data.fileSize, qint64(6));
    QVERIFY(data.dimensions.isEmpty());
    const auto missing = heiran::infra::readImageFileDetails(source.filePath("missing.png"));
    QCOMPARE(missing.fileSize, qint64(-1));
    QVERIFY(missing.dimensions.isEmpty());
    heiran::ui::ImageListPanelWidget panel;
    panel.setDetailsVisible(true);
    panel.setPages({corrupt.fileName()}, 0);
    auto *table = panel.findChild<QTreeWidget *>();
    QTRY_COMPARE(table->topLevelItem(0)->text(1), QStringLiteral("\u2014"));
  }

  void restoresTheDetailsViewMode() {
    {
      heiran::ui::MainWindow window;
      window.findChild<QComboBox *>("imageListViewMode")->setCurrentIndex(1);
    }
    heiran::ui::MainWindow reopened;
    QCOMPARE(reopened.findChild<QComboBox *>("imageListViewMode")->currentIndex(), 1);
  }

  void readsNativeFormatDimensions() {
    QTemporaryDir source;
    QFile file(source.filePath("native.hdr"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n");
    bytes.append(QByteArray::fromHex("8040208180402081"));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    const auto data = heiran::infra::readImageFileDetails(file.fileName());
    QCOMPARE(data.dimensions, QSize(2, 1));
    QCOMPARE(data.fileSize, qint64(bytes.size()));
  }

  void discardsOldDetailsAfterReplacingTheList() {
    QTemporaryDir first, second;
    const auto oldPaths = createImages(first.path(), 12);
    const auto newPath = second.filePath("new.png");
    QImage image(120, 90, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(newPath));
    heiran::ui::ImageListPanelWidget panel;
    panel.setDetailsVisible(true);
    panel.setPages(oldPaths, 5);
    panel.setPages({newPath}, 0);
    auto *table = panel.findChild<QTreeWidget *>();
    QCOMPARE(table->topLevelItemCount(), 1);
    QTRY_COMPARE(table->topLevelItem(0)->text(1), QString("120"));
    QCOMPARE(table->topLevelItem(0)->text(2), QString("90"));
    QCOMPARE(table->topLevelItem(0)->text(0), QString("new.png"));
  }
};

QTEST_MAIN(ImageListPanelTests)
#include "ImageListPanelTests.moc"
