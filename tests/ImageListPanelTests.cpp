#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/infra/ImageFileDetails.hpp"
#include "heiran/ui/ImageListDragViews.hpp"
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
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QtTest>
#include <memory>

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
    QCOMPARE(table->columnCount(), 5);
    auto *row = table->topLevelItem(0);
    QCOMPARE(row->text(1), QFileInfo(paths.first()).fileName());
    QTRY_COMPARE(row->text(2), QString("60"));
    QCOMPARE(row->text(3), QString("40"));
    QCOMPARE(row->text(4), QLocale().formattedDataSize(QFileInfo(paths.first()).size(), 1));
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
    QTRY_COMPARE(table->topLevelItem(0)->text(2), QStringLiteral("\u2014"));
  }

  void restoresTheDetailsViewMode() {
    {
      heiran::ui::MainWindow window;
      window.findChild<QComboBox *>("imageListViewMode")->setCurrentIndex(1);
    }
    heiran::ui::MainWindow reopened;
    QCOMPARE(reopened.findChild<QComboBox *>("imageListViewMode")->currentIndex(), 1);
  }

  void updatesFavoriteMarkersImmediatelyInBothViews() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 3);
    QCOMPARE(paths.size(), 3);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    auto *open = window.findChild<QAction *>("imageListAction");
    open->trigger();
    auto *list = window.findChild<QListWidget *>("thumbnailList");
    auto *table = window.findChild<QTreeWidget *>("imageDetailsList");
    auto *mark = window.findChild<QAction *>("favoriteAction");
    const auto star = QStringLiteral("\u2605");
    QVERIFY(!list->item(0)->text().startsWith(star));
    mark->trigger();
    QVERIFY(list->item(0)->text().startsWith(star));
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    QCOMPARE(list->count(), 3);
    window.findChild<QComboBox *>("imageListViewMode")->setCurrentIndex(1);
    QTRY_COMPARE(table->topLevelItem(0)->text(2), QString("60"));
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    mark->trigger();
    QVERIFY(!list->item(0)->text().startsWith(star));
    QCOMPARE(table->topLevelItem(0)->text(0), QStringLiteral("\u2014"));
    QCOMPARE(list->count(), 3);
    mark->trigger();
    window.findChild<QCheckBox *>("imageListFavoritesOnly")->setChecked(true);
    QCOMPARE(list->count(), 1);
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    mark->trigger();
    QCOMPARE(list->count(), 0);
    window.findChild<QCheckBox *>("imageListFavoritesOnly")->setChecked(false);
    window.findChild<QDockWidget *>("thumbnailDock")->close();
    mark->trigger();
    open->trigger();
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    QVERIFY(!window.isWindowModified());
  }

  void marksPagesWithFavoritesInAnotherComparedCell() {
    QTemporaryDir left, right, metadata;
    const auto a = createImages(left.path(), 2);
    const auto b = createImages(right.path(), 2);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    QVERIFY(store.setFavorite(b.at(1), true));
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({a.first(), b.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *table = window.findChild<QTreeWidget *>("imageDetailsList");
    QCOMPARE(table->topLevelItem(1)->text(0), QStringLiteral("\u2605"));
    QCOMPARE(table->topLevelItem(0)->text(0), QStringLiteral("\u2014"));
  }

  void preservesMultipleSelectionsAcrossViewsAndPageChanges() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 4);
    QCOMPARE(paths.size(), 4);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *list = panel->findChild<QListWidget *>();
    list->item(0)->setSelected(true);
    list->item(2)->setSelected(true);
    QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(0), paths.at(2)}));
    panel->setDetailsVisible(true);
    auto *table = panel->findChild<QTreeWidget *>();
    QCOMPARE(table->selectedItems().size(), 2);
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    grid->nextPage();
    QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(0), paths.at(2)}));
    panel->setDetailsVisible(false);
    QCOMPARE(list->selectedItems().size(), 2);
    QVERIFY(panel->findChild<QPushButton *>("compareSelectedImages")->isEnabled());
  }

  void comparesSelectedImagesAndCanUndoTheLayoutChange() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 4);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    const auto original = grid->sessionSnapshot();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *list = panel->findChild<QListWidget *>();
    list->item(1)->setSelected(true);
    list->item(3)->setSelected(true);
    list->item(0)->setSelected(false);
    QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(1), paths.at(3)}));
    panel->findChild<QPushButton *>("compareSelectedImages")->click();
    QCOMPARE(grid->sessionSnapshot().cells.size(), 2);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(1));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(3));
    QVERIFY(window.isWindowModified());
    QAction *undo = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->shortcut() == QKeySequence::Undo)
        undo = action;
    }
    QVERIFY(undo && undo->isEnabled());
    undo->trigger();
    QCOMPARE(grid->sessionSnapshot().cells.size(), original.cells.size());
    QCOMPARE(grid->sessionSnapshot().images.first().resolvedPaths,
             original.images.first().resolvedPaths);
    QVERIFY(!window.isWindowModified());
  }

  void exportsSelectedOriginalFilesAsCopyOnlyUrls() {
    QTemporaryDir source;
    auto paths = createImages(source.path(), 3);
    const auto unicode = source.filePath(QString::fromUtf8("画像 #1.png"));
    QVERIFY(QFile::copy(paths.first(), unicode));
    paths[0] = unicode;
    heiran::ui::ImageListPanelWidget panel;
    panel.setPages(paths, 0);
    auto *list = panel.findChild<QListWidget *>();
    list->item(2)->setSelected(true);
    std::unique_ptr<QMimeData> mime(
        list->model()->mimeData(list->selectionModel()->selectedIndexes()));
    QVERIFY(mime && mime->hasUrls());
    QCOMPARE(mime->urls(),
             QList<QUrl>({QUrl::fromLocalFile(paths.at(0)), QUrl::fromLocalFile(paths.at(2))}));
    QCOMPARE(list->model()->supportedDragActions(), Qt::DropActions(Qt::CopyAction));
    panel.setDetailsVisible(true);
    auto *table = panel.findChild<QTreeWidget *>();
    std::unique_ptr<QMimeData> detailsMime(
        table->model()->mimeData(table->selectionModel()->selectedIndexes()));
    QCOMPARE(detailsMime->urls(), mime->urls());
    QCOMPARE(table->model()->supportedDragActions(), Qt::DropActions(Qt::CopyAction));
    QVERIFY(QFile::remove(paths.at(2)));
    std::unique_ptr<QMimeData> missing(
        table->model()->mimeData(table->selectionModel()->selectedIndexes()));
    QCOMPARE(missing->urls(), QList<QUrl>({QUrl::fromLocalFile(paths.at(0))}));
    QVERIFY(QFileInfo(paths.first()).isFile());
    panel.setPages({paths.first(), paths.first()}, 0);
    table->selectAll();
    std::unique_ptr<QMimeData> duplicates(
        table->model()->mimeData(table->selectionModel()->selectedIndexes()));
    QCOMPARE(duplicates->urls(), QList<QUrl>({QUrl::fromLocalFile(paths.first())}));
  }

  void selectsWithControlAndShiftClicksInBothViews() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 4);
    for (bool details : {false, true}) {
      heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
      window.openImageFiles({paths.first()});
      window.resize(1200, 800);
      window.show();
      window.findChild<QAction *>("imageListAction")->trigger();
      window.resizeDocks({window.findChild<QDockWidget *>("thumbnailDock")}, {520}, Qt::Horizontal);
      QVERIFY(QTest::qWaitForWindowActive(&window));
      auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
      panel->setDetailsVisible(details);
      auto *list = panel->findChild<QListWidget *>();
      auto *tree = panel->findChild<QTreeWidget *>();
      const auto click = [&](int row, Qt::KeyboardModifiers modifiers) {
        auto *viewport = details ? tree->viewport() : list->viewport();
        const auto rectangle = details ? tree->visualItemRect(tree->topLevelItem(row))
                                       : list->visualItemRect(list->item(row));
        QTest::mouseClick(viewport, Qt::LeftButton, modifiers, rectangle.center());
      };
      click(1, Qt::NoModifier);
      click(3, Qt::ControlModifier);
      QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(1), paths.at(3)}));
      click(0, Qt::NoModifier);
      click(2, Qt::ShiftModifier);
      QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(0), paths.at(1), paths.at(2)}));
    }
  }

  void permitsLargeDragSelectionsWithoutEnablingComparison() {
    QTemporaryDir source;
    const auto paths = createImages(source.path(), 20);
    heiran::ui::ImageListPanelWidget panel;
    panel.setPages(paths, 0);
    auto *list = panel.findChild<QListWidget *>();
    list->selectAll();
    QVERIFY(!panel.findChild<QPushButton *>("compareSelectedImages")->isEnabled());
    std::unique_ptr<QMimeData> mime(
        list->model()->mimeData(list->selectionModel()->selectedIndexes()));
    QCOMPARE(mime->urls().size(), 20);
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
    QTRY_COMPARE(table->topLevelItem(0)->text(2), QString("120"));
    QCOMPARE(table->topLevelItem(0)->text(3), QString("90"));
    QCOMPARE(table->topLevelItem(0)->text(1), QString("new.png"));
  }
};

QTEST_MAIN(ImageListPanelTests)
#include "ImageListPanelTests.moc"
