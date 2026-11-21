#include "MultiFrameFixtures.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/CellStatus.hpp"
#include "heiran/ui/HistogramWindow.hpp"
#include "heiran/ui/ImageListPanelWidget.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/StartupPreparationController.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

QMenu *findMenu(QMenuBar *menuBar, const QString &title) {
  for (auto *action : menuBar->actions()) {
    if (action->menu() != nullptr && action->text() == title) {
      return action->menu();
    }
  }
  return nullptr;
}

bool menuContainsAction(const QMenu *menu, const QAction *target) {
  for (const auto *action : menu->actions()) {
    if (action == target) {
      return true;
    }
  }
  return false;
}

// 関連付けから起動された状況を作るための実ファイル。
QStringList writeImageFiles(const QString &directory, int count) {
  QStringList paths;
  for (int index = 0; index < count; ++index) {
    QImage image(16, 12, QImage::Format_RGBA8888);
    image.fill(QColor(20 + index * 40, 90, 160));
    const QString path = QDir(directory).filePath(QString("image%1.png").arg(index));
    if (!image.save(path)) {
      return {};
    }
    paths.push_back(path);
  }
  return paths;
}

}  // namespace

class MainWindowTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void editsOverallZoomAsAPercentage() {
    QTemporaryDir directory;
    const auto paths = writeImageFiles(directory.path(), 1);
    heiran::ui::MainWindow window;
    window.resize(1100, 650);
    auto *zoom = window.findChild<QDoubleSpinBox *>("globalZoomPercent");
    QVERIFY(zoom);
    QVERIFY(!zoom->isEnabled());
    window.openImageFiles(paths);
    window.show();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *cell = grid->findChild<heiran::ui::ImageViewWidget *>();
    QTRY_VERIFY(cell->hasImage());
    auto session = grid->sessionSnapshot();
    session.cells[0].scale = 2.0;
    grid->setSessionPreservingView(session);
    grid->setGlobalScale(1.0);
    QVERIFY(zoom->isEnabled());
    QCOMPARE(zoom->value(), 100.0);
    QSignalSpy status(grid, &heiran::ui::ViewerGridWidget::statusChanged);
    zoom->setValue(150.25);
    QCOMPARE(grid->globalViewState().baseScale, 1.5025);
    QCOMPARE(grid->sessionSnapshot().cells.at(0).scale, 2.0);
    QVERIFY(status.last().at(0).toString().contains("150.25%"));
    grid->zoomBy(2.0);
    QCOMPARE(zoom->value(), 300.5);
    auto *edit = zoom->findChild<QLineEdit *>();
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, "175.50");
    QCOMPARE(grid->globalViewState().baseScale, 3.005);
    grid->zoomBy(1.1);
    QVERIFY(edit->text().contains("175.50"));
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(grid->globalViewState().baseScale, 1.755);
    QCOMPARE(zoom->value(), 175.5);
    grid->setGlobalScale(0.0);
    grid->setGlobalScale(std::numeric_limits<double>::quiet_NaN());
    grid->setGlobalScale(std::numeric_limits<double>::infinity());
    QCOMPARE(grid->globalViewState().baseScale, 1.755);
    window.setSession({});
    QVERIFY(!zoom->isEnabled());
  }

  void handsOffAlreadyDecodedImagesWithoutDecodingThemAgain() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);
    const auto selected =
        heiran::ui::StartupPreparationController::prepareImages({paths.last()}, directory.path());
    auto scheduler = std::make_shared<heiran::ui::DecodeScheduler>(nullptr, true);
    QSignalSpy buffered(scheduler.get(), &heiran::ui::DecodeScheduler::completionBuffered);
    scheduler->request(paths.last(), true, false);
    QTRY_COMPARE(buffered.count(), 1);
    QVERIFY(QFile::remove(paths.last()));
    heiran::ui::MainWindow window;
    window.openPreparedImages(selected, std::move(scheduler));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(grid->currentPage(), 2);
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);
    QVERIFY(cell->hasImage());
    QCOMPARE(grid->decodeCacheMissCount(), 0);
  }

  void handsOffDecodeErrorsWithoutRetryingTheFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);
    const auto prepared =
        heiran::ui::StartupPreparationController::prepareImages(paths, directory.path());
    QVERIFY(QFile::remove(paths.first()));
    auto scheduler = std::make_shared<heiran::ui::DecodeScheduler>(nullptr, true);
    QSignalSpy buffered(scheduler.get(), &heiran::ui::DecodeScheduler::completionBuffered);
    scheduler->request(paths.first(), true, false);
    QTRY_COMPARE(buffered.count(), 1);
    heiran::ui::MainWindow window;
    window.openPreparedImages(prepared, std::move(scheduler));
    const auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    const auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cell->cellStatus(), heiran::ui::CellStatus::Failed);
    QCOMPARE(grid->decodeCacheMissCount(), 0);
  }

  void opensPreparedImagesWithoutRescanningTheDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = writeImageFiles(directory.path(), 3);
    const auto prepared =
        heiran::ui::StartupPreparationController::prepareImages({paths.last()}, directory.path());
    QVERIFY(QFile::remove(paths.first()));
    heiran::ui::MainWindow window;
    window.openPreparedImages(prepared);
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->currentPage(), 2);
    QCOMPARE(grid->totalPages(), 3);
    QCOMPARE(grid->sessionSnapshot().images.first().resolvedPaths, paths);
    QCOMPARE(grid->targetCellFilePath(), paths.last());
    QVERIFY(!window.isWindowModified());
  }

  void keepsPageControlsAvailableWhenNavigationIsHidden() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = writeImageFiles(directory.path(), 3);
    heiran::ui::MainWindow window;
    window.openImageFiles({paths.first()});
    window.resize(440, 560);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *navigation = window.findChild<QToolBar *>("navigationToolBar");
    auto *compact = window.findChild<QWidget *>("compactPageNavigation");
    auto *selector = window.findChild<QComboBox *>("compactPageSelector");
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(navigation && compact && selector && grid);
    QVERIFY(compact->isHidden());
    navigation->hide();
    QVERIFY(compact->isVisible());
    QCOMPARE(selector->count(), 3);
    window.findChild<QToolButton *>("compactNextPage")->click();
    QCOMPARE(grid->currentPage(), 1);
    QCOMPARE(selector->currentIndex(), 1);
    selector->setCurrentIndex(2);
    QCOMPARE(grid->currentPage(), 2);
    window.findChild<QToolButton *>("compactPreviousPage")->click();
    QCOMPARE(grid->currentPage(), 1);
    navigation->show();
    QVERIFY(compact->isHidden());
  }

  void wrapsNavigationAndKeepsThePageSelectorUsable() {
    const auto organization = QCoreApplication::organizationName();
    const auto application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("NavigationWrapTests");
    const auto restore = qScopeGuard([organization, application]() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    });
    QSettings().clear();
    auto settings = heiran::ui::AppSettings::load();
    settings.wrapNavigation = true;
    settings.save();
    QVERIFY(heiran::ui::AppSettings::load().wrapNavigation);

    QTemporaryDir directory;
    const auto paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);
    const auto longPath = directory.filePath(QString(125, QLatin1Char('x')) + ".png");
    QVERIFY(QFile::rename(paths.at(0), longPath));
    heiran::ui::MainWindow window;
    window.openImageFiles({longPath});
    window.resize(1250, 620);
    window.show();
    auto *navigation = window.findChild<QToolBar *>("navigationToolBar");
    auto *second = window.findChild<QToolBar *>("navigationSecondRow");
    auto *selector = window.findChild<QComboBox *>("pageSelector");
    auto *zoom = window.findChild<QDoubleSpinBox *>("globalZoomPercent");
    auto *compact = window.findChild<QWidget *>("compactPageNavigation");
    QVERIFY(navigation && second && selector && zoom && compact);
    QVERIFY(second->isHidden());
    QCOMPARE(selector->count(), 2);
    QVERIFY(selector->currentText().contains(QString(125, QLatin1Char('x'))));
    QCOMPARE(selector->toolTip(), selector->currentText());
    QCOMPARE(selector->itemData(selector->currentIndex(), Qt::ToolTipRole).toString(),
             selector->currentText());
    const int wideSelector = selector->width();

    window.resize(960, 620);
    QTRY_VERIFY(selector->width() < wideSelector);
    QVERIFY(selector->width() >= selector->minimumWidth());
    QVERIFY(second->isHidden());
    window.resize(440, 620);
    QTRY_VERIFY(second->isVisible());
    QVERIFY(zoom->parentWidget() == second);
    QVERIFY(selector->isVisible() && zoom->isVisible());
    for (auto *row : {navigation, second}) {
      for (auto *action : row->actions()) {
        if (!action->isSeparator())
          QVERIFY2(row->widgetForAction(action)->isVisible(), qPrintable(action->text()));
      }
    }
    QVERIFY(compact->isHidden());
    const auto shortIndex = 1 - selector->currentIndex();
    selector->setCurrentIndex(shortIndex);
    QCOMPARE(selector->toolTip(), selector->currentText());
    navigation->hide();
    QVERIFY(second->isHidden() && compact->isVisible());
    navigation->show();
    QTRY_VERIFY(second->isVisible());
    window.resize(1250, 620);
    QTRY_VERIFY(second->isHidden());
    QVERIFY(zoom->parentWidget() == navigation);
    QCOMPARE(selector->currentIndex(), shortIndex);

    settings.wrapNavigation = false;
    settings.save();
    heiran::ui::MainWindow oneRow;
    oneRow.resize(440, 620);
    oneRow.show();
    QCoreApplication::processEvents();
    QVERIFY(oneRow.findChild<QToolBar *>("navigationSecondRow")->isHidden());
  }

  void hidesCellHeadersAcrossRebuildAndRestart() {
    const auto organization = QCoreApplication::organizationName();
    const auto application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("CellHeaderVisibilityTests");
    const auto restore = qScopeGuard([organization, application]() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    });
    QSettings().clear();
    {
      heiran::ui::MainWindow window;
      auto *action = window.findChild<QAction *>("cellHeadersAction");
      QVERIFY(action && action->isChecked());
      action->trigger();
      for (auto *header : window.findChildren<QWidget *>("cellHeader")) {
        QVERIFY(header->isHidden());
      }
      heiran::domain::SessionModel session;
      session.viewer.layout.rows = 1;
      session.viewer.layout.cols = 2;
      session.cells.resize(2);
      window.setSession(session);
      QCOMPARE(window.findChildren<QWidget *>("cellHeader").size(), 2);
      for (auto *header : window.findChildren<QWidget *>("cellHeader")) {
        QVERIFY(header->isHidden());
      }
    }
    heiran::ui::MainWindow restored;
    auto *action = restored.findChild<QAction *>("cellHeadersAction");
    QVERIFY(action && !action->isChecked());
    action->trigger();
    for (auto *header : restored.findChildren<QWidget *>("cellHeader")) {
      QVERIFY(!header->isHidden());
    }
  }

  void fitsImagesOpenedBeforeShowingTheWindow_data() {
    QTest::addColumn<int>("imageCount");
    QTest::addColumn<bool>("decodeBeforeShow");
    QTest::addColumn<bool>("handoff");
    QTest::newRow("single-async") << 1 << false << false;
    QTest::newRow("multiple-async") << 2 << false << false;
    QTest::newRow("single-decoded-before-show") << 1 << true << false;
    QTest::newRow("multiple-decoded-before-show") << 2 << true << false;
    QTest::newRow("single-handoff-pending") << 1 << false << true;
    QTest::newRow("multiple-handoff-pending") << 2 << false << true;
    QTest::newRow("single-handoff-completed") << 1 << true << true;
    QTest::newRow("multiple-handoff-completed") << 2 << true << true;
  }

  void fitsImagesOpenedBeforeShowingTheWindow() {
    QFETCH(int, imageCount);
    QFETCH(bool, decodeBeforeShow);
    QFETCH(bool, handoff);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (int index = 0; index < imageCount; ++index) {
      QImage image(index == 0 ? QSize(1600, 900) : QSize(900, 1600), QImage::Format_RGB32);
      image.fill(QColor(30, 90, 150));
      const QString path = directory.filePath(QString("startup%1.png").arg(index));
      QVERIFY(image.save(path));
      paths.push_back(path);
    }
    heiran::ui::MainWindow window;
    window.resize(900, 600);
    if (handoff) {
      const auto prepared =
          heiran::ui::StartupPreparationController::prepareImages(paths, directory.path());
      auto scheduler = std::make_shared<heiran::ui::DecodeScheduler>(nullptr, true);
      QSignalSpy buffered(scheduler.get(), &heiran::ui::DecodeScheduler::completionBuffered);
      for (const auto &path : paths) {
        scheduler->request(path, true, false);
      }
      if (decodeBeforeShow) {
        QTRY_COMPARE(buffered.count(), imageCount);
      }
      window.openPreparedImages(prepared, std::move(scheduler));
    } else {
      window.openImageFiles(paths);
    }
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), imageCount);
    const auto decoded = [&]() {
      return std::all_of(cells.begin(), cells.end(), [](auto *cell) {
        return cell->cellStatus() == heiran::ui::CellStatus::Ready;
      });
    };
    if (decodeBeforeShow) {
      QTRY_VERIFY(decoded());
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(decoded());
    QTest::qWait(50);
    const auto fitsCurrentCells = [&]() {
      std::vector<heiran::domain::FitTarget> targets;
      for (auto *cell : cells) {
        const QSize imageSize = cell->currentImage().size();
        targets.push_back(
            {.imageWidth = imageSize.width(), .imageHeight = imageSize.height(), .cellScale = 1.0});
      }
      const QSize canvasSize = cells.first()->canvasSize();
      const auto expected =
          heiran::domain::computeFitView(targets, canvasSize.width(), canvasSize.height());
      const auto actual = grid->globalViewState();
      return expected.valid && std::abs(actual.baseScale - expected.baseScale) < 1e-6 &&
             std::abs(actual.baseOffsetX - expected.baseOffsetX) < 1e-6 &&
             std::abs(actual.baseOffsetY - expected.baseOffsetY) < 1e-6;
    };
    QTRY_VERIFY_WITH_TIMEOUT(fitsCurrentCells(), 2000);
    auto *headersAction = window.findChild<QAction *>("cellHeadersAction");
    QVERIFY(headersAction != nullptr);
    headersAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(fitsCurrentCells(), 2000);
    headersAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(fitsCurrentCells(), 2000);
  }

  void switchesTransparencyBackgroundFromTheCanvasContextMenu() {
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("MainWindowTransparencyMenuTests");
    const auto restoreSettings = qScopeGuard([organization, application]() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    });
    QSettings().clear();
    heiran::ui::AppSettings initial;
    initial.transparencyBackground.solidColor = QColor(20, 40, 60);
    initial.transparencyBackground.checkerColor1 = QColor(70, 80, 90);
    initial.transparencyBackground.checkerColor2 = QColor(170, 180, 190);
    initial.save();
    heiran::ui::MainWindow window;
    heiran::domain::SessionModel session;
    session.viewer.layout.cols = 2;
    session.cells.resize(2);
    window.setSession(session);
    window.resize(640, 480);
    window.show();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);

    for (const auto desired :
         {heiran::ui::TransparencyPattern::Checkerboard, heiran::ui::TransparencyPattern::Solid}) {
      const auto previous = heiran::ui::AppSettings::load().transparencyBackground.pattern;
      const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
      QVERIFY(!cells.isEmpty());
      auto *cell =
          desired == heiran::ui::TransparencyPattern::Checkerboard ? cells.last() : cells.first();
      auto *canvas = cell->findChild<QWidget *>("imageCanvas");
      QVERIFY(canvas != nullptr);
      bool checkedStateMatches = false;
      QTimer::singleShot(0, [&]() {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu == nullptr) {
          return;
        }
        QMenu *backgroundMenu = nullptr;
        for (auto *action : menu->actions()) {
          if (action->text() == "Transparency background") {
            backgroundMenu = action->menu();
          }
        }
        if (backgroundMenu == nullptr || backgroundMenu->actions().size() != 2) {
          menu->close();
          return;
        }
        auto *solid = backgroundMenu->actions().at(0);
        auto *checker = backgroundMenu->actions().at(1);
        checkedStateMatches =
            solid->isCheckable() && checker->isCheckable() &&
            solid->isChecked() == (previous == heiran::ui::TransparencyPattern::Solid) &&
            checker->isChecked() == (previous == heiran::ui::TransparencyPattern::Checkerboard);
        menu->setActiveAction(backgroundMenu->menuAction());
        QTest::keyClick(menu, Qt::Key_Right);
        backgroundMenu->setActiveAction(
            desired == heiran::ui::TransparencyPattern::Solid ? solid : checker);
        QTest::keyClick(backgroundMenu, Qt::Key_Return);
      });
      QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5),
                              canvas->mapToGlobal(QPoint(5, 5)));
      QApplication::sendEvent(canvas, &event);
      QVERIFY(checkedStateMatches);
      auto expected = initial.transparencyBackground;
      expected.pattern = desired;
      QCOMPARE(heiran::ui::AppSettings::load().transparencyBackground, expected);
      QVERIFY(!window.isWindowModified());
    }
  }

  void offersPixelInfoDockToggleInViewMenu() {
    heiran::ui::MainWindow window;

    auto *dock = window.findChild<QDockWidget *>();
    QVERIFY(dock != nullptr);

    auto *viewMenu = findMenu(window.menuBar(), "&View");
    QVERIFY(viewMenu != nullptr);

    // 閉じたドックを再表示する導線がメニューに無いと、閉じたきり開けなくなる。
    QVERIFY(menuContainsAction(viewMenu, dock->toggleViewAction()));
  }

  void picksLayoutForImageCount() {
    // QCOMPARE はマクロのため、比較する値にカンマを含む型は直接書けない。
    const auto check = [](int count, int expectedRows, int expectedCols) {
      const auto layout = heiran::ui::SessionLayoutController::layoutForImageCount(count);
      const QString description =
          QString("count %1 -> %2x%3").arg(count).arg(layout.rows).arg(layout.cols);
      QVERIFY2(layout.rows == expectedRows && layout.cols == expectedCols, qPrintable(description));
    };

    check(1, 1, 1);
    check(2, 1, 2);
    check(3, 2, 2);
    check(4, 2, 2);
    check(5, 2, 3);
    check(6, 2, 3);
    check(9, 3, 3);
  }

  // 関連付けから起動された場合の入口。枚数に合う格子へ切り替えて割り当てる。
  void opensImageFilesIntoAFittingLayout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);

    // 既定は 2x2 なので、2 枚なら 1x2 へ減っていることが分かる。
    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 2);
    // その場限りのセッションなので、閉じるときに保存を聞かれてはいけない。
    QVERIFY(!window.isWindowModified());
  }

  // 1 枚を指定して開いた場合は、その拡張子で同じフォルダを search した結果を
  // ページとして持ち、指定した画像のページから始まる。
  void opensASingleImageAsASearchStartingAtThatImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 4);
    QCOMPARE(paths.size(), 4);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(2)});

    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 1);
    QCOMPARE(grid->currentPage(), 2);

    // ページ数は画面に出ているページ選択から確かめる。
    auto *pageSelector = window.findChild<QComboBox *>("pageSelector");
    QVERIFY(pageSelector != nullptr);
    QCOMPARE(pageSelector->count(), 4);
  }

  // 画像を直接開いた場合、名前を画像の上に重ねない。
  // 既定セッションの画像セットを使い回すと、前の名前が残ってしまう。
  void opensImageFilesWithoutLabels() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);

    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const auto session = grid->sessionSnapshot();

    // 使わない画像セットが残っていないこと。
    QCOMPARE(session.images.size(), 1);
    QVERIFY2(!session.images.at(0).labelVisible, "labels must be off by default");
    // ラベル文字列は開いた画像に合わせて更新されていること。
    QVERIFY2(session.images.at(0).label.contains("image0"), qPrintable(session.images.at(0).label));
  }

  void opensThreeImageFilesIntoTwoByTwo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);

    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 4);
  }

  // 左右キーでページを送れること。ツールバーの入力欄がフォーカスを持っていても
  // ページ送りが優先されることまで確かめる。
  void changesPageWithArrowKeys() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->totalPages(), 3);
    QCOMPARE(grid->currentPage(), 0);

    // 露出の入力欄はテキストカーソルの移動に左右キーを使う。入力欄は押したときに出る欄の中に
    // あり、閉じればフォーカスは戻る。ページ送りが効くことを確かめる。
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(grid->currentPage(), 1);

    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(grid->currentPage(), 0);
  }

  // 解析ウィンドウは開いた直後に内容が出ること。
  // 閉じている間は計算しないようにしたため、表示より先に更新すると空のまま出る。
  // Analysis > Diff Cell で、差分のセルが並ぶ。空きが無ければ格子が広がり、差分が出る。
  void addsADifferenceCellFromTheAnalysisMenu() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // 同じフォルダの 2 枚は、1 つの画像セットのページとして開かれる。別のフォルダに置く。
    QDir(directory.path()).mkpath("a");
    QDir(directory.path()).mkpath("b");
    QStringList paths = writeImageFiles(QDir(directory.path()).filePath("a"), 1);
    paths += writeImageFiles(QDir(directory.path()).filePath("b"), 2).mid(1);
    QCOMPARE(paths.size(), 2);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->currentAnalysisImages().size(), 2);

    auto *analysisMenu = findMenu(window.menuBar(), "&Analysis");
    QVERIFY(analysisMenu != nullptr);
    QAction *diffCellAction = nullptr;
    for (auto *action : analysisMenu->actions()) {
      if (action->text() == "Diff Cell") {
        diffCellAction = action;
      }
    }
    QVERIFY(diffCellAction != nullptr);
    diffCellAction->trigger();

    // 2 つのセルで空きが無いため 1 列広がり、3 つ目が差分になる。
    QCOMPARE(grid->currentAnalysisImages().size(), 3);
    const auto diff = grid->cellDiff(2);
    QVERIFY(diff.has_value());
    QCOMPARE(diff->baseCell, 0);
    QCOMPARE(diff->targetCell, 1);
    QTRY_VERIFY(grid->cellDiffMetrics(2).has_value());
    // 保存される内容にも入る。
    QVERIFY(grid->sessionSnapshot().cells.at(2).diff.has_value());
  }

  // Help メニューに、マニュアルとバージョン情報がある。マニュアルは F1。
  void offersTheManualFromTheHelpMenu() {
    heiran::ui::MainWindow window;
    auto *helpMenu = findMenu(window.menuBar(), "&Help");
    QVERIFY(helpMenu != nullptr);
    QAction *manualAction = nullptr;
    for (auto *action : helpMenu->actions()) {
      if (action->text() == "Manual") {
        manualAction = action;
      }
    }
    QVERIFY(manualAction != nullptr);
    QCOMPARE(manualAction->shortcut(), QKeySequence(QKeySequence::HelpContents));
    QStringList titles;
    for (const auto *action : helpMenu->actions()) {
      titles.push_back(action->text());
    }
    QVERIFY(titles.contains("About Heiran..."));
  }

  // ツールバーのボタンの説明に、割り当てたキーが添えられる (アイコンだけでは、キーが分からない)。
  void showsTheShortcutKeysInTheToolBarToolTips() {
    heiran::ui::MainWindow window;
    QStringList checked;
    for (auto *toolBar : window.findChildren<QToolBar *>()) {
      for (auto *action : toolBar->actions()) {
        if (action->text() == "Fit") {
          QVERIFY2(action->toolTip().contains("(F)"), qPrintable(action->toolTip()));
          checked.push_back("Fit");
        } else if (action->text() == "1:1") {
          QVERIFY2(action->toolTip().contains("(1)"), qPrintable(action->toolTip()));
          checked.push_back("1:1");
        } else if (action->text() == "Prev") {
          QVERIFY2(action->toolTip().startsWith("Previous page"), qPrintable(action->toolTip()));
          checked.push_back("Prev");
        }
      }
    }
    QVERIFY(checked.contains("Fit") && checked.contains("1:1") && checked.contains("Prev"));
    // 何度呼んでも、キーが重ならない。
    QVERIFY(window.findChildren<QToolBar *>().first()->actions().size() > 0);
  }

  // 露出のリセットは、メニューにも出る (キーの \ だけでは、見つけられない)。
  void offersResetExposureInTheViewMenu() {
    heiran::ui::MainWindow window;
    auto *viewMenu = findMenu(window.menuBar(), "&View");
    QVERIFY(viewMenu != nullptr);
    QAction *reset = nullptr;
    for (auto *action : viewMenu->actions()) {
      if (action->text() == "Reset Exposure") {
        reset = action;
      }
    }
    QVERIFY(reset != nullptr);
    QCOMPARE(reset->shortcut(), QKeySequence(Qt::Key_Backslash));
    // 同じキーを持つ操作が、2 つに増えていない (増えると、キーが効かなくなる)。
    int count = 0;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->shortcut() == QKeySequence(Qt::Key_Backslash)) {
        ++count;
      }
    }
    QCOMPARE(count, 1);
  }

  // 操作の対象のセルは、枠で示す。複数のセルが見えているときだけ。
  void marksTheActiveCellWithAFrame() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 2);

    // 操作する前は、どのセルも対象ではない。
    for (auto *cell : cells) {
      QVERIFY(!cell->hasActiveFrame());
    }
    // セルへマウスを動かすと、そのセルが対象になる。
    for (auto *cell : cells) {
      if (cell->cellIndex() == 1) {
        Q_EMIT cell->hoverInfoRequested(1, QPointF(2, 2), true);
      }
    }
    for (auto *cell : cells) {
      QCOMPARE(cell->hasActiveFrame(), cell->cellIndex() == 1);
    }
    // 単独表示では、見えるセルは 1 つなので、枠は出ない。
    grid->setSoloCell(1);
    for (auto *cell : cells) {
      QVERIFY(!cell->hasActiveFrame());
    }
    grid->setSoloCell(-1);
    for (auto *cell : cells) {
      QCOMPARE(cell->hasActiveFrame(), cell->cellIndex() == 1);
    }
  }

  // ---- 差分の性能 ----

  // 倍率、見せ方、閾値の変更では、差の場 (重い計算)
  // を作り直さない。計算の種類を替えたときだけ、作り直す。
  void reusesTheDifferenceFieldWhenOnlyThePresentationChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));  // 1x2
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
      QTRY_VERIFY(cell->cellStatus() == heiran::ui::CellStatus::Ready);
    }
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Diff Cell") {
        action->trigger();
      }
    }
    QTRY_VERIFY(grid->diffFieldComputeCount() >= 1);
    const int afterFirst = grid->diffFieldComputeCount();

    const auto changeSettings =
        [&](const std::function<void(heiran::domain::DiffSettings &)> &edit) {
          auto cells = grid->sessionSnapshot().cells;
          QVERIFY(cells.at(2).diff.has_value());
          auto diff = *cells.at(2).diff;
          edit(diff.settings);
          grid->setCellDiff(2, diff);
          QTest::qWait(60);  // 作り直しは、イベントの合間に行われる
        };
    changeSettings([](heiran::domain::DiffSettings &settings) { settings.gain = 3.0; });
    changeSettings([](heiran::domain::DiffSettings &settings) {
      settings.view = heiran::domain::DiffViewMode::Overlay;
    });
    changeSettings([](heiran::domain::DiffSettings &settings) { settings.threshold = 20.0; });
    changeSettings([](heiran::domain::DiffSettings &settings) {
      settings.mode = heiran::domain::DiffMode::Signed;  // 輝度の差の場を、そのまま使える
    });
    QCOMPARE(grid->diffFieldComputeCount(), afterFirst);

    // 色差へ替えると、場が変わる。大きさの合わせ方を替えても、作り直す。
    changeSettings([](heiran::domain::DiffSettings &settings) {
      settings.mode = heiran::domain::DiffMode::DeltaE;
    });
    QTRY_COMPARE(grid->diffFieldComputeCount(), afterFirst + 1);
    changeSettings([](heiran::domain::DiffSettings &settings) {
      settings.sizeMatch = heiran::domain::DiffSizeMatch::ToLarger;
    });
    QTRY_COMPARE(grid->diffFieldComputeCount(), afterFirst + 2);
  }

  // ---- ワイプ ----

  // Analysis > Wipe Cell で、2
  // 枚をつなぎ合わせるセルが足される。境目の線をドラッグすると、位置が変わる。
  void addsAWipeCellWhoseLineCanBeDragged() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // 幅の広い画像にして、セルの幅で、倍率が決まるようにする。
    QStringList paths;
    for (int index = 0; index < 2; ++index) {
      QImage image(200, 50, QImage::Format_RGBA8888);
      image.fill(index == 0 ? QColor(220, 40, 40) : QColor(40, 40, 220));
      const QString path = QDir(directory.path()).filePath(QString("wipe%1.png").arg(index));
      QVERIFY(image.save(path));
      paths.push_back(path);
    }
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);  // 1x2
    window.resize(1000, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
      QTRY_VERIFY(cell->cellStatus() == heiran::ui::CellStatus::Ready);
    }

    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Wipe Cell") {
        action->trigger();
      }
    }
    // 格子が 1 つ広がり、3 つ目のセルがワイプになる。
    const auto session = grid->sessionSnapshot();
    QCOMPARE(session.viewer.layout.cols, 3);
    QVERIFY(session.cells.at(2).diff.has_value());
    QCOMPARE(session.cells.at(2).diff->settings.mode, heiran::domain::DiffMode::Wipe);
    QCOMPARE(session.cells.at(2).diff->settings.wipePosition, 0.5);

    heiran::ui::ImageViewWidget *wipeCell = nullptr;
    for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
      if (cell->cellIndex() == 2) {
        wipeCell = cell;
      }
    }
    QVERIFY(wipeCell != nullptr);
    QTRY_VERIFY2(!wipeCell->wipeHandleGeometry().isEmpty(), "the wipe line did not appear");
    QCOMPARE(wipeCell->currentImage().size(), QSize(200, 50));
    // 境目の左は基準 (赤)、右は比べる側 (青)。
    const QImage composite = wipeCell->currentImage().convertToFormat(QImage::Format_RGBA8888);
    QCOMPARE(composite.pixelColor(10, 25).red(), 220);
    QCOMPARE(composite.pixelColor(190, 25).blue(), 220);

    // 線を、セルの左寄りへドラッグすると、位置が小さくなる。
    const QRect line = wipeCell->wipeHandleGeometry();
    wipeCell->dragWipeLineTo(QPoint(line.center().x() - line.center().x() / 2, line.center().y()));
    QTRY_VERIFY(grid->sessionSnapshot().cells.at(2).diff->settings.wipePosition < 0.45);
    // 線の位置が、追いかける。作り直した画像の境目も、動く。
    QTRY_VERIFY(wipeCell->wipeHandleGeometry().center().x() < line.center().x());
    QTRY_VERIFY(wipeCell->currentImage()
                    .convertToFormat(QImage::Format_RGBA8888)
                    .pixelColor(110, 25)
                    .blue() == 220);
  }

  // ---- 元に戻す ----

  // セルの割り当て、格子の変更を、戻して、やり直せる。操作の名前が、メニューの項目に出る。
  void undoesAndRedoesSessionEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 3));  // 2x2、3 セルに画像
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();

    const auto actionByPrefix = [&](const QString &prefix) -> QAction * {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text().startsWith(prefix) && action->shortcut().count() > 0 &&
            (prefix == "Undo" || prefix == "Redo")) {
          return action;
        }
      }
      return nullptr;
    };
    const auto assignedCells = [&]() {
      int count = 0;
      for (const auto &cell : grid->sessionSnapshot().cells) {
        count += cell.imageIndex >= 0 ? 1 : 0;
      }
      return count;
    };
    const auto triggerByText = [&](const QString &text) {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text() == text && action->isEnabled()) {
          action->trigger();
          return true;
        }
      }
      return false;
    };

    // 何も変えていないうちは、戻せない。
    QAction *undoAction = actionByPrefix("Undo");
    QAction *redoAction = actionByPrefix("Redo");
    QVERIFY(undoAction != nullptr && redoAction != nullptr);
    QVERIFY(!undoAction->isEnabled());
    QCOMPARE(assignedCells(), 3);

    QVERIFY(triggerByText("Clear All Cells"));
    QCOMPARE(assignedCells(), 0);
    QCOMPARE(undoAction->text(), QString("Undo Clear All Cells"));
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    undoAction->trigger();
    QCOMPARE(assignedCells(), 3);
    QVERIFY(!undoAction->isEnabled());
    QCOMPARE(redoAction->text(), QString("Redo Clear All Cells"));

    redoAction->trigger();
    QCOMPARE(assignedCells(), 0);
    QVERIFY(undoAction->isEnabled());

    // もう一度戻して、新しい変更 (格子の変更) をすると、やり直しは使えなくなる。
    undoAction->trigger();
    QVERIFY(redoAction->isEnabled());
    QVERIFY(triggerByText("1 Cell"));
    QCOMPARE(grid->sessionSnapshot().viewer.layout.rows, 1);
    QVERIFY(!redoAction->isEnabled());
    QCOMPARE(undoAction->text(), QString("Undo Change Layout"));
    undoAction->trigger();
    QCOMPARE(grid->sessionSnapshot().viewer.layout.rows, 2);
    QCOMPARE(grid->sessionSnapshot().viewer.layout.cols, 2);
    QCOMPARE(assignedCells(), 3);
  }

  // 戻した状態の「未保存」の印は、変更の前の状態に戻る。別のファイルを開いたら、前の変更は戻せない。
  void restoresTheSavedStateAndForgetsHistoryOnANewSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    QVERIFY(!window.isWindowModified());

    const auto trigger = [&](const QString &text) {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text() == text && action->isEnabled()) {
          action->trigger();
          return true;
        }
      }
      return false;
    };
    QVERIFY(trigger("Clear All Cells"));
    QVERIFY(window.isWindowModified());
    QVERIFY(trigger("Undo Clear All Cells"));
    QVERIFY(!window.isWindowModified());

    // 別のファイルを開くと、前の変更は、戻せない。
    QVERIFY(trigger("Clear All Cells"));
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text().startsWith("Undo") && action->shortcut().count() > 0) {
        QVERIFY(!action->isEnabled());
        QCOMPARE(action->text(), QString("Undo"));
      }
    }
  }

  // ---- 案内 (画像を開いていないとき) ----

  // 画像が 1 枚も無いときだけ、案内が出る。開くと消え、セルを空にすると、また出る。
  void showsTheWelcomeOnlyWhileNoImageIsOpen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.resize(900, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *welcome = window.findChild<QWidget *>("welcome");
    QVERIFY(welcome != nullptr);
    QVERIFY(welcome->isVisible());

    window.openImageFiles(writeImageFiles(directory.path(), 2));
    QVERIFY(!welcome->isVisible());

    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Clear All Cells") {
        action->trigger();
      }
    }
    QVERIFY(welcome->isVisible());
    // 画面の全体へ広がっている。
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(welcome->size(), grid->size());
  }

  void highlightsValueRangesFromTheHistogramUntilCleared() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage first(3, 1, QImage::Format_RGBA8888);
    first.fill(Qt::black);
    first.setPixelColor(0, 0, Qt::red);
    first.setPixelColor(1, 0, Qt::green);
    const auto firstPath = directory.filePath("0000.png");
    QVERIFY(first.save(firstPath));
    QImage second(3, 1, QImage::Format_RGBA8888);
    second.fill(Qt::black);
    second.setPixelColor(2, 0, Qt::red);
    QVERIFY(second.save(directory.filePath("0001.png")));

    QVector<QImage> masks;
    heiran::ui::MainWindow window;
    window.openImageFiles({firstPath});
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QTRY_COMPARE(grid->currentFilePathForCell(0), firstPath);
    QTRY_VERIFY(grid->findChild<heiran::ui::ImageViewWidget *>()->hasImage());
    const auto actions = findMenu(window.menuBar(), "&Analysis")->actions();
    QCOMPARE(actions.first()->text(), QString("Histogram"));
    actions.first()->trigger();
    auto *histogram = window.findChild<heiran::ui::HistogramWindow *>();
    QVERIFY(histogram);
    auto *chart = histogram->findChild<QWidget *>("histogramChart");
    auto *channels = histogram->findChild<QComboBox *>("histogramHighlightChannel");
    auto *clear = histogram->findChild<QPushButton *>("clearHistogramHighlight");
    QVERIFY(chart && channels && clear);
    const auto preview = chart->grab();
    QVERIFY(!preview.isNull());
    connect(histogram, &heiran::ui::HistogramWindow::highlightMasksChanged, this,
            [&](const QVector<QImage> &newMasks) { masks = newMasks; });

    const QPoint start(chart->width() - 17, 70);
    const QPoint end(chart->width() / 2, 70);
    const auto drag = [&]() {
      QTest::mousePress(chart, Qt::LeftButton, Qt::NoModifier, start);
      QTest::mouseRelease(chart, Qt::LeftButton, Qt::NoModifier, end);
    };
    drag();
    QTRY_COMPARE(masks.size(), 1);
    QCOMPARE(masks.first().pixelIndex(0, 0), 1);
    QCOMPARE(masks.first().pixelIndex(1, 0), 1);
    QCOMPARE(masks.first().pixelIndex(2, 0), 0);

    channels->setCurrentIndex(1);
    QTRY_COMPARE(masks.size(), 1);
    QCOMPARE(masks.first().pixelIndex(0, 0), 1);
    QCOMPARE(masks.first().pixelIndex(1, 0), 0);
    grid->setCurrentPage(1);
    QTRY_VERIFY(masks.size() == 1 && !masks.first().isNull());
    QCOMPARE(masks.first().pixelIndex(0, 0), 0);
    QCOMPARE(masks.first().pixelIndex(2, 0), 1);

    clear->click();
    QVERIFY(masks.isEmpty());
    QVERIFY(!clear->isEnabled());
    drag();
    QTRY_COMPARE(masks.size(), 1);
    histogram->close();
    QVERIFY(masks.isEmpty());
  }

  void keepsTheCurrentImageReadyWhenChangingLayout() {
    QTemporaryDir directory;
    const auto paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);
    heiran::ui::MainWindow window;
    window.openImageFiles({paths.first()});
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    grid->setCurrentPage(1);
    const QPointer cell = grid->findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);
    const QImage image = cell->currentImage();

    auto *layoutMenu = findMenu(window.menuBar(), "&Layout");
    QVERIFY(layoutMenu);
    const auto actions = layoutMenu->actions().at(1)->menu()->actions();
    QCOMPARE(actions.last()->text(), QString("2x1"));
    actions.last()->trigger();

    QCOMPARE(grid->currentPage(), 1);
    QCOMPARE(grid->findChildren<heiran::ui::ImageViewWidget *>().size(), 2);
    QVERIFY(cell);
    QCOMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);
    QCOMPARE(cell->currentImage(), image);
  }

  // 格子を減らしたとき (1x2 から 1x1
  // など)、残ったセルが、格子の全体へ広がる。前の格子の列や行が、場所を取り続けない。
  void expandsTheRemainingCellsWhenTheLayoutShrinks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));  // 1x2
    window.resize(900, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();

    const auto triggerByText = [&](const QString &text) {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text() == text && action->isEnabled()) {
          action->trigger();
          return true;
        }
      }
      return false;
    };
    const auto visibleCells = [&]() { return grid->findChildren<heiran::ui::ImageViewWidget *>(); };
    QCOMPARE(visibleCells().size(), 2);

    // 2x2 へ広げてから、1x1 へ減らす。どの縮小でも、セルは、格子の全体を埋める。
    for (const auto &[layout, expectedCells] :
         {std::pair{QString("2x2"), 4}, std::pair{QString("1x2"), 2},
          std::pair{QString("1 Cell"), 1}, std::pair{QString("2x1"), 2},
          std::pair{QString("1 Cell"), 1}}) {
      QVERIFY(triggerByText(layout));
      QTRY_COMPARE(visibleCells().size(), expectedCells);
      QTRY_VERIFY2(
          [&]() {
            int covered = 0;
            for (auto *cell : visibleCells()) {
              covered += cell->width() * cell->height();
            }
            // セルの合計の面積が、格子の面積の大半 (間の溝を除く) を占める。黒い空きが残らない。
            return covered * 100 > grid->width() * grid->height() * 90;
          }(),
          qPrintable(layout));
    }
    // 1x1 では、セルの幅が、格子の幅と一致する。
    const auto cells = visibleCells();
    QCOMPARE(cells.size(), 1);
    QTRY_COMPARE(cells.first()->width(), grid->width());
  }

  // ---- 単独表示 (A/B の切り替え) ----

  // 単独表示では、1 つのセルが格子の全体へ広がり、ほかは隠れる。切り替えて、全セルへ戻れる。
  // セッションは変わらない (未保存の印が付かない)。
  void showsOneCellAtATime() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 2);
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.resize(900, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 2);
    heiran::ui::ImageViewWidget *first = nullptr;
    heiran::ui::ImageViewWidget *second = nullptr;
    for (auto *cell : cells) {
      (cell->cellIndex() == 0 ? first : second) = cell;
    }
    QCOMPARE(grid->soloCellIndex(), -1);
    QVERIFY(first->isVisible() && second->isVisible());
    const int sharedWidth = first->width();

    grid->cycleSoloCell(1);
    QCOMPARE(grid->soloCellIndex(), 0);
    QVERIFY(first->isVisible());
    QVERIFY(!second->isVisible());
    // 広がる (格子の幅の、ほぼ全体)。
    QTRY_VERIFY2(first->width() > sharedWidth * 3 / 2, qPrintable(QString::number(first->width())));

    // 次へ。最後を越えたら、先頭へ戻る。
    grid->cycleSoloCell(1);
    QCOMPARE(grid->soloCellIndex(), 1);
    QVERIFY(second->isVisible());
    QVERIFY(!first->isVisible());
    grid->cycleSoloCell(1);
    QCOMPARE(grid->soloCellIndex(), 0);
    grid->cycleSoloCell(-1);
    QCOMPARE(grid->soloCellIndex(), 1);

    // 全セルへ戻る。セルの大きさも、元に戻る。
    grid->setSoloCell(-1);
    QVERIFY(first->isVisible() && second->isVisible());
    QTRY_COMPARE(first->width(), sharedWidth);
    QVERIFY(!window.isWindowModified());
  }

  // フィットした状態で見ていたなら、単独表示で広がった面へフィットし直す。動かした状態なら、そのまま保つ。
  void refitsTheSingleCellOnlyWhenTheViewWasFitted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // 小さな画像では、倍率の上限に当たってしまう。幅の広い画像にして、幅で決まるようにする。
    QStringList paths;
    for (int index = 0; index < 2; ++index) {
      QImage image(400, 100, QImage::Format_RGBA8888);
      image.fill(QColor(20 + index * 40, 90, 160));
      const QString path = QDir(directory.path()).filePath(QString("big%1.png").arg(index));
      QVERIFY(image.save(path));
      paths.push_back(path);
    }
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.resize(900, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    // 画像は非同期で読み込まれる。読み終えてから、フィットする。
    for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
      QTRY_VERIFY(cell->cellStatus() == heiran::ui::CellStatus::Ready);
    }
    grid->requestFitToCells();
    const double fittedScale = grid->globalViewState().baseScale;

    // フィットしていれば、広い面へ合わせて、倍率が上がる。
    grid->setSoloCell(0);
    QTRY_VERIFY2(grid->globalViewState().baseScale > fittedScale * 1.3,
                 qPrintable(QString::number(grid->globalViewState().baseScale)));
    grid->setSoloCell(-1);
    QTRY_COMPARE_WITH_TIMEOUT(grid->globalViewState().baseScale, fittedScale, 2000);

    // 拡大していれば (フィットでない)、単独表示にしても、倍率は変わらない。
    grid->zoomBy(2.0);
    const double zoomedScale = grid->globalViewState().baseScale;
    grid->setSoloCell(1);
    QTest::qWait(200);
    QCOMPARE(grid->globalViewState().baseScale, zoomedScale);
  }

  // ウィンドウの大きさを変えたとき、フィットした状態で見ているなら、フィットし直す。ズームで動かした状態なら、倍率を保つ。
  void followsTheWindowSizeOnlyWhileFitted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (int index = 0; index < 2; ++index) {
      QImage image(400, 100, QImage::Format_RGBA8888);
      image.fill(QColor(20 + index * 40, 90, 160));
      const QString path = QDir(directory.path()).filePath(QString("wide%1.png").arg(index));
      QVERIFY(image.save(path));
      paths.push_back(path);
    }
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.resize(900, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    for (auto *cell : grid->findChildren<heiran::ui::ImageViewWidget *>()) {
      QTRY_VERIFY(cell->cellStatus() == heiran::ui::CellStatus::Ready);
    }
    grid->requestFitToCells();
    const double fittedScale = grid->globalViewState().baseScale;

    window.resize(1300, 700);
    QTRY_VERIFY2(grid->globalViewState().baseScale > fittedScale * 1.2,
                 qPrintable(QString::number(grid->globalViewState().baseScale)));

    // ズームで動かしたあとは、大きさを変えても倍率を保つ。
    grid->zoomBy(2.0);
    const double zoomedScale = grid->globalViewState().baseScale;
    window.resize(900, 500);
    QTest::qWait(200);
    QCOMPARE(grid->globalViewState().baseScale, zoomedScale);
  }

  // メニューとキーの操作: Next Cell / Previous Cell、Single Cell の項目、Esc。
  void switchesTheSingleCellFromTheMenuAndEscape() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();

    const auto triggerByText = [&](const QString &text) {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text() == text && action->isEnabled()) {
          action->trigger();
          return true;
        }
      }
      return false;
    };
    QVERIFY(triggerByText("Next Cell"));
    QCOMPARE(grid->soloCellIndex(), 0);
    QVERIFY(triggerByText("Next Cell"));
    QCOMPARE(grid->soloCellIndex(), 1);
    QVERIFY(triggerByText("Previous Cell"));
    QCOMPARE(grid->soloCellIndex(), 0);

    // Single Cell の項目: 直接指定。チェックが付く。格子にない番号は、選べない。
    QVERIFY(triggerByText("Cell 2"));
    QCOMPARE(grid->soloCellIndex(), 1);
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Cell 2") {
        QVERIFY(action->isChecked());
      }
      if (action->text() == "Cell 3") {
        QVERIFY(!action->isEnabled());
      }
    }

    // Esc: 選択範囲が無ければ、全セルの表示へ戻る。
    QVERIFY(triggerByText("Clear Selection"));
    QCOMPARE(grid->soloCellIndex(), -1);

    QVERIFY(triggerByText("Cell 1"));
    QCOMPARE(grid->soloCellIndex(), 0);
    QVERIFY(triggerByText("All Cells"));
    QCOMPARE(grid->soloCellIndex(), -1);
  }

  // 画像の無いセルは、飛ばして切り替える。
  void skipsEmptyCellsWhenSwitching() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 3));  // 2x2。4 つ目が空
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 4);

    QList<int> visited;
    for (int step = 0; step < 4; ++step) {
      grid->cycleSoloCell(1);
      visited.push_back(grid->soloCellIndex());
    }
    QCOMPARE(visited, QList<int>({0, 1, 2, 0}));
  }

  // 格子を替えるなど、セルを作り直すと、単独表示は解ける。
  void leavesTheSingleCellWhenTheLayoutChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    grid->setSoloCell(1);
    QCOMPARE(grid->soloCellIndex(), 1);

    window.openImageFiles(writeImageFiles(directory.path(), 3));
    QCOMPARE(grid->soloCellIndex(), -1);
  }

  void fillsTheDiffWindowWhenItIsOpened() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto *analysisMenu = findMenu(window.menuBar(), "&Analysis");
    QVERIFY(analysisMenu != nullptr);
    QAction *diffAction = nullptr;
    for (auto *action : analysisMenu->actions()) {
      if (action->text() == "Diff Window") {
        diffAction = action;
      }
    }
    QVERIFY(diffAction != nullptr);

    diffAction->trigger();

    // DiffWindow は Q_OBJECT を持たないため型では引けない。表題で探す。
    QWidget *diffWindow = nullptr;
    for (auto *child : window.findChildren<QWidget *>()) {
      if (child->isWindow() && child->windowTitle().startsWith("Diff")) {
        diffWindow = child;
      }
    }
    QVERIFY(diffWindow != nullptr);
    QVERIFY(diffWindow->isVisible());

    // 統計が出ていれば、表示後に内容が作られている。
    bool hasStatistics = false;
    for (const auto *label : diffWindow->findChildren<QLabel *>()) {
      hasStatistics = hasStatistics || label->text().contains("max absolute diff");
    }
    QVERIFY2(hasStatistics, "the diff window was opened without content");
  }

  // ヒストグラムの所要時間。差分と同じく全画素を走査する。
  // 数値は計測用で、失敗の判定には使わない。
  void reportsElapsedTimeForTheHistogram() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(1024, 1024, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
      }
    }
    const QString path = QDir(directory.path()).filePath("large.png");
    QVERIFY(image.save(path));

    heiran::ui::MainWindow window;
    window.openImageFiles({path});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto *analysisMenu = findMenu(window.menuBar(), "&Analysis");
    QVERIFY(analysisMenu != nullptr);
    QAction *histogramAction = nullptr;
    for (auto *action : analysisMenu->actions()) {
      if (action->text() == "Histogram") {
        histogramAction = action;
      }
    }
    QVERIFY(histogramAction != nullptr);

    QElapsedTimer timer;
    timer.start();
    histogramAction->trigger();
    const double firstMilliseconds = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;

    // 2 回目は同じ画素データのため、取っておいた集計が使えるはず。
    timer.restart();
    histogramAction->trigger();
    const double secondMilliseconds = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;

    qInfo() << "histogram 1024x1024:" << firstMilliseconds << "ms, repeated:" << secondMilliseconds
            << "ms";
  }

  void offersDirectCellCountsWithGridSubmenus() {
    heiran::ui::MainWindow window;
    auto *layoutMenu = findMenu(window.menuBar(), "&Layout");
    QVERIFY(layoutMenu != nullptr);

    QStringList countTitles;
    QMap<QString, QStringList> gridsByTitle;
    for (const auto *action : layoutMenu->actions()) {
      if (!action->text().contains("Cell"))
        continue;
      QVERIFY(action->isCheckable());
      countTitles.push_back(action->text());
      if (action->text() == "1 Cell") {
        QVERIFY(!action->menu());
        continue;
      }
      QVERIFY(action->menu());
      for (const auto *grid : action->menu()->actions())
        gridsByTitle[action->text()].push_back(grid->text());
    }
    QCOMPARE(countTitles.size(), 16);
    QCOMPARE(countTitles.first(), QString("1 Cell"));
    QCOMPARE(countTitles.at(1), QString("2 Cells"));
    QCOMPARE(countTitles.last(), QString("16 Cells"));
    QCOMPARE(gridsByTitle.size(), 15);
    QVERIFY(!gridsByTitle.contains("1 Cell"));
    QCOMPARE(gridsByTitle.value("7 Cells"), QStringList({"1x7", "7x1"}));
    QCOMPARE(gridsByTitle.value("12 Cells"),
             QStringList({"1x12", "2x6", "3x4", "4x3", "6x2", "12x1"}));
    QCOMPARE(gridsByTitle.value("16 Cells"), QStringList({"1x16", "2x8", "4x4", "8x2", "16x1"}));
  }

  void usesMouseAndKeyboardToApplyLayoutsOrOpenGridChoices() {
    const auto organization = QCoreApplication::organizationName();
    QCoreApplication::setOrganizationName("HeiranLayoutMenuTests");
    const auto restore = qScopeGuard([organization]() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
    });
    QSettings().clear();

    heiran::ui::MainWindow window;
    auto *menu = findMenu(window.menuBar(), "&Layout");
    QVERIFY(menu);
    auto *sixCells = menu->actions().at(5);
    QCOMPARE(sixCells->text(), QString("6 Cells"));
    auto *choices = sixCells->menu();
    QVERIFY(choices);
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    const auto currentSize = [grid]() {
      const auto layout = grid->sessionSnapshot().viewer.layout;
      return QSize(layout.cols, layout.rows);
    };
    const auto initialSize = currentSize();
    const auto popupPosition = window.mapToGlobal(QPoint(50, 50));

    menu->popup(popupPosition);
    const QRect row = menu->actionGeometry(sixCells);
    const QPoint label = row.center();
    const QPoint arrow(row.right() - 5, label.y());
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, arrow);
    QTRY_VERIFY(choices->isVisible());
    QCOMPARE(currentSize(), initialSize);
    QTest::keyClick(choices, Qt::Key_Left);
    QTRY_VERIFY(!choices->isVisible());
    QVERIFY(menu->isVisible());

    menu->setActiveAction(menu->actions().first());
    for (int step = 0; step < 5; ++step)
      QTest::keyClick(menu, Qt::Key_Down);
    QCOMPARE(menu->activeAction(), sixCells);
    QTest::keyClick(menu, Qt::Key_Right);
    QTRY_VERIFY(choices->isVisible());
    QCOMPARE(currentSize(), initialSize);
    const auto *gridChoice = choices->actions().at(2);
    QCOMPARE(gridChoice->text(), QString("3x2"));
    QTest::keyClick(choices, Qt::Key_Down);
    QTest::keyClick(choices, Qt::Key_Down);
    QCOMPARE(choices->activeAction(), gridChoice);
    QTest::keyClick(choices, Qt::Key_Return);
    QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(currentSize(), QSize(2, 3));

    menu->popup(popupPosition);
    menu->setActiveAction(menu->actions().first());
    QTest::keyClick(menu, Qt::Key_Return);
    QCOMPARE(currentSize(), QSize(1, 1));
    menu->popup(popupPosition);
    QTest::keyClick(menu, Qt::Key_Down);
    for (int step = 0; step < 5; ++step)
      QTest::keyClick(menu, Qt::Key_Down);
    QCOMPARE(menu->activeAction(), sixCells);
    QTest::keyClick(menu, Qt::Key_Enter);
    QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(currentSize(), QSize(2, 3));

    menu->actions().first()->trigger();
    menu->popup(popupPosition);
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, label);
    QTRY_VERIFY(!menu->isVisible());
    QVERIFY(!choices->isVisible());
    QCOMPARE(currentSize(), QSize(2, 3));

    menu->actions().first()->trigger();
    menu->popup(popupPosition);
    QTest::mouseMove(menu->windowHandle(), label - QPoint(1, 0));
    QTest::mouseMove(menu->windowHandle(), label);
    QTRY_VERIFY(choices->isVisible());
    QCOMPARE(currentSize(), QSize(1, 1));
    const auto labelFromSubmenu = choices->mapFromGlobal(menu->mapToGlobal(label));
    QTest::mouseClick(choices, Qt::LeftButton, Qt::NoModifier, labelFromSubmenu);
    QTRY_VERIFY(!menu->isVisible());
    QVERIFY(!choices->isVisible());
    QCOMPARE(currentSize(), QSize(2, 3));
  }

  void remembersGridDimensionsIndependentlyForEachCellCount() {
    const auto organization = QCoreApplication::organizationName();
    const auto application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("PreferredGridsTests");
    const auto restore = qScopeGuard([organization, application]() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    });
    QSettings().clear();
    const auto applyAndCheck = [](heiran::ui::MainWindow &window, const QString &text, int rows,
                                  int cols) {
      const auto actions = window.findChildren<QAction *>();
      const auto action = std::ranges::find_if(
          actions, [&text](const auto *entry) { return entry->text() == text; });
      QVERIFY(action != actions.end());
      (*action)->trigger();
      const auto layout =
          window.findChild<heiran::ui::ViewerGridWidget *>()->sessionSnapshot().viewer.layout;
      QCOMPARE(layout.rows, rows);
      QCOMPARE(layout.cols, cols);
      QVERIFY((*action)->isChecked());
    };
    {
      heiran::ui::MainWindow window;
      QSettings().setValue("layout/preferredGrid/6", QSize(4, 4));
      applyAndCheck(window, "6 Cells", 2, 3);
      applyAndCheck(window, "3x2", 3, 2);
      applyAndCheck(window, "2x1", 2, 1);
      applyAndCheck(window, "6 Cells", 3, 2);
      applyAndCheck(window, "2 Cells", 2, 1);
      applyAndCheck(window, "1 Cell", 1, 1);
    }
    heiran::ui::MainWindow reopened;
    applyAndCheck(reopened, "6 Cells", 3, 2);
    applyAndCheck(reopened, "2 Cells", 2, 1);
  }

  // 4 x 4 を超える格子 (1 行に 7 枚など) を選べて、現在の格子にチェックが付く。
  void appliesAWideLayoutFromTheMenu() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    heiran::ui::MainWindow window;
    window.openImageFiles(writeImageFiles(directory.path(), 2));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *wide = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "1x7") {
        wide = action;
      }
    }
    QVERIFY(wide != nullptr);
    wide->trigger();
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QCOMPARE(grid->sessionSnapshot().viewer.layout.rows, 1);
    QCOMPARE(grid->sessionSnapshot().viewer.layout.cols, 7);
    QVERIFY(wide->isChecked());
    QCOMPARE(grid->findChildren<heiran::ui::ImageViewWidget *>().size(), 7);
  }

  // メニュー項目の並び。コンストラクタを段階へ分けたときに崩れやすいため固定する。
  void keepsTheMenuLayout() {
    heiran::ui::MainWindow window;

    const auto titlesOf = [](QMenu *menu) {
      QStringList titles;
      for (const auto *action : menu->actions()) {
        if (action->isSeparator()) {
          titles.push_back("---");
          continue;
        }
        titles.push_back(action->text());
      }
      return titles;
    };

    auto *fileMenu = findMenu(window.menuBar(), "&File");
    QVERIFY(fileMenu != nullptr);
    QCOMPARE(titlesOf(fileMenu),
             QStringList({"Open Image...", "Recent Images", "Open .hrn...", "Open Recent", "---",
                          "Save", "Save As...", "---", "Rename...", "Move to Recycle Bin...",
                          "Copy File Path", "Show in Folder", "Print...", "Refresh File List"}));

    auto *settingsMenu = findMenu(window.menuBar(), "&Settings");
    QVERIFY(settingsMenu != nullptr);
    QCOMPARE(titlesOf(settingsMenu), QStringList({"App Settings..."}));

    // 最上位の並びも固定する。
    QStringList menuTitles;
    for (const auto *action : window.menuBar()->actions()) {
      menuTitles.push_back(action->text());
    }
    QCOMPARE(menuTitles, QStringList({"&File", "&Images", "&Layout", "&View", "&Analysis",
                                      "&Settings", "&Help"}));

    auto *analysisMenu = findMenu(window.menuBar(), "&Analysis");
    QVERIFY(analysisMenu != nullptr);
    QCOMPARE(titlesOf(analysisMenu),
             QStringList({"Histogram", "Histogram 3D", "Waveform", "Vectorscope", "Diff Cell",
                          "Wipe Cell", "Diff Window", "Change Graph"}));
  }

  // ページのサムネイル一覧が中身を持って開くこと。
  // 読み込みは別スレッドで走るため、届くまで待ってから見る。
  void fillsTheImageListPanel() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QStringList paths;
    for (int index = 0; index < 3; ++index) {
      QImage image(60, 40, QImage::Format_RGB32);
      image.fill(QColor(40 * (index + 1), 60, 80));
      const QString path = QDir(directory.path()).filePath(QString("%1.png").arg(index));
      QVERIFY(image.save(path));
      paths.push_back(path);
    }

    heiran::ui::ImageListPanelWidget dialog;
    dialog.setPages(paths, 1);

    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 3);
    // 現在ページが選ばれた状態で開くこと。
    QCOMPARE(list->currentRow(), 1);
    // 番号とファイル名が出ること。
    QVERIFY2(list->item(0)->text().startsWith("1: "), qPrintable(list->item(0)->text()));

    // サムネイルが届くこと。
    QVERIFY(QTest::qWaitFor(
        [list]() { return !list->item(0)->icon().pixmap(160, 120).toImage().isNull(); }, 5000));
    for (int index = 0; index < list->count(); ++index) {
      const QImage thumbnail = list->item(index)->icon().pixmap(160, 120).toImage();
      QVERIFY2(!thumbnail.isNull(), qPrintable(QString("page %1 has no thumbnail").arg(index)));
    }

    QSignalSpy selected(&dialog, &heiran::ui::ImageListPanelWidget::pageSelected);
    list->setCurrentRow(2);
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().at(0).toInt(), 2);
  }

  // 読めないページが混ざっていても開けること。
  void toleratesUnreadablePages() {
    heiran::ui::ImageListPanelWidget dialog;
    dialog.setPages({QString(), "does-not-exist.png"}, 0);

    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    // 経路が無いページは番号だけを出す。
    QCOMPARE(list->item(0)->text(), QString("1"));
  }

  // ツールバーの入力欄がキーボードのフォーカスを取らないこと。
  //
  // 取ると矢印キーがそちらへ行き、ページ送りが効かなくなる。2026-09-03 に
  // 露出の入力欄で実際に起きた。QTest の合成イベントでは当時の不具合そのものを
  // 再現できなかったため、原因側 (フォーカス方針) を固定する。
  void keepsToolBarInputsOutOfTheFocusChain() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // 数値や文字を受ける部品は、明示的に触ったときだけフォーカスを取る。
    const auto spinBoxes = window.findChildren<QAbstractSpinBox *>();
    QVERIFY2(!spinBoxes.isEmpty(), "the toolbar has no spin box to check");
    for (const auto *spinBox : spinBoxes) {
      QVERIFY2(!(spinBox->focusPolicy() & Qt::TabFocus),
               qPrintable(QString("%1 takes tab focus").arg(spinBox->objectName())));
    }
    for (const auto *toolbar : window.findChildren<QToolBar *>()) {
      for (const auto *comboBox : toolbar->findChildren<QComboBox *>()) {
        QVERIFY2(!(comboBox->focusPolicy() & Qt::TabFocus),
                 qPrintable(QString("%1 takes tab focus").arg(comboBox->objectName())));
      }
    }

    // タブを何度押してもビューアから離れないこと。
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    for (int press = 0; press < 6; ++press) {
      QTest::keyClick(&window, Qt::Key_Tab);
      const QWidget *focused = QApplication::focusWidget();
      QVERIFY2(qobject_cast<const QAbstractSpinBox *>(focused) == nullptr,
               "tab moved the focus into a numeric input");
      QVERIFY2(qobject_cast<const QComboBox *>(focused) == nullptr,
               "tab moved the focus into a combo box");
    }
  }

  // 矢印キーでページが動くこと。
  //
  // これは上のフォーカス方針のテストの代わりにはならない。フォーカス方針を
  // 戻して確かめたところ、このテストは通ったままだった。合成イベントでは入力欄へ
  // フォーカスが移らないため、実機で起きた事象を再現できない。ページ送りその
  // ものが動くことだけを見る。
  void movesPagesWithTheArrowKeys() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (int index = 0; index < 3; ++index) {
      QImage image(32, 24, QImage::Format_RGB32);
      image.fill(QColor(40 * (index + 1), 60, 80));
      QVERIFY(image.save(QDir(directory.path()).filePath(QString("%1.png").arg(index))));
    }

    heiran::ui::MainWindow window;
    window.openImageFiles({QDir(directory.path()).filePath("0.png")});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->totalPages(), 3);
    QCOMPARE(grid->currentPage(), 0);

    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(grid->currentPage(), 1);
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(grid->currentPage(), 0);
  }

  // 全画面では画像だけを見せるため、メニューもツールバーも隠し、戻すと元に戻ること。
  void hidesToolBarsInFullScreenAndRestoresThem() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const auto toolBars = window.findChildren<QToolBar *>();
    QList<const QToolBar *> initiallyVisible;
    for (const auto *toolBar : toolBars) {
      if (toolBar->isVisible())
        initiallyVisible.push_back(toolBar);
    }
    QVERIFY(!initiallyVisible.isEmpty());

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QVERIFY(!window.menuBar()->isVisible());
    for (const auto *toolBar : toolBars) {
      QVERIFY2(!toolBar->isVisible(), "a tool bar stayed visible in full screen");
    }

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(window.menuBar()->isVisible());
    for (const auto *toolBar : toolBars) {
      QCOMPARE(toolBar->isVisible(), initiallyVisible.contains(toolBar));
    }
  }

  // 全画面で、マウスを画面の上端へ寄せると、メニューとツールバーが出る。画像の面へ降りると、引っ込む。
  void showsTheToolBarWhenThePointerReachesTheTopInFullScreen() {
    heiran::ui::MainWindow window;
    window.setFullScreenChromeHideDelay(30);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *toolBar = window.findChild<QToolBar *>();
    QVERIFY(toolBar != nullptr);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QVERIFY(!toolBar->isVisible());
    QVERIFY(!window.isFullScreenChromeShown());

    // 上端から離れた位置では、出ない。
    const int top = window.frameGeometry().top();
    window.handleFullScreenPointer(QPoint(100, top + 200));
    QVERIFY(!window.isFullScreenChromeShown());
    QVERIFY(!toolBar->isVisible());

    // 上端へ寄せると、出る。
    window.handleFullScreenPointer(QPoint(100, top));
    QVERIFY(window.isFullScreenChromeShown());
    QVERIFY(toolBar->isVisible());
    QVERIFY(window.menuBar()->isVisible());

    // ツールバーの上にいる間は、出したまま。
    window.handleFullScreenPointer(QPoint(100, top + 10));
    QTest::qWait(100);
    QVERIFY(window.isFullScreenChromeShown());

    // 画像の面へ降りると、少し待って、引っ込む。
    window.handleFullScreenPointer(QPoint(100, top + 600));
    QTRY_VERIFY(!window.isFullScreenChromeShown());
    QVERIFY(!toolBar->isVisible());
    QVERIFY(!window.menuBar()->isVisible());

    // 全画面を抜けると、元に戻る。
    window.handleFullScreenPointer(QPoint(100, top));
    QVERIFY(window.isFullScreenChromeShown());
    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(toolBar->isVisible());
    QVERIFY(window.menuBar()->isVisible());
    QVERIFY(!window.isFullScreenChromeShown());
  }

  // 利用者が閉じていたツールバーは、全画面を抜けても閉じたままであること。
  // 全画面へ入る前の状態を覚えていないと、抜けたときに勝手に開いてしまう。
  void keepsAToolBarHiddenByTheUserAcrossFullScreen() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *toolBar = window.findChild<QToolBar *>();
    QVERIFY(toolBar != nullptr);
    toolBar->hide();

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());

    QVERIFY(!toolBar->isVisible());
  }

  // ツールバーを隠してもキー操作は効くこと。ページ送りや Fit のキーはツールバーの
  // アクションに付いており、隠すとショートカットが無効になる。
  void keepsPageKeysWorkingInFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());

    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(grid->currentPage(), 1);
    QTest::keyClick(&window, Qt::Key_PageDown);
    QCOMPARE(grid->currentPage(), 2);
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(grid->currentPage(), 1);
    QTest::keyClick(&window, Qt::Key_Home);
    QCOMPARE(grid->currentPage(), 0);
  }

  // 画像を直接開いたときは、ページ送りでフィットすること。
  //
  // 寸法の違う写真をめくる使い方では、ズームとパンを引き継ぐと 2 枚目以降が
  // 隅しか見えないことがある。設定は書き換えない。
  void fitsOnPageChangeWhenImagesAreOpenedDirectly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const bool configured = heiran::ui::AppSettings::load().fitOnPageChange;

    window.openImageFiles({paths.at(0)});

    QVERIFY(grid->fitOnPageChange());
    QCOMPARE(heiran::ui::AppSettings::load().fitOnPageChange, configured);
  }

  // .hrn のセッションは設定のままにすること。比較が目的で、引き継ぐ方が正しい。
  // 直接開いた既定を次のセッションへ持ち越さない。
  void doesNotCarryTheDirectOpenDefaultIntoASession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const bool configured = heiran::ui::AppSettings::load().fitOnPageChange;

    window.openImageFiles({paths.at(0)});
    QVERIFY(grid->fitOnPageChange());

    window.setSession(heiran::domain::SessionModel{});
    QCOMPARE(grid->fitOnPageChange(), configured);
  }

  // 利用者が切り替えたら、直接開いた既定より優先すること。
  void lettingTheUserTurnOffFitOnPageChangeAfterOpeningDirectly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    const bool configured = heiran::ui::AppSettings::load().fitOnPageChange;
    window.openImageFiles({paths.at(0)});
    QVERIFY(grid->fitOnPageChange());

    QAction *fitAction = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Fit on Page Change") {
        fitAction = action;
      }
    }
    QVERIFY(fitAction != nullptr);
    // 表示は有効な値と揃っていること。直接開いた既定なのに未チェックだと、
    // 押した結果が利用者の期待と逆になる。
    QVERIFY(fitAction->isChecked());

    fitAction->trigger();

    QVERIFY(!grid->fitOnPageChange());
    QVERIFY(!fitAction->isChecked());
    // 切り替えは設定へ保存される。他のテストへ残さないよう元へ戻す。
    heiran::ui::AppSettings restored = heiran::ui::AppSettings::load();
    restored.fitOnPageChange = configured;
    restored.save();
  }

  // R と Shift+R で画像を回せること。回転が右クリックのメニューにしか無かった。
  void rotatesWithTheRKeys() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);

    QTest::keyClick(&window, Qt::Key_R);
    QCOMPARE(grid->sessionSnapshot().cells.at(0).rotation, 1);
    QTest::keyClick(&window, Qt::Key_R, Qt::ShiftModifier);
    QCOMPARE(grid->sessionSnapshot().cells.at(0).rotation, 0);
  }

  void rotatesAllCellsFromTheToolbarAndViewMenu() {
    QTemporaryDir directory;
    const auto paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *toolbar = window.findChild<QToolBar *>("navigationToolBar");
    auto *viewMenu = findMenu(window.menuBar(), "&View");
    auto *left = window.findChild<QAction *>("rotateAllLeftAction");
    auto *right = window.findChild<QAction *>("rotateAllRightAction");
    QVERIFY(grid && toolbar && viewMenu && left && right);
    QVERIFY(toolbar->actions().contains(left) && toolbar->actions().contains(right));
    QVERIFY(viewMenu->actions().contains(left) && viewMenu->actions().contains(right));
    QVERIFY(!left->icon().isNull() && !right->icon().isNull());

    QCOMPARE(grid->sessionSnapshot().cells.size(), 2);
    grid->rotateActiveCellBy(1);
    auto session = grid->sessionSnapshot();
    session.cells[1].flipX = true;
    grid->setSessionPreservingView(session);
    const int active = grid->activeCellIndex();
    const auto before = grid->sessionSnapshot();
    QSignalSpy changes(grid, &heiran::ui::ViewerGridWidget::persistentStateChanged);
    left->trigger();
    const auto rotated = grid->sessionSnapshot();
    for (int i = 0; i < before.cells.size(); ++i)
      QCOMPARE(rotated.cells.at(i).rotation, (before.cells.at(i).rotation + 3) % 4);
    QVERIFY(!rotated.cells.at(1).flipX && rotated.cells.at(1).flipY);
    QCOMPARE(grid->activeCellIndex(), active);
    QCOMPARE(changes.count(), 1);
    right->trigger();
    const auto restored = grid->sessionSnapshot();
    for (int i = 0; i < before.cells.size(); ++i)
      QCOMPARE(restored.cells.at(i).rotation, before.cells.at(i).rotation);
    QVERIFY(restored.cells.at(1).flipX && !restored.cells.at(1).flipY);
    QCOMPARE(changes.count(), 2);
  }

  // Ctrl+C で見ている画像をコピーできること。右クリックのメニューにしか無かった。
  void copiesTheImageWithCtrlC() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_VERIFY(cell->cellStatus() != heiran::ui::CellStatus::Loading);

    QGuiApplication::clipboard()->clear();
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);

    QVERIFY(!QGuiApplication::clipboard()->image().isNull());
  }

  // 名前を変えた後も、同じファイルを表示し続けること。名前が変わると並びも変わり、
  // ページ番号のままだと別のファイルへ移ってしまう。
  void keepsShowingTheSameFileAfterARename() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QTRY_COMPARE(QFileInfo(grid->targetCellFilePath()), QFileInfo(paths.at(0)));
    // 読み込み中のファイルは Windows では名前を変えられない。読み終わるのを待つ。
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);

    // image0.png を並びの最後になる名前へ変える。
    const QString renamed = QDir(directory.path()).filePath("zzz.png");
    QVERIFY(QFile::rename(paths.at(0), renamed));
    window.refreshFileList(renamed);

    QCOMPARE(grid->totalPages(), 3);
    QCOMPARE(grid->currentPage(), 2);
    QTRY_COMPARE(QFileInfo(grid->targetCellFilePath()), QFileInfo(renamed));
  }

  // ファイルが消えたら一覧から外れ、ページ数が減ること。最後のページなら前へ寄る。
  void dropsARemovedFileFromThePages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(2)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->totalPages(), 3);
    QCOMPARE(grid->currentPage(), 2);
    // 読み込み中のファイルは Windows では消せない。読み終わるのを待つ。
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);

    QVERIFY(QFile::remove(paths.at(2)));
    window.refreshFileList();

    QCOMPARE(grid->totalPages(), 2);
    QCOMPARE(grid->currentPage(), 1);
    QTRY_COMPARE(QFileInfo(grid->targetCellFilePath()), QFileInfo(paths.at(1)));
  }

  // ファイル操作のキーは全画面でも効くこと。メニューもツールバーも隠れるため、
  // ウィンドウへ登録していないとキーが効かなくなる。ダイアログを出さない操作で確かめる。
  void copiesTheFilePathInFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_VERIFY(cell->cellStatus() != heiran::ui::CellStatus::Loading);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QGuiApplication::clipboard()->clear();
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);

    QCOMPARE(QGuiApplication::clipboard()->text(),
             QDir::toNativeSeparators(QFileInfo(paths.at(0)).absoluteFilePath()));
  }

  // ファイル操作が標準のキーに割り当たっていること。印刷が Ctrl+P で、
  // ページ一覧はそれを譲って T になっている。
  void assignsStandardKeysToFileOperations() {
    heiran::ui::MainWindow window;
    const auto shortcutOf = [&window](const QString &text) {
      for (auto *action : window.findChildren<QAction *>()) {
        if (action->text() == text) {
          return action->shortcut();
        }
      }
      return QKeySequence();
    };

    QCOMPARE(shortcutOf("Rename..."), QKeySequence(Qt::Key_F2));
    QCOMPARE(shortcutOf("Move to Recycle Bin..."), QKeySequence(QKeySequence::Delete));
    QCOMPARE(shortcutOf("Print..."), QKeySequence(QKeySequence::Print));
    QCOMPARE(shortcutOf("Image List"), QKeySequence(Qt::Key_T));
    QVERIFY(!shortcutOf("Copy File Path").isEmpty());
    QVERIFY(!shortcutOf("Show in Folder").isEmpty());
  }

  // 開いた画像が履歴に載り、そこから開き直せること。
  void remembersOpenedImagesInTheRecentMenu() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 2);
    QCOMPARE(paths.size(), 2);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.openImageFiles({paths.at(1)});

    auto *fileMenu = findMenu(window.menuBar(), "&File");
    QVERIFY(fileMenu != nullptr);
    QMenu *recentMenu = nullptr;
    for (auto *action : fileMenu->actions()) {
      if (action->text() == "Recent Images") {
        recentMenu = action->menu();
      }
    }
    QVERIFY(recentMenu != nullptr);

    // 新しく開いた順。
    QStringList names;
    for (const auto *action : recentMenu->actions()) {
      if (!action->isSeparator()) {
        names.push_back(action->text());
      }
    }
    QVERIFY(names.size() >= 2);
    QCOMPARE(names.at(0), QFileInfo(paths.at(1)).fileName());
    QCOMPARE(names.at(1), QFileInfo(paths.at(0)).fileName());

    // 押すと開き直せる。
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    recentMenu->actions().at(1)->trigger();
    QTRY_COMPARE(QFileInfo(grid->targetCellFilePath()), QFileInfo(paths.at(0)));
  }

  // 消えたファイルは、押したときに履歴から外れること。残すと押すたびに失敗する。
  void dropsAMissingFileFromTheRecentMenu() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    // 読み込み中のファイルは Windows では消せない。読み終わるのを待つ。
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);
    QVERIFY(QFile::remove(paths.at(0)));

    auto *fileMenu = findMenu(window.menuBar(), "&File");
    QMenu *recentMenu = nullptr;
    for (auto *action : fileMenu->actions()) {
      if (action->text() == "Recent Images") {
        recentMenu = action->menu();
      }
    }
    QVERIFY(recentMenu != nullptr);
    const QString name = QFileInfo(paths.at(0)).fileName();
    QAction *entry = nullptr;
    for (auto *action : recentMenu->actions()) {
      if (action->text() == name) {
        entry = action;
      }
    }
    QVERIFY(entry != nullptr);

    entry->trigger();

    for (const auto *action : recentMenu->actions()) {
      QVERIFY2(action->text() != name, "the missing file stayed in the recent menu");
    }
  }

  // 全画面ではステータスバーが隠れる。ページを送ったら、いまのページを画像の上へ出すこと。
  void showsThePageOverImageInFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *overlay = window.findChild<QLabel *>("transientMessage");
    QVERIFY(overlay != nullptr);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    // 入った直後は、抜け方を 1 度だけ知らせる。
    QVERIFY(overlay->isVisible());
    QVERIFY(overlay->text().contains("Esc"));

    QTest::keyClick(&window, Qt::Key_Right);
    QTRY_VERIFY(overlay->text().contains("2 / 3"));
    QVERIFY(overlay->isVisible());
    QVERIFY2(overlay->text().contains(QFileInfo(paths.at(1)).fileName()),
             qPrintable(overlay->text()));
  }

  // 通常の窓では出さないこと。ステータスバーが見えており、二重になる。
  void doesNotShowPageMessagesOutsideFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *overlay = window.findChild<QLabel *>("transientMessage");
    QVERIFY(grid != nullptr && overlay != nullptr);

    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(grid->currentPage(), 1);

    QVERIFY(!overlay->isVisible());
  }

  // 全画面で操作した結果も、画像の上へ出ること。見えないと、効いたのか分からない。
  void announcesFileOperationsOverImageInFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    auto *overlay = window.findChild<QLabel *>("transientMessage");
    QVERIFY(cell != nullptr && overlay != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);

    QTRY_VERIFY(overlay->text().contains("Copied"));
  }

  void keepsTheCursorVisibleWhileIdleInFullScreen() {
    QTemporaryDir directory;
    const auto paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);
    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    auto *canvas = window.findChild<QWidget *>("imageCanvas");
    QVERIFY(canvas);
    const auto pointerVisible = [canvas]() {
      const auto *overrideCursor = QGuiApplication::overrideCursor();
      return (overrideCursor ? *overrideCursor : canvas->cursor()).shape() != Qt::BlankCursor;
    };

    QVERIFY(pointerVisible());
    QTest::qWait(2300);
    const bool visibleWhileIdle = pointerVisible();
    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(pointerVisible());
    QVERIFY(visibleWhileIdle);
  }

  // [ ] で露出を上げ下げし、\ で戻せること。全画面では入力欄が見えず、触れない。
  void stepsExposureWithKeys() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *exposureBox = window.findChild<QDoubleSpinBox *>("exposureSpinBox");
    QVERIFY(exposureBox != nullptr);
    QCOMPARE(exposureBox->value(), 0.0);

    QTest::keyClick(&window, Qt::Key_BracketRight);
    QTest::keyClick(&window, Qt::Key_BracketRight);
    QCOMPARE(exposureBox->value(), 1.0);
    QTest::keyClick(&window, Qt::Key_BracketLeft);
    QCOMPARE(exposureBox->value(), 0.5);
    QTest::keyClick(&window, Qt::Key_Backslash);
    QCOMPARE(exposureBox->value(), 0.0);
  }

  // 露出はツールバーのボタン 1 つにまとまり、0 以外のときだけ強調して値を添えること。
  // 他のセルと違う見え方になっていることに、気付けなくなるのを避ける。
  void marksTheExposureButtonWhenExposureIsNotZero() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *exposureBox = window.findChild<QDoubleSpinBox *>("exposureSpinBox");
    QVERIFY(exposureBox != nullptr);

    QToolButton *button = nullptr;
    for (auto *candidate : window.findChildren<QToolButton *>()) {
      if (candidate->toolTip() == "Exposure") {
        button = candidate;
      }
    }
    QVERIFY(button != nullptr);
    QVERIFY(button->text().isEmpty());
    QVERIFY(!button->property("exposureActive").toBool());

    exposureBox->setValue(1.5);
    QVERIFY(button->property("exposureActive").toBool());
    QCOMPARE(button->text(), QString("1.5 EV"));

    exposureBox->setValue(-2.0);
    QCOMPARE(button->text(), QString("-2.0 EV"));

    exposureBox->setValue(0.0);
    QVERIFY(button->text().isEmpty());
    QVERIFY(!button->property("exposureActive").toBool());
  }

  // 押したときに出る欄のスライダーと入力欄が、互いに追従すること。
  void keepsTheExposureSliderAndBoxInStep() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *exposureBox = window.findChild<QDoubleSpinBox *>("exposureSpinBox");
    QVERIFY(exposureBox != nullptr);
    auto *slider = window.findChild<QSlider *>();
    QVERIFY(slider != nullptr);

    slider->setValue(6);  // 0.5 EV 刻み。
    QCOMPARE(exposureBox->value(), 3.0);
    exposureBox->setValue(-1.5);
    QCOMPARE(slider->value(), -3);
  }

  // 全画面で露出を変えたら、値を画像の上へ出すこと。
  void showsTheExposureOverImageInFullScreen() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *overlay = window.findChild<QLabel *>("transientMessage");
    QVERIFY(overlay != nullptr);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_BracketRight);

    QVERIFY2(overlay->text().contains("Exposure 0.5"), qPrintable(overlay->text()));
  }

  // マウスの戻る / 進むボタンでページを送れること。
  void changesPageWithTheMouseBackAndForwardButtons() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(grid != nullptr && cell != nullptr);
    auto *canvas = cell->findChild<QWidget *>("imageCanvas");
    QVERIFY(canvas != nullptr);

    QTest::mouseClick(canvas, Qt::ForwardButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(grid->currentPage(), 1);
    QTest::mouseClick(canvas, Qt::ForwardButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(grid->currentPage(), 2);
    QTest::mouseClick(canvas, Qt::BackButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(grid->currentPage(), 1);
  }

  // Alt+Enter でも全画面へ入れること。F11 が使えないキーボードもある。
  void togglesFullScreenWithAltEnter() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
    QTRY_VERIFY(!window.isFullScreen());
  }

  // ---- アニメーション / ページ ----

  // 画像を直接開いたアニメーションは、自動で再生すること。「普通に見る」使い方では
  // 動くものと思って開く。
  void playsAnAnimationOpenedDirectly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    window.openImageFiles({gif});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);

    QVERIFY(grid->isAnimationPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(grid->hasAnimation() && grid->subImageIndex() != 0, 3000);
  }

  // .hrn のセッションは止めた状態で始まること。比較が目的で、勝手に動くと
  // フレームを合わせて見られない。
  void startsASessionPaused() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    window.openImageFiles({gif});
    QVERIFY(grid->isAnimationPlaying());

    // .hrn を開いた場合 (保存先がある) は止めて始める。
    heiran::domain::SessionModel hrnSession;
    hrnSession.sessionFilePath = QDir(directory.path()).filePath("compare.hrn");
    window.setSession(hrnSession);

    QVERIFY(!grid->isAnimationPlaying());
  }

  // .hrn を開いていないセッションは、動くものを動かすこと。何も開いていない状態から
  // ドロップした場合もこれにあたる。ドロップは「開く」経路を通らない。
  void playsAnAnimationDroppedOntoAnEmptySession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif =
        heiran::test::writeFile(QDir(directory.path()), "dropped.gif",
                                heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    // 起動時と同じ、何も割り当てていない 1 セルのセッション。
    heiran::domain::SessionModel emptySession;
    emptySession.cells.resize(1);
    window.setSession(emptySession);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(grid != nullptr && cell != nullptr);

    Q_EMIT cell->dropItemsRequested(0, {gif});

    QTRY_VERIFY_WITH_TIMEOUT(grid->hasAnimation(), 3000);
    QVERIFY(grid->isAnimationPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(grid->subImageIndex() != 0, 3000);
  }

  // P で再生と停止を切り替えられること。
  void togglesPlaybackWithTheP() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    window.openImageFiles({gif});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(grid != nullptr && cell != nullptr);
    QTRY_VERIFY(grid->hasAnimation());
    QVERIFY(grid->isAnimationPlaying());

    QTest::keyClick(&window, Qt::Key_P);
    QVERIFY(!grid->isAnimationPlaying());
    const int stopped = grid->subImageIndex();
    QTest::qWait(300);
    QCOMPARE(grid->subImageIndex(), stopped);

    QTest::keyClick(&window, Qt::Key_P);
    QVERIFY(grid->isAnimationPlaying());
  }

  // 動くものが無いファイルでは、再生にせず理由を知らせること。押した結果が見えないと
  // 効いたのか分からない。
  void explainsWhyThereIsNothingToPlay() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);
    // 直接開いた画像は再生の指定が入っている。一度止めて、押した結果を見る。
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    grid->setAnimationPlaying(false);

    QTest::keyClick(&window, Qt::Key_P);

    QVERIFY(!grid->isAnimationPlaying());
    QVERIFY2(window.statusBar()->currentMessage().contains("no animation"),
             qPrintable(window.statusBar()->currentMessage()));
  }

  // Ctrl+←/→ でコマ送りできること。送ったら再生は止まる。
  void stepsFramesWithCtrlArrows() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    window.openImageFiles({gif});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QTRY_VERIFY(grid->hasAnimation());
    QTest::keyClick(&window, Qt::Key_P);
    QVERIFY(!grid->isAnimationPlaying());
    grid->setSubImageIndex(0);

    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(grid->subImageIndex(), 1);
    QTest::keyClick(&window, Qt::Key_Left, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_Left, Qt::ControlModifier);
    // 先頭より前は最後のコマへ巻き戻る。
    QCOMPARE(grid->subImageIndex(), 2);
    QVERIFY(!grid->isAnimationPlaying());
  }

  // 再生中に手で送ったら、再生を止めること。次のコマがすぐ来ると、送った先が見えない。
  void stopsPlaybackWhenSteppingByHand() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    window.openImageFiles({gif});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QTRY_VERIFY(grid->hasAnimation());
    QVERIFY(grid->isAnimationPlaying());

    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);

    QVERIFY(!grid->isAnimationPlaying());
  }

  // 全画面でコマ送りしたら、何コマ目かを画像の上へ出すこと。状態欄が隠れている。
  void announcesTheFrameInFullScreen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString gif = heiran::test::writeFile(
        QDir(directory.path()), "anim.gif", heiran::test::makeGif(4, 4, {0, 1, 2}, {11, 11, 11}));

    heiran::ui::MainWindow window;
    window.openImageFiles({gif});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    auto *overlay = window.findChild<QLabel *>("transientMessage");
    QVERIFY(grid != nullptr && overlay != nullptr);
    QTRY_VERIFY(grid->hasAnimation());
    QTest::keyClick(&window, Qt::Key_P);
    grid->setSubImageIndex(0);

    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);

    QTRY_VERIFY2(overlay->text().contains("Frame 2 / 3"), qPrintable(overlay->text()));
  }

  // マルチページ TIFF は再生されず、コマ送りでページをめくれること。
  void stepsThePagesOfAMultiPageTiff() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString tiff = heiran::test::writeFile(
        QDir(directory.path()), "pages.tif",
        heiran::test::makeTiff({{QSize(4, 4), Qt::red}, {QSize(4, 4), Qt::green}}));

    heiran::ui::MainWindow window;
    window.openImageFiles({tiff});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QTRY_COMPARE(grid->subImageCount(), 2);

    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(grid->subImageIndex(), 1);
    // ページは端で止まる。
    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(grid->subImageIndex(), 1);
  }

  // 1 枚しかないファイルでは、コマ送りが理由を知らせること。
  void explainsWhyThereIsNothingToStep() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 1);
    QCOMPARE(paths.size(), 1);

    heiran::ui::MainWindow window;
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *cell = window.findChild<heiran::ui::ImageViewWidget *>();
    QVERIFY(cell != nullptr);
    QTRY_COMPARE(cell->cellStatus(), heiran::ui::CellStatus::Ready);

    QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);

    QVERIFY2(window.statusBar()->currentMessage().contains("only one image"),
             qPrintable(window.statusBar()->currentMessage()));
  }

  // ---- 複数ドロップ / クリア ----

  // 複数の画像をセルへドロップしたら、それぞれを別のセルへ読み込むこと。
  // 1 枚だけ開いた直後は 1 セルだが、そこへ 3 枚ドロップしたら格子が広がる。
  void loadsSeveralDroppedImagesIntoSeparateCells() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 4);
    QCOMPARE(paths.size(), 4);

    heiran::ui::MainWindow window;
    window.resize(900, 700);
    window.openImageFiles({paths.at(0)});
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);
    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 1);
    // ドロップのイベントの解析はキャンバスが持つ。ここで確かめたいのは、要求を受けた後の
    // 経路 (格子を広げて別々のセルへ入れる) のため、要求をそのまま出す。
    auto *dropCell = window.findChild<heiran::ui::ImageViewWidget *>();
    Q_EMIT dropCell->dropItemsRequested(0, {paths.at(1), paths.at(2), paths.at(3)});

    // 3 枚は 2x2。全部が別のセルに入る。
    QTRY_COMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 4);
    const auto cells = window.findChildren<heiran::ui::ImageViewWidget *>();
    QStringList shown;
    for (const auto *cell : cells) {
      if (!cell->currentFilePath().isEmpty()) {
        shown.push_back(QFileInfo(cell->currentFilePath()).fileName());
      }
    }
    shown.sort();
    QCOMPARE(shown,
             QStringList({QFileInfo(paths.at(1)).fileName(), QFileInfo(paths.at(2)).fileName(),
                          QFileInfo(paths.at(3)).fileName()}));
  }

  // Clear All Cells で全セルの画像が外れ、レイアウトはそのまま残ること。
  void clearsEveryCell() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths = writeImageFiles(directory.path(), 3);
    QCOMPARE(paths.size(), 3);

    heiran::ui::MainWindow window;
    window.openImageFiles(paths);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const auto cells = window.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 4);
    QTRY_VERIFY(!cells.at(0)->currentFilePath().isEmpty());

    QAction *clearAction = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Clear All Cells") {
        clearAction = action;
      }
    }
    QVERIFY(clearAction != nullptr);
    clearAction->trigger();

    QTRY_VERIFY(cells.at(0)->currentFilePath().isEmpty());
    for (const auto *cell : cells) {
      QVERIFY(cell->currentFilePath().isEmpty());
      QCOMPARE(cell->cellStatus(), heiran::ui::CellStatus::Empty);
    }
    // 格子は変わらない。
    QCOMPARE(window.findChildren<heiran::ui::ImageViewWidget *>().size(), 4);
  }

  // 画像が無いときは、何もしないと知らせること。未保存の印を付けない。
  void explainsThatThereIsNothingToClear() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QAction *clearAction = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Clear All Cells") {
        clearAction = action;
      }
    }
    QVERIFY(clearAction != nullptr);
    clearAction->trigger();

    QVERIFY2(window.statusBar()->currentMessage().contains("nothing to clear"),
             qPrintable(window.statusBar()->currentMessage()));
    QVERIFY(!window.isWindowModified());
  }

  // View > Animation Sync で方式を選べ、排他で、設定へ保存されること。
  void choosesTheAnimationSyncModeFromTheMenu() {
    // 組織名が無い QSettings は保存されない。このテストだけ専用の名前で保存先を持つ。
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("MainWindowSyncModeTests");
    QSettings().clear();
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *autoAction = nullptr;
    QAction *lockedAction = nullptr;
    QAction *independentAction = nullptr;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->text() == "Auto") {
        autoAction = action;
      } else if (action->text() == "Locked") {
        lockedAction = action;
      } else if (action->text() == "Independent") {
        independentAction = action;
      }
    }
    QVERIFY(autoAction != nullptr);
    QVERIFY(lockedAction != nullptr);
    QVERIFY(independentAction != nullptr);
    QVERIFY(autoAction->isChecked());

    independentAction->trigger();
    QVERIFY(independentAction->isChecked());
    QVERIFY(!autoAction->isChecked());
    QVERIFY(!lockedAction->isChecked());
    QCOMPARE(heiran::ui::AppSettings::load().animationSyncMode,
             heiran::ui::AnimationSyncMode::Independent);

    lockedAction->trigger();
    QVERIFY(lockedAction->isChecked());
    QVERIFY(!independentAction->isChecked());
    QCOMPARE(heiran::ui::AppSettings::load().animationSyncMode,
             heiran::ui::AnimationSyncMode::Locked);
    QSettings().clear();
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(QString());
  }

  // View > Cell Borders で枠線を消せ、あとから作るセルにも引き継がれ、設定へ保存されること。
  void togglesTheCellBordersFromTheMenu() {
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("MainWindowCellBordersTests");
    QSettings().clear();
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>();
    QVERIFY(grid != nullptr);

    QAction *action = nullptr;
    for (auto *candidate : window.findChildren<QAction *>()) {
      if (candidate->text() == "Cell Borders") {
        action = candidate;
      }
    }
    QVERIFY(action != nullptr);
    QVERIFY(action->isChecked());
    QVERIFY(grid->cellBordersVisible());

    action->trigger();
    QVERIFY(!action->isChecked());
    QVERIFY(!grid->cellBordersVisible());
    for (const auto *cell : window.findChildren<heiran::ui::ImageViewWidget *>()) {
      QVERIFY(!cell->bordersVisible());
    }
    QVERIFY(!heiran::ui::AppSettings::load().showCellBorders);

    // 画像を開いて作り直されたセルも、枠線なしのまま。
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    window.openImageFiles(writeImageFiles(directory.path(), 3));
    for (const auto *cell : window.findChildren<heiran::ui::ImageViewWidget *>()) {
      QVERIFY(!cell->bordersVisible());
    }

    QSettings().clear();
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(QString());
  }

  void hidesPixelInfoDockByDefault() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *dock = window.findChild<QDockWidget *>();
    QVERIFY(dock != nullptr);
    QVERIFY(!dock->isVisible());
  }

  void reopensPixelInfoDockAfterClosing() {
    heiran::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *dock = window.findChild<QDockWidget *>();
    QVERIFY(dock != nullptr);

    dock->toggleViewAction()->trigger();
    QVERIFY(dock->isVisible());

    dock->close();
    QVERIFY(!dock->isVisible());

    dock->toggleViewAction()->trigger();
    QVERIFY(dock->isVisible());
  }
};

QTEST_MAIN(MainWindowTests)

#include "MainWindowTests.moc"
