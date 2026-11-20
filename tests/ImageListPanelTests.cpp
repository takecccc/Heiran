#include "heiran/app/SessionPathResolver.hpp"
#include "heiran/domain/SessionPages.hpp"
#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/infra/ImageFileDetails.hpp"
#include "heiran/ui/FavoritesController.hpp"
#include "heiran/ui/ImageListDragViews.hpp"
#include "heiran/ui/ImageListPanelWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/ThumbnailItemDelegate.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>
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
    auto *checkbox = window.findChild<QToolButton *>("imageListFavoritesOnly");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    checkbox->setChecked(true);
    QVERIFY(filter->isChecked());
    QCOMPARE(grid->navigationPageCount(), 2);
    grid->previousPage();
    QCOMPARE(grid->currentPage(), 0);
    window.findChild<QToolButton *>("imageListDetailsButton")->click();
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
    QCOMPARE(table->columnCount(), 6);
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

  void switchesModesWithButtonsAndResizesThumbnails() {
    QTemporaryDir source;
    const auto paths = createImages(source.path(), 3);
    QImage large(640, 480, QImage::Format_RGB32);
    large.fill(Qt::red);
    QVERIFY(large.save(paths.first()));
    heiran::ui::ImageListPanelWidget panel;
    panel.resize(520, 600);
    panel.setPages(paths, 0);
    panel.show();
    auto *list = panel.findChild<QListWidget *>();
    auto *thumbnails = panel.findChild<QToolButton *>("imageListThumbnailsButton");
    auto *details = panel.findChild<QToolButton *>("imageListDetailsButton");
    auto *small = panel.findChild<QToolButton *>("thumbnailWidth64");
    auto *medium = panel.findChild<QToolButton *>("thumbnailWidth160");
    auto *largePreset = panel.findChild<QToolButton *>("thumbnailWidth224");
    auto *extraLarge = panel.findChild<QToolButton *>("thumbnailWidth320");
    QVERIFY(small && medium && largePreset && extraLarge);
    QVERIFY(medium->isChecked());
    QVERIFY(!thumbnails->icon().isNull() && !details->icon().isNull());
    QVERIFY(thumbnails->isChecked() && !details->isChecked());
    list->item(2)->setSelected(true);
    panel.setFavoriteStates({true, false, false});
    panel.setDisplayedFiles({paths.first(), paths.first()});
    QTRY_COMPARE(list->item(0)->icon().actualSize(QSize(512, 512)), QSize(160, 120));
    largePreset->click();
    QCOMPARE(panel.thumbnailWidth(), 224);
    QVERIFY(largePreset->isChecked() && !medium->isChecked());
    QCOMPARE(list->iconSize(), QSize(224, 168));
    QTRY_COMPARE(list->item(0)->icon().actualSize(QSize(512, 512)), QSize(224, 168));
    const auto wheel = [viewport = list->viewport()](int angle, Qt::KeyboardModifiers modifiers) {
      QWheelEvent event(QPointF(10, 10), QPointF(viewport->mapToGlobal(QPoint(10, 10))), {},
                        QPoint(0, angle), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
      QApplication::sendEvent(viewport, &event);
    };
    wheel(120, Qt::NoModifier);
    QCOMPARE(panel.thumbnailWidth(), 224);
    wheel(60, Qt::ControlModifier);
    QCOMPARE(panel.thumbnailWidth(), 224);
    wheel(60, Qt::ControlModifier);
    QCOMPARE(panel.thumbnailWidth(), 240);
    QVERIFY(!small->isChecked() && !medium->isChecked() && !largePreset->isChecked() &&
            !extraLarge->isChecked());
    QTRY_COMPARE(list->item(0)->icon().actualSize(QSize(512, 512)), QSize(240, 180));
    wheel(1200, Qt::ControlModifier);
    QCOMPARE(panel.thumbnailWidth(), 320);
    QVERIFY(extraLarge->isChecked());
    wheel(-720, Qt::ControlModifier);
    QVERIFY(largePreset->isChecked());
    medium->click();
    QCOMPARE(panel.thumbnailWidth(), 160);
    QCOMPARE(panel.selectedFilePaths(), QStringList({paths.first(), paths.at(2)}));
    QVERIFY(list->item(0)->data(heiran::ui::ThumbnailFavoriteRole).toBool());
    QCOMPARE(list->item(0)->data(heiran::ui::ThumbnailCellsRole).toStringList(),
             QStringList({"1", "2"}));
    details->click();
    QVERIFY(details->isChecked() && !thumbnails->isChecked());
    QVERIFY(!small->isVisible());
    panel.setThumbnailWidth(80);
    thumbnails->click();
    QVERIFY(small->isVisible() && !small->isChecked());
    QCOMPARE(panel.selectedFilePaths().size(), 2);
    QTRY_COMPARE(list->item(0)->icon().actualSize(QSize(512, 512)), QSize(80, 60));
    panel.setThumbnailWidth(9999);
    QCOMPARE(panel.thumbnailWidth(), 320);
    panel.setThumbnailWidth(-1);
    QCOMPARE(panel.thumbnailWidth(), 64);
  }

  void restoresThumbnailSize() {
    QSettings().setValue("display/thumbnailWidth", 259);
    {
      heiran::ui::MainWindow window;
      auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
      QCOMPARE(panel->thumbnailWidth(), 256);
      window.findChild<QToolButton *>("thumbnailWidth320")->click();
      panel->setThumbnailWidth(304);
    }
    heiran::ui::MainWindow reopened;
    QCOMPARE(reopened.findChild<heiran::ui::ImageListPanelWidget *>()->thumbnailWidth(), 304);
  }

  void restoresTheDetailsViewMode() {
    {
      heiran::ui::MainWindow window;
      window.findChild<QToolButton *>("imageListDetailsButton")->click();
    }
    heiran::ui::MainWindow reopened;
    QVERIFY(reopened.findChild<QToolButton *>("imageListDetailsButton")->isChecked());
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
    QVERIFY(!list->item(0)->data(heiran::ui::ThumbnailFavoriteRole).toBool());
    mark->trigger();
    QVERIFY(list->item(0)->data(heiran::ui::ThumbnailFavoriteRole).toBool());
    QVERIFY(!list->item(0)->text().contains(star));
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    QCOMPARE(list->count(), 3);
    window.findChild<QToolButton *>("imageListDetailsButton")->click();
    QTRY_COMPARE(table->topLevelItem(0)->text(2), QString("60"));
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    mark->trigger();
    QVERIFY(!list->item(0)->data(heiran::ui::ThumbnailFavoriteRole).toBool());
    QCOMPARE(table->topLevelItem(0)->text(0), QStringLiteral("\u2014"));
    QCOMPARE(list->count(), 3);
    mark->trigger();
    window.findChild<QToolButton *>("imageListFavoritesOnly")->setChecked(true);
    QCOMPARE(list->count(), 1);
    QCOMPARE(table->topLevelItem(0)->text(0), star);
    mark->trigger();
    QCOMPARE(list->count(), 0);
    window.findChild<QToolButton *>("imageListFavoritesOnly")->setChecked(false);
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

  void drawsThumbnailStatesInOppositeUpperCorners() {
    QTemporaryDir source;
    const auto paths = createImages(source.path(), 1);
    QImage portrait(40, 360, QImage::Format_RGB32);
    portrait.fill(Qt::red);
    QVERIFY(portrait.save(paths.first()));
    heiran::ui::ImageListPanelWidget panel;
    panel.resize(260, 480);
    panel.setPages(paths, 0);
    panel.show();
    auto *list = panel.findChild<QListWidget *>();
    QTRY_COMPARE(thumbnailColor(list), QColor(Qt::red));
    const auto caption = list->item(0)->text();
    auto capture = [&]() {
      return list->viewport()->grab().toImage().scaled(list->viewport()->size());
    };
    auto changes = [](const QImage &a, const QImage &b) {
      QRect bounds;
      for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
          if (a.pixel(x, y) != b.pixel(x, y))
            bounds = bounds.united(QRect(x, y, 1, 1));
      return bounds;
    };
    const auto normal = capture();
    panel.setFavoriteStates({true});
    const auto favorite = capture();
    const auto right = changes(normal, favorite);
    const auto item = list->visualItemRect(list->item(0));
    QVERIFY(!right.isEmpty());
    QVERIFY(right.center().x() > item.center().x());
    QVERIFY(right.right() >= item.right() - 5);
    QVERIFY(right.bottom() < item.center().y());
    panel.setDisplayedFiles({paths.first(), paths.first()});
    const auto cells = capture();
    const auto left = changes(favorite, cells);
    QVERIFY(!left.isEmpty());
    QVERIFY(left.center().x() < item.center().x());
    QVERIFY(left.left() <= item.left() + 5);
    QVERIFY(left.bottom() < item.center().y());
    QCOMPARE(list->item(0)->text(), caption);
    panel.setDisplayedFiles(QStringList(16, paths.first()));
    const auto many = changes(favorite, capture());
    QVERIFY(many.right() < right.left());
    QVERIFY(list->item(0)->toolTip().contains("16"));
    panel.setFavoriteStates({false});
    panel.setDisplayedFiles({});
    QVERIFY(changes(normal, capture()).isEmpty());
    QCOMPARE(thumbnailColor(list), QColor(Qt::red));
    auto *table = panel.findChild<QTreeWidget *>();
    QCOMPARE(table->headerItem()->text(5), QString("Cell"));
    QCOMPARE(table->headerItem()->toolTip(5), QString("Displayed in"));
  }

  void identifiesDisplayedFilesSeparatelyFromSelection() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 4);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *list = panel->findChild<QListWidget *>();
    auto *table = panel->findChild<QTreeWidget *>();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(table->topLevelItem(0)->text(5), QString("1"));
    QVERIFY(list->item(0)->toolTip().contains("Displayed in cell 1"));
    list->item(2)->setSelected(true);
    QCOMPARE(table->topLevelItem(2)->text(5), QString());
    grid->nextPage();
    QCOMPARE(panel->selectedFilePaths().size(), 2);
    QCOMPARE(table->topLevelItem(0)->text(5), QString());
    QCOMPARE(table->topLevelItem(1)->text(5), QString("1"));
    QVERIFY(!list->item(0)->toolTip().contains("Displayed in cell"));
    window.findChild<QAction *>("favoriteAction")->trigger();
    QVERIFY(list->item(1)->toolTip().contains("Displayed in cell 1"));
    panel->setDetailsVisible(true);
    QCOMPARE(table->topLevelItem(1)->text(5), QString("1"));
    window.findChild<QDockWidget *>("thumbnailDock")->hide();
    grid->nextPage();
    window.findChild<QAction *>("imageListAction")->trigger();
    QCOMPARE(table->topLevelItem(1)->text(5), QString());
    QCOMPARE(table->topLevelItem(2)->text(5), QString("1"));
    panel->setDisplayedFiles({paths.at(2), paths.at(2), paths.at(3)});
    QCOMPARE(table->topLevelItem(2)->text(5), QString("1, 2"));
    QCOMPARE(table->topLevelItem(3)->text(5), QString("3"));
    QVERIFY(list->item(2)->toolTip().contains("Displayed in cell 1, 2"));
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

  void endsRepeatedComparisonAndRestoresOriginalPage() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 6);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *end = panel->findChild<QPushButton *>("finishComparison");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    QVERIFY(end && end->isHidden());
    QVERIFY(!panel->findChild<QToolButton *>("imageListFavoritesOnly")->icon().isNull());
    QVERIFY(!panel->findChild<QPushButton *>("compareSelectedImages")->icon().isNull());
    grid->setCurrentPage(3);
    const auto original = grid->sessionSnapshot();
    window.compareSelectedImages({paths.at(0), paths.at(2)});
    QVERIFY(!end->isHidden());
    grid->nextPage();
    window.compareSelectedImages({paths.at(0), paths.at(2), paths.at(4)});
    filter->setChecked(true);
    end->click();
    QVERIFY(end->isHidden());
    QVERIFY(!filter->isChecked());
    QCOMPARE(grid->currentPage(), 3);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(3));
    QCOMPARE(grid->sessionSnapshot().cells.size(), original.cells.size());
    QCOMPARE(grid->sessionSnapshot().images.first().resolvedPaths,
             original.images.first().resolvedPaths);
    QVERIFY(!window.isWindowModified());
    const auto actions = window.findChildren<QAction *>();
    const auto undo = std::ranges::find_if(
        actions, [](const auto *action) { return action->shortcut() == QKeySequence::Undo; });
    QVERIFY(undo != actions.end());
    (*undo)->trigger();
    QVERIFY(!end->isHidden());
    end->click();
    QCOMPARE(grid->currentPage(), 3);

    grid->setCurrentPage(0);
    window.compareSelectedImages({paths.at(0), paths.at(2)});
    filter->setChecked(true);
    end->click();
    QVERIFY(!filter->isChecked());
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));

    window.findChild<heiran::ui::FavoritesController *>()->setFavorites({paths.at(0)}, true);
    window.compareSelectedImages({paths.at(0), paths.at(2)});
    filter->setChecked(true);
    end->click();
    QVERIFY(filter->isChecked());
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));

    window.compareSelectedImages({paths.at(0), paths.at(2)});
    window.openImageFiles({paths.at(1)});
    QVERIFY(end->isHidden());
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(1));
  }

  void preservesSourceListWhenComparingAndPaging() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 6);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *list = panel->findChild<QListWidget *>();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    list->clearSelection();
    list->item(1)->setSelected(true);
    list->item(4)->setSelected(true);
    panel->findChild<QPushButton *>("compareSelectedImages")->click();
    QCOMPARE(list->count(), paths.size());
    for (int row = 0; row < paths.size(); ++row)
      QCOMPARE(list->item(row)->data(Qt::UserRole + 2).toString(), paths.at(row));
    QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(1), paths.at(4)}));
    grid->nextPage();
    QCOMPARE(list->count(), paths.size());
    grid->previousPage();
    QCOMPARE(list->count(), paths.size());
    list->clearSelection();
    list->item(2)->setSelected(true);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    list->clearSelection();
    list->item(1)->setSelected(true);
    list->item(4)->setSelected(true);
    panel->findChild<QPushButton *>("compareSelectedImages")->click();
    QCOMPARE(list->count(), paths.size());
    window.findChild<QDockWidget *>("thumbnailDock")->hide();
    window.findChild<QAction *>("imageListAction")->trigger();
    QCOMPARE(list->count(), paths.size());
    panel->setDetailsVisible(true);
    auto *table = panel->findChild<QTreeWidget *>();
    QCOMPARE(table->topLevelItemCount(), paths.size());
    QCOMPARE(table->topLevelItem(1)->text(5), QString("1"));
    QCOMPARE(table->topLevelItem(4)->text(5), QString("2"));
    QAction *undo = nullptr, *redo = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->shortcut() == QKeySequence::Undo)
        undo = action;
      if (action->shortcut() == QKeySequence::Redo)
        redo = action;
    }
    QVERIFY(undo && redo);
    undo->trigger();
    undo->trigger();
    QCOMPARE(list->count(), paths.size());
    redo->trigger();
    QCOMPARE(list->count(), paths.size());
    redo->trigger();
    QCOMPARE(list->count(), paths.size());
    for (int row = 0; row < paths.size(); ++row)
      QCOMPARE(list->item(row)->data(Qt::UserRole + 2).toString(), paths.at(row));
    QTemporaryDir replacement;
    const auto next = createImages(replacement.path(), 2);
    window.openImageFiles({next.first()});
    QCOMPARE(list->count(), next.size());
    QCOMPARE(list->item(0)->data(Qt::UserRole + 2).toString(), next.first());
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), next.at(1));
  }

  void loopsFilteredPagesAndSuspendsLoopingDuringComparison() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 6);
    QCOMPARE(paths.size(), 6);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    QVERIFY(store.setFavorites({paths.at(0), paths.at(2), paths.at(4)}, true));

    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({paths.first()});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *loop = window.findChild<QAction *>("loopPagesAction");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    QVERIFY(grid && loop && filter);
    QVERIFY(!loop->isChecked());
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(grid->currentPage(), 0);

    loop->trigger();
    QVERIFY(loop->isChecked());
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(grid->currentPage(), 5);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(grid->currentPage(), 0);

    filter->setChecked(true);
    grid->previousPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));
    grid->previousPage();
    window.compareSelectedImages({paths.at(0), paths.at(2)});
    QVERIFY(!loop->isEnabled());
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    window.findChild<QPushButton *>("finishComparison")->click();
    QVERIFY(loop->isEnabled() && loop->isChecked());
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));

    heiran::ui::MainWindow reopened(nullptr, database);
    QVERIFY(reopened.findChild<QAction *>("loopPagesAction")->isChecked());
  }

  void stepsEachComparedImageThroughTheFilteredSource() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 7);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    QVERIFY(store.setFavorites({paths.at(0), paths.at(2), paths.at(4), paths.at(6)}, true));
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    window.compareSelectedImages({paths.at(0), paths.at(2)});
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *list = window.findChild<QListWidget *>("thumbnailList");
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    filter->setChecked(true);
    QCOMPARE(list->count(), 4);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(2));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(6));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(6));
    grid->setCurrentPage(0);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(2));
    grid->stepPages(9);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(6));
    grid->stepPages(-9);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(2));
    auto *selector = window.findChild<QComboBox *>("pageSelector");
    QCOMPARE(selector->count(), 3);
    QVERIFY(selector->itemText(2).contains(QFileInfo(paths.at(4)).fileName()));
    window.compareSelectedImages({paths.at(0), paths.at(6)});
    QCOMPARE(grid->navigationPageCount(), 1);
    QCOMPARE(window.findChild<heiran::ui::FavoritesController *>()->favoriteFiles().size(), 4);
    filter->setChecked(false);
    window.compareSelectedImages({paths.at(0), paths.at(2), paths.at(4)});
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(1));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(3));
    QCOMPARE(grid->currentFilePathForCell(2), paths.at(5));
    QVERIFY(grid->currentFilePathForCell(3).isEmpty());
    auto saved = grid->sessionSnapshot();
    heiran::app::SessionPathResolver::resolveImagePaths(&saved);
    for (int cell = 0; cell < 3; ++cell)
      QCOMPARE(heiran::domain::cellPageAt(saved, cell, 0).path, grid->currentFilePathForCell(cell));
  }

  void retainsExcludedComparedImagesUntilNavigation() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 7);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    const QStringList marked{paths.at(0), paths.at(2), paths.at(4), paths.at(6)};
    QVERIFY(store.setFavorites(marked, true));
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    window.compareSelectedImages({paths.at(1), paths.at(3)});
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    auto *favorites = window.findChild<heiran::ui::FavoritesController *>();
    auto *excluded = window.findChild<QLabel *>("excludedDisplayedFiles");
    auto *list = window.findChild<QListWidget *>("thumbnailList");
    grid->zoomBy(1.5);
    const auto view = grid->globalViewState();
    filter->setChecked(true);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(1));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(3));
    QCOMPARE(grid->globalViewState().baseScale, view.baseScale);
    QCOMPARE(grid->globalViewState().baseOffsetX, view.baseOffsetX);
    QCOMPARE(grid->globalViewState().baseOffsetY, view.baseOffsetY);
    QVERIFY(excluded->isVisible());
    QVERIFY(excluded->text().contains(QFileInfo(paths.at(1)).fileName()));
    QVERIFY(excluded->text().contains(QFileInfo(paths.at(3)).fileName()));
    QCOMPARE(list->count(), 4);
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    QVERIFY(!excluded->isVisible());
    grid->previousPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(0));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(2));
    grid->nextPage();
    favorites->setFavorites({paths.at(2)}, false);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    QVERIFY(excluded->isVisible());
    favorites->setFavorites({paths.at(2)}, true);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    QVERIFY(!excluded->isVisible());
    favorites->setFavorites(marked, false);
    QCOMPARE(list->count(), 0);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    QVERIFY(excluded->isVisible());
    grid->nextPage();
    grid->previousPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    QVERIFY(window.findChild<QAction *>("favoriteAction")->isEnabled());
    favorites->setFavorites({paths.at(3), paths.at(5)}, true);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(2));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(4));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(3));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(5));
    QVERIFY(!excluded->isVisible());
    filter->setChecked(false);
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(3));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(5));
    grid->nextPage();
    QCOMPARE(grid->currentFilePathForCell(0), paths.at(4));
    QCOMPARE(grid->currentFilePathForCell(1), paths.at(6));
  }

  void preservesFilteredSourceWhenComparing() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 6);
    const auto database = metadata.filePath("favorites.json");
    heiran::infra::FavoriteStore store(database);
    QVERIFY(store.setFavorites({paths.at(0), paths.at(2), paths.at(5)}, true));
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
    auto *list = panel->findChild<QListWidget *>();
    auto *filter = window.findChild<QAction *>("favoritesOnlyAction");
    filter->setChecked(true);
    QCOMPARE(list->count(), 3);
    list->clearSelection();
    list->item(0)->setSelected(true);
    list->item(2)->setSelected(true);
    panel->findChild<QPushButton *>("compareSelectedImages")->click();
    QVERIFY(filter->isChecked());
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->item(0)->data(Qt::UserRole + 2).toString(), paths.at(0));
    QCOMPARE(list->item(1)->data(Qt::UserRole + 2).toString(), paths.at(2));
    QCOMPARE(list->item(2)->data(Qt::UserRole + 2).toString(), paths.at(5));
    window.findChild<heiran::ui::FavoritesController *>()->setFavorites({paths.at(5)}, false);
    QCOMPARE(list->count(), 2);
    filter->setChecked(false);
    QCOMPARE(list->count(), paths.size());
    filter->setChecked(true);
    QCOMPARE(list->count(), 2);
  }

  void preservesSourceWhileThePanelIsHidden() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 5);
    heiran::ui::MainWindow window(nullptr, metadata.filePath("favorites.json"));
    window.openImageFiles({paths.first()});
    window.show();
    window.compareSelectedImages({paths.at(1), paths.at(4)});
    window.findChild<QAction *>("imageListAction")->trigger();
    auto *list = window.findChild<QListWidget *>("thumbnailList");
    QCOMPARE(list->count(), paths.size());
    for (int row = 0; row < paths.size(); ++row)
      QCOMPARE(list->item(row)->data(Qt::UserRole + 2).toString(), paths.at(row));
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

  void addsAndRemovesSelectedFavoritesInBothViews() {
    for (bool details : {false, true}) {
      QTemporaryDir source, metadata;
      const auto paths = createImages(source.path(), 4);
      const auto database = metadata.filePath("favorites.json");
      heiran::infra::FavoriteStore store(database);
      QVERIFY(store.setFavorites({paths.at(0), paths.at(3)}, true));
      heiran::ui::MainWindow window(nullptr, database);
      window.openImageFiles({paths.first()});
      window.show();
      window.findChild<QAction *>("imageListAction")->trigger();
      auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
      panel->setDetailsVisible(details);
      auto *list = panel->findChild<QListWidget *>();
      auto *table = panel->findChild<QTreeWidget *>();
      auto select = [&](QVector<int> rows) {
        if (details) {
          table->clearSelection();
          for (int row : rows)
            table->topLevelItem(row)->setSelected(true);
        } else {
          list->clearSelection();
          for (int row : rows)
            list->item(row)->setSelected(true);
        }
      };
      auto *add = panel->findChild<QAction *>("addSelectedFavoritesAction");
      auto *remove = panel->findChild<QAction *>("removeSelectedFavoritesAction");
      QVERIFY(add && remove);
      select({});
      QVERIFY(!add->isEnabled());
      QVERIFY(!remove->isEnabled());
      select({0, 2});
      QSignalSpy updates(window.findChild<heiran::ui::FavoritesController *>(),
                         &heiran::ui::FavoritesController::favoritesChanged);
      add->trigger();
      QCOMPARE(updates.size(), 1);
      QVERIFY(store.load());
      QVERIFY(store.contains(paths.at(0)) && store.contains(paths.at(2)) &&
              store.contains(paths.at(3)));
      QVERIFY(!store.contains(paths.at(1)));
      QCOMPARE(panel->selectedFilePaths().size(), 2);
      window.findChild<QAction *>("favoritesOnlyAction")->setChecked(true);
      QCOMPARE(list->count(), 3);
      select({0, 1});
      updates.clear();
      remove->trigger();
      QCOMPARE(updates.size(), 1);
      QCOMPARE(list->count(), 1);
      QVERIFY(store.load());
      QVERIFY(!store.contains(paths.at(0)) && !store.contains(paths.at(2)));
      QVERIFY(store.contains(paths.at(3)));
      QVERIFY(!window.isWindowModified());
    }
  }

  void editsFavoritesFromTheContextMenuInBothViews() {
    for (bool details : {false, true}) {
      QTemporaryDir source, metadata;
      const auto paths = createImages(source.path(), 4);
      const auto database = metadata.filePath("favorites.json");
      heiran::ui::MainWindow window(nullptr, database);
      window.openImageFiles({paths.first()});
      window.show();
      window.findChild<QAction *>("imageListAction")->trigger();
      auto *panel = window.findChild<heiran::ui::ImageListPanelWidget *>();
      panel->setDetailsVisible(details);
      auto *list = panel->findChild<QListWidget *>();
      auto *tree = panel->findChild<QTreeWidget *>();
      auto *view =
          details ? static_cast<QAbstractItemView *>(tree) : static_cast<QAbstractItemView *>(list);
      QCoreApplication::processEvents();
      auto select = [&](int row) {
        if (details)
          tree->topLevelItem(row)->setSelected(true);
        else
          list->item(row)->setSelected(true);
      };
      auto position = [&](int row) {
        return details ? tree->visualItemRect(tree->topLevelItem(row)).center()
                       : list->visualItemRect(list->item(row)).center();
      };
      view->clearSelection();
      auto *add = panel->findChild<QAction *>("addSelectedFavoritesAction");
      auto *remove = panel->findChild<QAction *>("removeSelectedFavoritesAction");
      QVERIFY(!add->isEnabled() && !remove->isEnabled());
      select(0);
      select(2);
      QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, position(0));
      QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(0), paths.at(2)}));
      const auto blank = view->viewport()->rect().bottomRight() - QPoint(3, 3);
      QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, blank);
      QCOMPARE(panel->selectedFilePaths().size(), 2);
      auto invokeMenu = [&](QAction *action) {
        bool opened = false;
        QTimer::singleShot(0, &window, [&]() {
          auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
          if (!menu)
            return;
          opened = menu->actions().contains(add) && menu->actions().contains(remove);
          menu->close();
          action->trigger();
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, position(0),
                                view->viewport()->mapToGlobal(position(0)));
        QCoreApplication::sendEvent(view->viewport(), &event);
        return opened;
      };
      QVERIFY(invokeMenu(add));
      heiran::infra::FavoriteStore saved(database);
      QVERIFY(saved.load());
      QVERIFY(saved.contains(paths.at(0)) && saved.contains(paths.at(2)));
      QVERIFY(!saved.contains(paths.at(1)) && !saved.contains(paths.at(3)));
      window.findChild<QAction *>("favoritesOnlyAction")->setChecked(true);
      QCOMPARE(list->count(), 2);
      select(0);
      select(1);
      QVERIFY(invokeMenu(remove));
      QCOMPARE(list->count(), 0);
      QVERIFY(saved.load());
      QVERIFY(!saved.contains(paths.at(0)) && !saved.contains(paths.at(2)));
      window.findChild<QAction *>("favoritesOnlyAction")->setChecked(false);
      select(0);
      select(2);
      QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, position(1));
      QCOMPARE(panel->selectedFilePaths(), QStringList({paths.at(1)}));
    }
  }

  void registersMoreThanSixteenSelectedFavorites() {
    QTemporaryDir source, metadata;
    const auto paths = createImages(source.path(), 20);
    const auto database = metadata.filePath("favorites.json");
    heiran::ui::MainWindow window(nullptr, database);
    window.openImageFiles({paths.first()});
    window.show();
    window.findChild<QAction *>("imageListAction")->trigger();
    window.findChild<QListWidget *>("thumbnailList")->selectAll();
    auto *add = window.findChild<QAction *>("addSelectedFavoritesAction");
    QVERIFY(add && add->isEnabled());
    add->trigger();
    heiran::infra::FavoriteStore saved(database);
    QVERIFY(saved.load());
    for (const auto &path : paths)
      QVERIFY(saved.contains(path));
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
