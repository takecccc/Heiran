#include "heiran/ui/MainWindow.hpp"

#include "heiran/ui/PixelInfoText.hpp"

#include "heiran/app/SessionPathResolver.hpp"
#include "heiran/domain/SessionPages.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/AboutDialog.hpp"
#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/ChangeGraphWindow.hpp"
#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/ComparisonNavigationController.hpp"
#include "heiran/ui/DiffWindow.hpp"
#include "heiran/ui/FavoritesController.hpp"
#include "heiran/ui/FileOperations.hpp"
#include "heiran/ui/GlobalZoomController.hpp"
#include "heiran/ui/Histogram3DWindow.hpp"
#include "heiran/ui/HistogramWindow.hpp"
#include "heiran/ui/ImageInfoWindow.hpp"
#include "heiran/ui/ImageListPanelController.hpp"
#include "heiran/ui/ImagePrinter.hpp"
#include "heiran/ui/ImageSetManagerDialog.hpp"
#include "heiran/ui/LoupeWidget.hpp"
#include "heiran/ui/ManualLocator.hpp"
#include "heiran/ui/PageListing.hpp"
#include "heiran/ui/PixelInfoWidget.hpp"
#include "heiran/ui/RecentImages.hpp"
#include "heiran/ui/ScopeWindows.hpp"
#include "heiran/ui/SessionIssueDialog.hpp"
#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/SessionMutationController.hpp"
#include "heiran/ui/SettingsDialog.hpp"
#include "heiran/ui/StartupPreparationController.hpp"
#include "heiran/ui/TextureBudgetLedger.hpp"
#include "heiran/ui/TransientMessageWidget.hpp"
#include "heiran/ui/ViewerChromeController.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"
#include "heiran/ui/WelcomeWidget.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>
#include <optional>

namespace heiran::ui {

MainWindow::MainWindow(QWidget *parent, QString favoritesFile)
    : QMainWindow(parent), dirtyStateController_(this), viewerGrid_(new ViewerGridWidget) {
  setWindowTitle(QCoreApplication::translate("MainWindow", "Untitled[*] - Heiran"));
  restoreWindowState();
  setCentralWidget(viewerGrid_);

  messageOverlay_ = new TransientMessageWidget(viewerGrid_);
  welcome_ = new WelcomeWidget(viewerGrid_);
  connect(welcome_, &WelcomeWidget::openImageRequested, this,
          [this]() { openImageFilesFromDialog(); });
  connect(welcome_, &WelcomeWidget::recentImageRequested, this,
          [this](const QString &path) { openRecentImage(path); });
  connect(welcome_, &WelcomeWidget::filesDropped, this,
          [this](const QStringList &paths) { applyDroppedItems(0, paths); });
  cursorIdleTimer_.setSingleShot(true);
  connect(&cursorIdleTimer_, &QTimer::timeout, this, [this]() { hideCursorAfterIdle(); });
  fullScreenChromeTimer_.setSingleShot(true);
  connect(&fullScreenChromeTimer_, &QTimer::timeout, this, [this]() {
    // メニューを開いている間 (ポップアップ) は、引っ込めない。
    if (fullScreenChromeShown_ && QApplication::activePopupWidget() == nullptr) {
      hideFullScreenChrome();
    }
  });
  // マウスの動きはセルの描画面が受ける。ウィンドウ単位では見えないため、アプリ全体で見る。
  qApp->installEventFilter(this);

  connectViewerGrid();
  createPixelInfoDock();
  createFileMenu();
  createImagesMenu();
  createLayoutMenu();
  createViewMenu();
  createAnalysisMenu();
  createHelpMenu();
  createNavigationToolBar();
  createGlobalShortcuts();
  applyInitialState();
  new FavoritesController(this, viewerGrid_, std::move(favoritesFile));
  auto *imageList = new ImageListPanelController(this, viewerGrid_);
  new ComparisonNavigationController(this, viewerGrid_, imageList);
  new GlobalZoomController(this, viewerGrid_);
  connect(imageList, &ImageListPanelController::compareRequested, this,
          &MainWindow::compareSelectedImages);
  addShortcutsToToolTips();
  // 役割が文字から明らかな項目は、アイコンで出す。文字は、ツールチップとメニューに残る。
  applyActionIcons(this);
  for (auto *toolBar : findChildren<QToolBar *>()) {
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->setIconSize(QSize(18, 18));
  }
}

// ビューアからの通知を受ける。
void MainWindow::connectViewerGrid() {
  connect(viewerGrid_, &ViewerGridWidget::statusChanged, this,
          [this](const QString &message) { updateViewerStatus(message); });
  connect(viewerGrid_, &ViewerGridWidget::pixelInfoChanged, this,
          [this](const QStringList &lines) { updatePixelInfoPanel(lines); });
  connect(viewerGrid_, &ViewerGridWidget::pixelPreviewChanged, this,
          [this](const LoupePreview &preview) { updateLoupePanel(preview); });
  connect(viewerGrid_, &ViewerGridWidget::viewChanged, this, [this]() {
    updatePageControls();
    updateWindowTitleForCurrentImage();
    refreshAnalysisWindows();
  });
  connect(viewerGrid_, &ViewerGridWidget::imageContentChanged, this, [this]() {
    updateWindowTitleForCurrentImage();
    refreshAnalysisWindows();
  });
  connect(viewerGrid_, &ViewerGridWidget::activeCellChanged, this,
          [this]() { updateChannelActions(); });
  connect(viewerGrid_, &ViewerGridWidget::persistentStateChanged, this,
          [this]() { dirtyStateController_.setDirty(true); });
  connect(viewerGrid_, &ViewerGridWidget::dropItemsRequested, this,
          [this](int targetCellIndex, const QStringList &paths) {
            applyDroppedItems(targetCellIndex, paths);
          });
  connect(viewerGrid_, &ViewerGridWidget::labelVisibilityRequested, this,
          [this](int cellIndex, bool visible) { applyLabelVisibility(cellIndex, visible); });
  connect(viewerGrid_, &ViewerGridWidget::editLabelRequested, this,
          [this](int cellIndex) { editLabelForCell(cellIndex); });
  connect(viewerGrid_, &ViewerGridWidget::assignImageIndexRequested, this,
          [this](int cellIndex, int imageIndex) { assignImageIndexToCell(cellIndex, imageIndex); });
  connect(viewerGrid_, &ViewerGridWidget::transparencyPatternRequested, this,
          [this](TransparencyPattern pattern) {
            appSettings_.transparencyBackground.pattern = pattern;
            appSettings_.save();
            applyAppSettings();
          });
}

// 画素値情報のドック。既定では畳んでおく。
void MainWindow::createPixelInfoDock() {
  pixelInfoDock_ = new QDockWidget(QCoreApplication::translate("MainWindow", "Pixel Info"), this);
  pixelInfoDock_->setMinimumWidth(420);
  auto *dockContents = new QWidget(pixelInfoDock_);
  auto *dockLayout = new QVBoxLayout(dockContents);
  loupeWidget_ = new LoupeWidget(dockContents);
  pixelInfoTextWidget_ = new PixelInfoWidget(dockContents);
  dockLayout->addWidget(loupeWidget_);
  dockLayout->addWidget(pixelInfoTextWidget_);
  pixelInfoDock_->setWidget(dockContents);
  addDockWidget(Qt::RightDockWidgetArea, pixelInfoDock_);
  // 既定では畳んでおく。普段は画像を広く見たいことが多く、画素値が要るときだけ
  // View メニューから開けばよい。
  pixelInfoDock_->hide();
}

// File メニュー。
void MainWindow::createFileMenu() {
  auto *fileMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&File"));
  auto *openImageAction =
      fileMenu->addAction(QCoreApplication::translate("MainWindow", "Open Image..."));
  openImageAction->setShortcut(QKeySequence::Open);
  connect(openImageAction, &QAction::triggered, this, [this]() { openImageFilesFromDialog(); });

  recentImagesMenu_ = fileMenu->addMenu(QCoreApplication::translate("MainWindow", "Recent Images"));
  recentImages_ = RecentImages::load();
  rebuildRecentImagesMenu();

  // .hrn は比較セッションの保存形式で、普段使いでは開く機会が少ない。
  auto *openAction = fileMenu->addAction(QCoreApplication::translate("MainWindow", "Open .hrn..."));
  openAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
  connect(openAction, &QAction::triggered, this, [this]() {
    if (!openSessionHandler_) {
      return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, QCoreApplication::translate("MainWindow", "Open .hrn"), QString(),
        QCoreApplication::translate("MainWindow", "Heiran Config (*.hrn)"));
    if (filePath.isEmpty()) {
      return;
    }

    if (!dirtyStateController_.confirmContinueWithUnsavedChanges(
            [this]() { return saveCurrentSession(false) && !dirtyStateController_.isDirty(); })) {
      return;
    }

    openSessionHandler_(filePath);
  });

  recentSessionsMenu_ = fileMenu->addMenu(QCoreApplication::translate("MainWindow", "Open Recent"));

  fileMenu->addSeparator();
  auto *settingsAction =
      fileMenu->addAction(QCoreApplication::translate("MainWindow", "Settings..."));
  connect(settingsAction, &QAction::triggered, this, [this]() { openSettingsDialog(); });
  rebuildRecentSessionsMenu();

  auto *saveAction = fileMenu->addAction(QCoreApplication::translate("MainWindow", "Save"));
  saveAction->setShortcut(QKeySequence::Save);
  connect(saveAction, &QAction::triggered, this, [this]() { saveCurrentSession(false); });

  auto *saveAsAction = fileMenu->addAction(QCoreApplication::translate("MainWindow", "Save As..."));
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  connect(saveAsAction, &QAction::triggered, this, [this]() { saveCurrentSession(true); });

  // 表示しているファイルへの操作。メニューが隠れる全画面でも効くよう、ウィンドウにも登録する。
  fileMenu->addSeparator();
  const auto addFileAction = [this, fileMenu](const QString &text, const QKeySequence &shortcut,
                                              std::function<void()> handler) {
    auto *action = fileMenu->addAction(text);
    action->setShortcut(shortcut);
    addAction(action);
    connect(action, &QAction::triggered, this, [handler = std::move(handler)]() { handler(); });
  };
  addFileAction(QCoreApplication::translate("MainWindow", "Rename..."), QKeySequence(Qt::Key_F2),
                [this]() { renameCurrentFile(); });
  addFileAction(QCoreApplication::translate("MainWindow", "Move to Recycle Bin..."),
                QKeySequence::Delete, [this]() { trashCurrentFile(); });
  addFileAction(QCoreApplication::translate("MainWindow", "Copy File Path"),
                QKeySequence("Ctrl+Shift+C"), [this]() { copyCurrentFilePath(); });
  addFileAction(QCoreApplication::translate("MainWindow", "Show in Folder"),
                QKeySequence("Ctrl+Shift+E"),
                [this]() { FileOperations::showInFolder(viewerGrid_->targetCellFilePath()); });
  addFileAction(QCoreApplication::translate("MainWindow", "Print..."), QKeySequence::Print,
                [this]() { printCurrentImage(); });
  addFileAction(QCoreApplication::translate("MainWindow", "Refresh File List"),
                QKeySequence::Refresh, [this]() {
                  refreshFileList();
                  announce(QCoreApplication::translate("MainWindow", "Refreshed the file list."));
                });
}

// Images メニュー。
void MainWindow::createImagesMenu() {
  auto *imagesMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&Images"));
  imagesMenu->setObjectName("imagesMenu");
  // 画像の割り当て、格子、画像セット、並べ替えの変更を、戻す。
  undoAction_ = imagesMenu->addAction(QCoreApplication::translate("MainWindow", "Undo"));
  undoAction_->setShortcut(QKeySequence::Undo);
  addAction(undoAction_);
  connect(undoAction_, &QAction::triggered, this, [this]() { undo(); });
  redoAction_ = imagesMenu->addAction(QCoreApplication::translate("MainWindow", "Redo"));
  redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
  addAction(redoAction_);
  connect(redoAction_, &QAction::triggered, this, [this]() { redo(); });
  updateUndoActions();
  imagesMenu->addSeparator();
  // 全セルの画像を外す。画像セットは残るため、右クリックの Assign Image で選び直せる。
  auto *clearAllCellsAction =
      imagesMenu->addAction(QCoreApplication::translate("MainWindow", "Clear All Cells"));
  connect(clearAllCellsAction, &QAction::triggered, this, [this]() { clearAllCells(); });
  imagesMenu->addSeparator();
  auto *manageImageSetsAction =
      imagesMenu->addAction(QCoreApplication::translate("MainWindow", "Manage Image Sets..."));
  connect(manageImageSetsAction, &QAction::triggered, this, [this]() { manageImageSets(); });
}

// Layout メニュー。
void MainWindow::createLayoutMenu() {
  auto *layoutMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&Layout"));
  layoutActionGroup_ = new QActionGroup(this);
  layoutActionGroup_->setExclusive(true);

  auto addLayoutAction = [this](QMenu *menu, const QString &label, int rows, int cols) {
    auto *action = menu->addAction(label);
    action->setCheckable(true);
    action->setData(QString("%1x%2").arg(rows).arg(cols));
    layoutActionGroup_->addAction(action);
    connect(action, &QAction::triggered, this, [this, rows, cols]() { applyLayout(rows, cols); });
  };

  // セルの数ごとの小メニュー。1 から 16 までの、それぞれの数で、選べる格子 (行 x 列) を並べる。
  for (int cellCount = 1; cellCount <= SessionLayoutController::kMaxCellCount; ++cellCount) {
    auto *countMenu = layoutMenu->addMenu(
        cellCount == 1 ? QCoreApplication::translate("MainWindow", "1 Cell")
                       : QCoreApplication::translate("MainWindow", "%1 Cells").arg(cellCount));
    for (const auto &layout : SessionLayoutController::layoutsForCellCount(cellCount)) {
      addLayoutAction(countMenu, QString("%1x%2").arg(layout.rows).arg(layout.cols), layout.rows,
                      layout.cols);
    }
  }
  layoutMenu->addSeparator();
  auto *customLayoutAction =
      layoutMenu->addAction(QCoreApplication::translate("MainWindow", "Custom..."));
  connect(customLayoutAction, &QAction::triggered, this, [this]() {
    bool ok = false;
    const int rows = QInputDialog::getInt(
        this, QCoreApplication::translate("MainWindow", "Custom Layout"),
        QCoreApplication::translate("MainWindow", "Rows"),
        std::min(currentSession_.viewer.layout.rows, SessionLayoutController::kMaxCellCount), 1,
        SessionLayoutController::kMaxCellCount, 1, &ok);
    if (!ok) {
      return;
    }
    // 列の上限は、セルの数が上限を超えない範囲。
    const int maxCols = SessionLayoutController::kMaxCellCount / rows;

    const int cols = QInputDialog::getInt(
        this, QCoreApplication::translate("MainWindow", "Custom Layout"),
        QCoreApplication::translate("MainWindow", "Columns"),
        std::min(currentSession_.viewer.layout.cols, maxCols), 1, maxCols, 1, &ok);
    if (!ok) {
      return;
    }

    applyLayout(rows, cols);
  });

  layoutMenu->addSeparator();
  auto *fillOrderMenu =
      layoutMenu->addMenu(QCoreApplication::translate("MainWindow", "Fill Order"));
  layoutOrderActionGroup_ = new QActionGroup(this);
  layoutOrderActionGroup_->setExclusive(true);

  auto *rowFirstAction =
      fillOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Row First"));
  rowFirstAction->setCheckable(true);
  rowFirstAction->setData("row-major");
  layoutOrderActionGroup_->addAction(rowFirstAction);
  connect(rowFirstAction, &QAction::triggered, this, [this]() { applyLayoutOrder("row-major"); });

  auto *columnFirstAction =
      fillOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Column First"));
  columnFirstAction->setCheckable(true);
  columnFirstAction->setData("col-major");
  layoutOrderActionGroup_->addAction(columnFirstAction);
  connect(columnFirstAction, &QAction::triggered, this,
          [this]() { applyLayoutOrder("col-major"); });

  auto *imageOrderMenu =
      layoutMenu->addMenu(QCoreApplication::translate("MainWindow", "Image Order"));
  auto *customImageOrderAction =
      imageOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Custom..."));
  connect(customImageOrderAction, &QAction::triggered, this, [this]() { applyCustomImageOrder(); });
  imageOrderMenu->addSeparator();
  auto *ascendingImagesAction =
      imageOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Ascending"));
  connect(ascendingImagesAction, &QAction::triggered, this,
          [this]() { applyImageReorder(&SessionLayoutController::reorderImagesAscending); });
  auto *reverseImagesAction =
      imageOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Reverse"));
  connect(reverseImagesAction, &QAction::triggered, this,
          [this]() { applyImageReorder(&SessionLayoutController::reorderImagesReverse); });
  auto *rotateLeftImagesAction =
      imageOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate Left"));
  connect(rotateLeftImagesAction, &QAction::triggered, this,
          [this]() { applyImageReorder(&SessionLayoutController::reorderImagesRotateLeft); });
  auto *rotateRightImagesAction =
      imageOrderMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate Right"));
  connect(rotateRightImagesAction, &QAction::triggered, this,
          [this]() { applyImageReorder(&SessionLayoutController::reorderImagesRotateRight); });
}

// View メニュー。ドックの再表示もここから行うため、ドックの後に呼ぶ。
void MainWindow::createViewMenu() {
  auto *viewMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&View"));
  viewMenu->setObjectName("viewMenu");
  auto *channelMenu = viewMenu->addMenu(QCoreApplication::translate("MainWindow", "Channel"));
  channelActionGroup_ = new QActionGroup(this);
  channelActionGroup_->setExclusive(true);

  auto addChannelAction = [this, channelMenu](const QString &label, ChannelMode mode,
                                              QAction **actionSlot) {
    auto *action = channelMenu->addAction(label);
    action->setCheckable(true);
    channelActionGroup_->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode]() { applyChannelMode(mode); });
    *actionSlot = action;
  };

  addChannelAction(QCoreApplication::translate("MainWindow", "Color"), ChannelMode::Color,
                   &channelColorAction_);
  addChannelAction(QCoreApplication::translate("MainWindow", "Gray"), ChannelMode::Gray,
                   &channelGrayAction_);
  addChannelAction(QCoreApplication::translate("MainWindow", "Red"), ChannelMode::Red,
                   &channelRedAction_);
  addChannelAction(QCoreApplication::translate("MainWindow", "Green"), ChannelMode::Green,
                   &channelGreenAction_);
  addChannelAction(QCoreApplication::translate("MainWindow", "Blue"), ChannelMode::Blue,
                   &channelBlueAction_);
  addChannelAction(QCoreApplication::translate("MainWindow", "Alpha"), ChannelMode::Alpha,
                   &channelAlphaAction_);

  auto *pixelValueMenu =
      viewMenu->addMenu(QCoreApplication::translate("MainWindow", "Pixel Value Unit"));
  pixelValueFormatActionGroup_ = new QActionGroup(this);
  pixelValueFormatActionGroup_->setExclusive(true);

  auto addPixelValueFormatAction = [this, pixelValueMenu](const QString &label,
                                                          PixelValueFormat format,
                                                          QAction **actionSlot) {
    auto *action = pixelValueMenu->addAction(label);
    action->setCheckable(true);
    pixelValueFormatActionGroup_->addAction(action);
    connect(action, &QAction::triggered, this, [this, format]() { applyPixelValueFormat(format); });
    *actionSlot = action;
  };

  addPixelValueFormatAction("0 - 255", PixelValueFormat::Normalized255,
                            &pixelValueFormat255Action_);
  addPixelValueFormatAction("0.0 - 1.0", PixelValueFormat::Normalized01,
                            &pixelValueFormat01Action_);

  // メニューとツールバーで同じ QAction を共有する。状態がずれない。
  fitOnPageChangeAction_ =
      new QAction(QCoreApplication::translate("MainWindow", "Fit on Page Change"), this);
  fitOnPageChangeAction_->setCheckable(true);
  fitOnPageChangeAction_->setShortcut(QKeySequence("Shift+F"));
  fitOnPageChangeAction_->setToolTip(QCoreApplication::translate(
      "MainWindow", "Fit the images into their cells whenever the page changes. (Shift+F)"));
  viewMenu->addAction(fitOnPageChangeAction_);
  cellBordersAction_ =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Cell Borders"));
  cellBordersAction_->setCheckable(true);
  connect(cellBordersAction_, &QAction::triggered, this,
          [this](bool visible) { applyCellBorders(visible); });
  connect(fitOnPageChangeAction_, &QAction::triggered, this,
          [this](bool checked) { applyFitOnPageChange(checked); });

  // 共有カーソルの描き方。縁付きの固定色と、画素の色から離れた色を比べられる。
  auto *cursorMenu = viewMenu->addMenu(QCoreApplication::translate("MainWindow", "Cursor"));
  cursorStyleActionGroup_ = new QActionGroup(this);
  cursorStyleActionGroup_->setExclusive(true);
  const auto addCursorStyleAction = [this, cursorMenu](const QString &label, CursorStyle style) {
    auto *action = cursorMenu->addAction(label);
    action->setCheckable(true);
    action->setData(static_cast<int>(style));
    cursorStyleActionGroup_->addAction(action);
    connect(action, &QAction::triggered, this, [this, style]() { applyCursorStyle(style); });
  };
  addCursorStyleAction(QCoreApplication::translate("MainWindow", "Outlined Color"),
                       CursorStyle::Outline);
  addCursorStyleAction(QCoreApplication::translate("MainWindow", "Contrast Color"),
                       CursorStyle::Contrast);

  invertAllAction_ =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Invert All Cells"));
  invertAllAction_->setCheckable(true);
  connect(invertAllAction_, &QAction::triggered, this,
          [this](bool checked) { viewerGrid_->setInvertForAllCells(checked); });

  viewMenu->addSeparator();
  // 画像を見るときの基本操作。ウィンドウにも登録し、メニューが隠れる全画面でも効かせる。
  auto *rotateRightAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate Right"));
  rotateRightAction->setShortcut(QKeySequence(Qt::Key_R));
  addAction(rotateRightAction);
  connect(rotateRightAction, &QAction::triggered, this,
          [this]() { viewerGrid_->rotateActiveCellBy(1); });

  auto *rotateLeftAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate Left"));
  rotateLeftAction->setShortcut(QKeySequence("Shift+R"));
  addAction(rotateLeftAction);
  connect(rotateLeftAction, &QAction::triggered, this,
          [this]() { viewerGrid_->rotateActiveCellBy(-1); });

  auto *rotateAllLeftAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate All Cells Left"));
  rotateAllLeftAction->setObjectName("rotateAllLeftAction");
  connect(rotateAllLeftAction, &QAction::triggered, this,
          [this]() { viewerGrid_->rotateAllCellsBy(-1); });
  auto *rotateAllRightAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Rotate All Cells Right"));
  rotateAllRightAction->setObjectName("rotateAllRightAction");
  connect(rotateAllRightAction, &QAction::triggered, this,
          [this]() { viewerGrid_->rotateAllCellsBy(1); });

  // ファイルの中の画像。アニメーション (GIF / WebP) は再生し、ページ (マルチページ TIFF) は
  // 手でめくる。ファイルの連番 (Prev / Next) とは別の軸で、キーも分ける。
  viewMenu->addSeparator();
  playAnimationAction_ =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Play Animation"));
  playAnimationAction_->setCheckable(true);
  playAnimationAction_->setShortcut(QKeySequence(Qt::Key_P));
  addAction(playAnimationAction_);
  connect(playAnimationAction_, &QAction::triggered, this, [this]() { toggleAnimationPlayback(); });

  // 複数のセルでアニメーションを動かすときの進め方。コマ送りは、どれでも全セルをそろえる。
  auto *animationSyncMenu =
      viewMenu->addMenu(QCoreApplication::translate("MainWindow", "Animation Sync"));
  auto *animationSyncGroup = new QActionGroup(this);
  animationSyncGroup->setExclusive(true);
  auto addAnimationSyncAction = [this, animationSyncMenu,
                                 animationSyncGroup](const QString &label, const QString &tip,
                                                     AnimationSyncMode mode, QAction **actionSlot) {
    auto *action = animationSyncMenu->addAction(label);
    action->setCheckable(true);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    animationSyncGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode]() { applyAnimationSyncMode(mode); });
    *actionSlot = action;
  };
  addAnimationSyncAction(QCoreApplication::translate("MainWindow", "Auto"),
                         QCoreApplication::translate(
                             "MainWindow",
                             "Sync the cells when the animations have the same number of frames; "
                             "otherwise play each at its own speed."),
                         AnimationSyncMode::Auto, &animationSyncAutoAction_);
  addAnimationSyncAction(
      QCoreApplication::translate("MainWindow", "Locked"),
      QCoreApplication::translate("MainWindow", "Always keep every cell on the same frame."),
      AnimationSyncMode::Locked, &animationSyncLockedAction_);
  addAnimationSyncAction(
      QCoreApplication::translate("MainWindow", "Independent"),
      QCoreApplication::translate("MainWindow", "Always play each cell at its own speed."),
      AnimationSyncMode::Independent, &animationSyncIndependentAction_);

  // 単独表示: 1
  // つのセルを、画面いっぱいに見る。切り替えても、同じ場所を同じ倍率で見続けられるため、A/B
  // の比較になる。
  viewMenu->addSeparator();
  auto *nextCellAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Next Cell"));
  nextCellAction->setShortcut(QKeySequence(Qt::Key_Tab));
  addAction(nextCellAction);
  connect(nextCellAction, &QAction::triggered, this, [this]() { viewerGrid_->cycleSoloCell(1); });
  auto *previousCellAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Previous Cell"));
  previousCellAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Tab));
  addAction(previousCellAction);
  connect(previousCellAction, &QAction::triggered, this,
          [this]() { viewerGrid_->cycleSoloCell(-1); });
  auto *singleCellMenu =
      viewMenu->addMenu(QCoreApplication::translate("MainWindow", "Single Cell"));
  soloCellActionGroup_ = new QActionGroup(this);
  soloCellActionGroup_->setExclusive(true);
  auto *allCellsAction =
      singleCellMenu->addAction(QCoreApplication::translate("MainWindow", "All Cells"));
  allCellsAction->setCheckable(true);
  allCellsAction->setChecked(true);
  allCellsAction->setData(-1);
  allCellsAction->setShortcut(QKeySequence("Alt+0"));
  soloCellActionGroup_->addAction(allCellsAction);
  addAction(allCellsAction);
  connect(allCellsAction, &QAction::triggered, this, [this]() { viewerGrid_->setSoloCell(-1); });
  for (int cellIndex = 0; cellIndex < 9; ++cellIndex) {
    auto *action = singleCellMenu->addAction(
        QCoreApplication::translate("MainWindow", "Cell %1").arg(cellIndex + 1));
    action->setCheckable(true);
    action->setData(cellIndex);
    action->setShortcut(QKeySequence(QString("Alt+%1").arg(cellIndex + 1)));
    soloCellActionGroup_->addAction(action);
    addAction(action);
    connect(action, &QAction::triggered, this,
            [this, cellIndex]() { viewerGrid_->setSoloCell(cellIndex); });
  }
  connect(viewerGrid_, &ViewerGridWidget::soloCellChanged, this, [this](int cellIndex) {
    updateSoloActions();
    if (cellIndex >= 0) {
      messageOverlay_->showMessage(
          QCoreApplication::translate("MainWindow", "Cell %1 of %2  (Tab: next, Esc: all cells)")
              .arg(cellIndex + 1)
              .arg(currentSession_.viewer.layout.rows * currentSession_.viewer.layout.cols),
          2000);
    } else {
      messageOverlay_->showMessage(QCoreApplication::translate("MainWindow", "All cells"), 1200);
    }
  });
  viewMenu->addSeparator();

  auto *previousFrameAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Previous Frame"));
  previousFrameAction->setShortcut(QKeySequence("Ctrl+Left"));
  addAction(previousFrameAction);
  connect(previousFrameAction, &QAction::triggered, this, [this]() { stepSubImage(-1); });

  auto *nextFrameAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Next Frame"));
  nextFrameAction->setShortcut(QKeySequence("Ctrl+Right"));
  addAction(nextFrameAction);
  connect(nextFrameAction, &QAction::triggered, this, [this]() { stepSubImage(1); });
  viewMenu->addSeparator();

  // 露出は全画面では入力欄が見えず、触れない。キーで上げ下げする。
  auto *darkerAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Decrease Exposure"));
  darkerAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
  addAction(darkerAction);
  connect(darkerAction, &QAction::triggered, this, [this]() { stepExposure(-0.5); });

  auto *brighterAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Increase Exposure"));
  brighterAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
  addAction(brighterAction);
  connect(brighterAction, &QAction::triggered, this, [this]() { stepExposure(0.5); });

  // メニューにも出す (キー \ だけでは、見つけられない)。
  auto *resetExposureAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Reset Exposure"));
  resetExposureAction->setShortcut(QKeySequence(Qt::Key_Backslash));
  addAction(resetExposureAction);
  connect(resetExposureAction, &QAction::triggered, this, [this]() {
    if (exposureSpinBox_ != nullptr) {
      exposureSpinBox_->setValue(0.0);
    }
  });

  auto *copyImageAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Copy Image"));
  copyImageAction->setShortcut(QKeySequence::Copy);
  addAction(copyImageAction);
  connect(copyImageAction, &QAction::triggered, this,
          [this]() { viewerGrid_->copyActiveCellImage(); });

  // 画像の情報 (ファイル、画素の形式、色、EXIF)。セルを列にして並べる。
  auto *imageInfoAction =
      viewMenu->addAction(QCoreApplication::translate("MainWindow", "Image Info..."));
  imageInfoAction->setShortcut(QKeySequence(Qt::Key_I));
  addAction(imageInfoAction);
  connect(imageInfoAction, &QAction::triggered, this, [this]() {
    if (imageInfoWindow_ == nullptr) {
      imageInfoWindow_ = new ImageInfoWindow(this);
    }
    imageInfoWindow_->showAndRaise();
    refreshAnalysisWindows();
  });

  // ドックは閉じるボタンを持つため、再表示する導線をメニューに用意する。
  viewMenu->addSeparator();
  viewMenu->addAction(pixelInfoDock_->toggleViewAction());
}

// マニュアルを、既定のブラウザで開く。見つからなければ、作り方を知らせる。
void MainWindow::openManual() {
  const QString index = findManualIndex(QCoreApplication::applicationDirPath());
  if (index.isEmpty()) {
    QMessageBox::information(
        this, QCoreApplication::translate("MainWindow", "Manual"),
        QCoreApplication::translate(
            "MainWindow", "The manual was not found. It is installed with Heiran. "
                          "In a development build, create it with scripts/build-manual.ps1."));
    return;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(index))) {
    statusBar()->showMessage(
        QCoreApplication::translate("MainWindow", "Could not open the manual: ") + index, 5000);
  }
}

// Help メニュー。マニュアルと、ライセンスの表記は、ここから見られる。
void MainWindow::createHelpMenu() {
  auto *helpMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&Help"));
  auto *manualAction = helpMenu->addAction(QCoreApplication::translate("MainWindow", "Manual"));
  manualAction->setShortcut(QKeySequence::HelpContents);
  connect(manualAction, &QAction::triggered, this, [this]() { openManual(); });
  helpMenu->addSeparator();
  auto *aboutAction =
      helpMenu->addAction(QCoreApplication::translate("MainWindow", "About Heiran..."));
  connect(aboutAction, &QAction::triggered, this, [this]() {
    AboutDialog dialog(this);
    dialog.exec();
  });
  // Qt は LGPL。標準の「Qt について」も出す。
  auto *aboutQtAction =
      helpMenu->addAction(QCoreApplication::translate("MainWindow", "About Qt..."));
  connect(aboutQtAction, &QAction::triggered, this, []() { QApplication::aboutQt(); });
}

// Analysis メニュー。
void MainWindow::createAnalysisMenu() {
  auto *analysisMenu = menuBar()->addMenu(QCoreApplication::translate("MainWindow", "&Analysis"));
  auto *histogramAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Histogram"));
  connect(histogramAction, &QAction::triggered, this, [this]() {
    if (histogramWindow_ == nullptr) {
      histogramWindow_ = new HistogramWindow(this);
    }
    histogramWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
  auto *histogram3DAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Histogram 3D"));
  connect(histogram3DAction, &QAction::triggered, this, [this]() {
    if (histogram3DWindow_ == nullptr) {
      histogram3DWindow_ = new Histogram3DWindow(this);
    }
    histogram3DWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
  // 波形モニタとベクトルスコープ: 信号の見え方 (位置ごとの値、色の分布) を、セルごとに見る。
  auto *waveformAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Waveform"));
  connect(waveformAction, &QAction::triggered, this, [this]() {
    if (waveformWindow_ == nullptr) {
      waveformWindow_ = new WaveformWindow(this);
    }
    waveformWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
  auto *vectorscopeAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Vectorscope"));
  connect(vectorscopeAction, &QAction::triggered, this, [this]() {
    if (vectorscopeWindow_ == nullptr) {
      vectorscopeWindow_ = new VectorscopeWindow(this);
    }
    vectorscopeWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
  // 差分は、セルの 1 つとして並べて見せる。基準は操作対象のセル。空きセルが無ければ、格子を広げる。
  auto *diffCellAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Diff Cell"));
  connect(diffCellAction, &QAction::triggered, this, [this]() { addDifferenceCell(); });
  // 2 枚を、動かせる境目で、つなぎ合わせて見る (ワイプ)。
  auto *wipeCellAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Wipe Cell"));
  connect(wipeCellAction, &QAction::triggered, this,
          [this]() { addDifferenceCell(heiran::domain::DiffMode::Wipe); });
  auto *diffAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Diff Window"));
  connect(diffAction, &QAction::triggered, this, [this]() {
    if (diffWindow_ == nullptr) {
      diffWindow_ = new DiffWindow(this);
    }
    diffWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
  auto *changeGraphAction =
      analysisMenu->addAction(QCoreApplication::translate("MainWindow", "Change Graph"));
  connect(changeGraphAction, &QAction::triggered, this, [this]() {
    if (changeGraphWindow_ == nullptr) {
      changeGraphWindow_ = new ChangeGraphWindow(this);
    }
    changeGraphWindow_->showAndRaise();
    refreshAnalysisWindows();
  });
}

// ページ送りとズームのツールバー。
void MainWindow::createNavigationToolBar() {
  auto *navigationToolBar = addToolBar(QCoreApplication::translate("MainWindow", "Navigation"));
  navigationToolBar->setObjectName("navigationToolBar");
  // 位置は固定。つまみの点々が、画像を見る邪魔になる。
  navigationToolBar->setMovable(false);

  auto *previousPageAction =
      navigationToolBar->addAction(QCoreApplication::translate("MainWindow", "Prev"));
  previousPageAction->setToolTip(QCoreApplication::translate("MainWindow", "Previous page"));
  previousPageAction->setShortcuts(
      {QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp), QKeySequence(Qt::Key_Backspace)});
  connect(previousPageAction, &QAction::triggered, this, [this]() { viewerGrid_->previousPage(); });
  // ツールバーのアクションはツールバーが隠れるとショートカットが効かなくなる。全画面で
  // ツールバーを隠しても操作できるよう、キーの付いたアクションはウィンドウにも登録する。
  addAction(previousPageAction);

  auto *nextPageAction =
      navigationToolBar->addAction(QCoreApplication::translate("MainWindow", "Next"));
  nextPageAction->setToolTip(QCoreApplication::translate("MainWindow", "Next page"));
  nextPageAction->setShortcuts(
      {QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown), QKeySequence(Qt::Key_Space)});
  connect(nextPageAction, &QAction::triggered, this, [this]() { viewerGrid_->nextPage(); });
  addAction(nextPageAction);

  // 連番を見るときの定番操作。ツールバーには出さず、キーだけ受ける。
  auto *firstPageAction =
      new QAction(QCoreApplication::translate("MainWindow", "First Page"), this);
  firstPageAction->setShortcut(QKeySequence(Qt::Key_Home));
  addAction(firstPageAction);
  connect(firstPageAction, &QAction::triggered, this, [this]() { viewerGrid_->setCurrentPage(0); });

  auto *lastPageAction = new QAction(QCoreApplication::translate("MainWindow", "Last Page"), this);
  lastPageAction->setShortcut(QKeySequence(Qt::Key_End));
  addAction(lastPageAction);
  connect(lastPageAction, &QAction::triggered, this,
          [this]() { viewerGrid_->setCurrentPage(viewerGrid_->totalPages() - 1); });

  navigationToolBar->addSeparator();
  navigationToolBar->addWidget(
      new QLabel(QCoreApplication::translate("MainWindow", "Page"), navigationToolBar));
  pageComboBox_ = new QComboBox(navigationToolBar);
  pageComboBox_->setObjectName("pageSelector");
  pageComboBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  // フォーカスを持たせない。持つと左右キーがページ送りではなく
  // 項目の選択に使われ、ビューアの操作として期待と食い違う。
  pageComboBox_->setFocusPolicy(Qt::NoFocus);
  connect(pageComboBox_, &QComboBox::currentIndexChanged, this, [this](int pageIndex) {
    if (pageIndex < 0) {
      return;
    }
    viewerGrid_->setCurrentPage(pageComboBox_->itemData(pageIndex).toInt());
  });
  navigationToolBar->addWidget(pageComboBox_);

  // 露出は、float 画像 (EXR など) を見るときだけ使う。普段は場所を取らないよう、アイコン 1 つに
  // まとめ、押したときに調整の欄を出す。0 以外のときは、強調して値を添える (他のセルと違う
  // 見え方になっていることに、気付けなくなるのを避ける)。キーとメニューからも変えられる。
  navigationToolBar->addSeparator();
  exposureSpinBox_ = new QDoubleSpinBox(this);
  auto *exposureSpinBox = exposureSpinBox_;
  exposureSpinBox->setObjectName("exposureSpinBox");
  exposureSpinBox->setRange(-10.0, 10.0);
  exposureSpinBox->setSingleStep(0.5);
  exposureSpinBox->setDecimals(2);
  exposureSpinBox->setValue(0.0);
  exposureSpinBox->setSuffix(QCoreApplication::translate("MainWindow", " EV"));
  exposureSpinBox->setFocusPolicy(Qt::ClickFocus);
  exposureSpinBox->setToolTip(QCoreApplication::translate(
      "MainWindow",
      "Exposure in stops. The displayed value is multiplied by 2^EV before clipping."));

  auto *exposureSlider = new QSlider(Qt::Horizontal, this);
  exposureSlider->setRange(-20, 20);  // 0.5 EV 刻み。
  exposureSlider->setMinimumWidth(160);
  exposureSlider->setFocusPolicy(Qt::ClickFocus);
  exposureSlider->setToolTip(exposureSpinBox->toolTip());

  auto *exposureResetButton = new QToolButton(this);
  exposureResetButton->setIcon(themedIcon("rotate-ccw"));
  exposureResetButton->setToolTip(
      QCoreApplication::translate("MainWindow", "Reset exposure to 0 EV (\\)"));
  exposureResetButton->setFocusPolicy(Qt::NoFocus);

  auto *exposurePanel = new QWidget(this);
  auto *exposureLayout = new QHBoxLayout(exposurePanel);
  exposureLayout->setContentsMargins(10, 8, 10, 8);
  exposureLayout->setSpacing(8);
  exposureLayout->addWidget(exposureSlider);
  exposureLayout->addWidget(exposureSpinBox);
  exposureLayout->addWidget(exposureResetButton);

  auto *exposureMenu = new QMenu(this);
  auto *exposureWidgetAction = new QWidgetAction(exposureMenu);
  exposureWidgetAction->setDefaultWidget(exposurePanel);
  exposureMenu->addAction(exposureWidgetAction);

  exposureButton_ = new QToolButton(navigationToolBar);
  exposureButton_->setIcon(themedIcon("sun"));
  exposureButton_->setMenu(exposureMenu);
  exposureButton_->setPopupMode(QToolButton::InstantPopup);
  exposureButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  exposureButton_->setFocusPolicy(Qt::NoFocus);
  exposureButton_->setToolTip(QCoreApplication::translate("MainWindow", "Exposure"));
  exposureButton_->setProperty("exposureActive", false);
  navigationToolBar->addWidget(exposureButton_);

  auto *exposureButton = exposureButton_;
  connect(
      exposureSpinBox, &QDoubleSpinBox::valueChanged, this,
      [this, exposureSlider, exposureButton](double stops) {
        viewerGrid_->setExposureStops(stops);
        {
          const QSignalBlocker blocker(exposureSlider);
          exposureSlider->setValue(static_cast<int>(std::lround(stops * 2.0)));
        }
        // 0 以外のときは、ボタンを強調して値を添える。
        const bool active = std::abs(stops) > 1e-9;
        exposureButton->setText(
            active ? QCoreApplication::translate("MainWindow", "%1 EV").arg(stops, 0, 'f', 1)
                   : QString());
        exposureButton->setProperty("exposureActive", active);
        exposureButton->style()->unpolish(exposureButton);
        exposureButton->style()->polish(exposureButton);
        // 全画面では入力欄が見えない。変えた値を画像の上へ出す。
        if (isFullScreen()) {
          messageOverlay_->showMessage(
              QCoreApplication::translate("MainWindow", "Exposure %1 EV").arg(stops, 0, 'f', 1));
        }
      });
  connect(exposureSlider, &QSlider::valueChanged, this,
          [exposureSpinBox](int steps) { exposureSpinBox->setValue(steps / 2.0); });
  connect(exposureResetButton, &QToolButton::clicked, this,
          [exposureSpinBox]() { exposureSpinBox->setValue(0.0); });

  auto *fitAction = navigationToolBar->addAction(QCoreApplication::translate("MainWindow", "Fit"));
  fitAction->setToolTip(
      QCoreApplication::translate("MainWindow", "Fit the images into their cells."));
  fitAction->setShortcut(QKeySequence(Qt::Key_F));
  connect(fitAction, &QAction::triggered, this, [this]() { viewerGrid_->requestFitToCells(); });
  addAction(fitAction);

  navigationToolBar->addAction(fitOnPageChangeAction_);

  auto *actualSizeAction = navigationToolBar->addAction("1:1");
  actualSizeAction->setObjectName("actualSizeAction");
  actualSizeAction->setToolTip(QCoreApplication::translate("MainWindow", "Show at 100%."));
  actualSizeAction->setShortcut(QKeySequence(Qt::Key_1));
  connect(actualSizeAction, &QAction::triggered, this, [this]() { viewerGrid_->setActualSize(); });
  addAction(actualSizeAction);

  auto *resetViewAction =
      navigationToolBar->addAction(QCoreApplication::translate("MainWindow", "Reset View"));
  resetViewAction->setToolTip(
      QCoreApplication::translate("MainWindow", "Reset the view (pan and zoom)"));
  resetViewAction->setShortcut(QKeySequence(Qt::Key_0));
  connect(resetViewAction, &QAction::triggered, this, [this]() { viewerGrid_->resetView(); });
  addAction(resetViewAction);
  navigationToolBar->addSeparator();
  navigationToolBar->addAction(findChild<QAction *>("rotateAllLeftAction"));
  navigationToolBar->addAction(findChild<QAction *>("rotateAllRightAction"));
  viewerChrome_ = new ViewerChromeController(this, viewerGrid_, navigationToolBar, pageComboBox_,
                                             previousPageAction, nextPageAction);
}

// メニューに出さないキー操作。ズーム、全画面、選択解除。
void MainWindow::createGlobalShortcuts() {
  // ホイールと同じ倍率で拡大縮小する。マウスを使わない操作用。
  constexpr double kKeyboardZoomStep = 1.1;
  auto *zoomInAction = new QAction(QCoreApplication::translate("MainWindow", "Zoom In"), this);
  zoomInAction->setShortcuts(
      {QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal), QKeySequence(Qt::Key_Period)});
  addAction(zoomInAction);
  connect(zoomInAction, &QAction::triggered, this,
          [this]() { viewerGrid_->zoomBy(kKeyboardZoomStep); });

  auto *zoomOutAction = new QAction(QCoreApplication::translate("MainWindow", "Zoom Out"), this);
  zoomOutAction->setShortcuts({QKeySequence(Qt::Key_Minus), QKeySequence(Qt::Key_Comma)});
  addAction(zoomOutAction);
  connect(zoomOutAction, &QAction::triggered, this,
          [this]() { viewerGrid_->zoomBy(1.0 / kKeyboardZoomStep); });

  auto *fullScreenAction =
      new QAction(QCoreApplication::translate("MainWindow", "Full Screen"), this);
  fullScreenAction->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence("Alt+Return")});
  addAction(fullScreenAction);
  connect(fullScreenAction, &QAction::triggered, this, [this]() { toggleFullScreen(); });

  clearSelectionAction_ =
      new QAction(QCoreApplication::translate("MainWindow", "Clear Selection"), this);
  clearSelectionAction_->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(clearSelectionAction_);
  connect(clearSelectionAction_, &QAction::triggered, this, [this]() {
    // 全画面中は抜ける方を優先する。抜け方が分からなくなるのを避ける。
    if (isFullScreen()) {
      toggleFullScreen();
      return;
    }
    // 選択範囲があれば、それを解除する。無ければ、単独表示から、全セルの表示へ戻る。
    if (!viewerGrid_->hasSelection() && viewerGrid_->soloCellIndex() >= 0) {
      viewerGrid_->setSoloCell(-1);
      return;
    }
    viewerGrid_->clearSelection();
  });

  // 起動直後の矢印キーがページ送りとして効くようにする。
  viewerGrid_->setFocus();
  statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Ready"));
}

// メニューとツールバーが揃った後の初期化。
// 表示状態の反映はアクションが出来ていることが前提になる。
void MainWindow::applyInitialState() {
  updateLayoutActions();
  updateChannelActions();
  updateInvertAction();
  appSettings_ = AppSettings::load();
  applyAppSettings();
  updateWelcomeVisibility();

  // 起動直後の矢印キーがページ送りとして効くようにする。
  viewerGrid_->setFocus();
  statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Ready"));
}

void MainWindow::setSession(const heiran::domain::SessionModel &session, int initialPage,
                            std::shared_ptr<DecodeScheduler> startupScheduler) {
  // 別のセッションを開いたら、前の変更は、戻せない。
  clearUndoHistory();
  // 前のセッションが直接開いたものでも、次へは持ち越さない。
  fitOnPageChangeOverride_.reset();
  updateFitOnPageChange();
  currentSession_ = session;
  cellLayoutCache_ = session.cells;
  // .hrn は比較が目的で、勝手に動くとフレームを合わせて見られないため、止めて始める。
  // .hrn を開いていない (保存先が無い) セッションは、画像を直接開いた、またはドロップした
  // 「普通に見る」使い方で、動くものは動かす。ドロップは開く側を通らないため、ここで決める。
  viewerGrid_->setAnimationPlaying(session.sessionFilePath.isEmpty());
  viewerGrid_->setSession(session, initialPage, std::move(startupScheduler));
  updateAnimationActions();
  dirtyStateController_.setSessionFilePath(currentSession_.sessionFilePath);
  dirtyStateController_.setDirty(false);
  refreshSessionDependentUi();
  updateWindowTitleForCurrentImage();
}

void MainWindow::reportSessionIssues(const SessionIssueReport &report) {
  if (report.failure.isEmpty() && report.issues.isEmpty()) {
    return;
  }

  if (showSessionIssueDialog(this, report) != SessionIssueChoice::OpenAnother) {
    return;
  }

  const QString filePath = QFileDialog::getOpenFileName(
      this, QCoreApplication::translate("MainWindow", "Open .hrn"), QString(),
      QCoreApplication::translate("MainWindow", "Heiran session (*.hrn);;All files (*)"));
  if (!filePath.isEmpty() && openSessionHandler_) {
    openSessionHandler_(filePath);
  }
}

void MainWindow::setStartupMessage(const QString &message) {
  statusBar()->showMessage(message);
}

void MainWindow::setOpenSessionHandler(std::function<void(const QString &)> handler) {
  openSessionHandler_ = std::move(handler);
}

void MainWindow::setSaveSessionHandler(
    std::function<void(const heiran::domain::SessionModel &, const QString &)> handler) {
  saveSessionHandler_ = std::move(handler);
}

void MainWindow::setRecentSessions(const QStringList &recentSessions) {
  recentSessions_ = recentSessions;
  rebuildRecentSessionsMenu();
}

void MainWindow::openImageFilesFromDialog() {
  const QStringList filePaths = QFileDialog::getOpenFileNames(
      this, QCoreApplication::translate("MainWindow", "Open Image"), QString(),
      QCoreApplication::translate("MainWindow",
                                  "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.avif *.jxl "
                                  "*.tif *.tiff *.exr *.hdr *.dpx *.psd "
                                  "*.tga);;"
                                  "All Files (*)"));
  if (filePaths.isEmpty()) {
    return;
  }

  if (!dirtyStateController_.confirmContinueWithUnsavedChanges(
          [this]() { return saveCurrentSession(false) && !dirtyStateController_.isDirty(); })) {
    return;
  }

  openImageFiles(filePaths);
}

void MainWindow::toggleFullScreen() {
  if (isFullScreen()) {
    showNormal();
    leaveFullScreenChrome();
    return;
  }

  enterFullScreenChrome();
  showFullScreen();
  // 抜け方が分からなくなるのを避ける。入った直後に 1 度だけ知らせる。
  messageOverlay_->showMessage(
      QCoreApplication::translate("MainWindow", "Full screen: press Esc or F11 to exit"), 2500);
}

// 全画面ではメニュー、ツールバー、ステータスを隠す。画像だけを見るための状態にする。
// ドックは隠さない。Pixel Info は利用者が開いたもので、全画面でも見たい場合がある。
void MainWindow::enterFullScreenChrome() {
  fullScreenChromeShown_ = false;
  fullScreenChromeTimer_.stop();
  menuBar()->setVisible(false);
  statusBar()->setVisible(false);
  toolBarsHiddenByFullScreen_.clear();
  for (auto *toolBar : findChildren<QToolBar *>()) {
    if (toolBar->isVisible()) {
      toolBarsHiddenByFullScreen_.push_back(toolBar);
      toolBar->setVisible(false);
    }
  }
  cursorIdleTimer_.start(cursorHideDelayMs_);
}

void MainWindow::leaveFullScreenChrome() {
  fullScreenChromeShown_ = false;
  fullScreenChromeTimer_.stop();
  menuBar()->setVisible(true);
  statusBar()->setVisible(true);
  // 隠したものだけを戻す。利用者が閉じていたツールバーは閉じたままにする。
  for (auto *toolBar : std::as_const(toolBarsHiddenByFullScreen_)) {
    toolBar->setVisible(true);
  }
  toolBarsHiddenByFullScreen_.clear();
  cursorIdleTimer_.stop();
  showCursorAgain();
  messageOverlay_->hide();
}

// 動かさないマウスのカーソルは、画像を見る邪魔になる。ダイアログが出ている間は隠さない。
void MainWindow::hideCursorAfterIdle() {
  if (!isFullScreen() || cursorHidden_ || QApplication::activeModalWidget() != nullptr) {
    return;
  }
  QGuiApplication::setOverrideCursor(Qt::BlankCursor);
  cursorHidden_ = true;
}

void MainWindow::showCursorAgain() {
  if (!cursorHidden_) {
    return;
  }
  QGuiApplication::restoreOverrideCursor();
  cursorHidden_ = false;
}

// 全画面で隠したメニューとツールバーを、マウスが上端へ寄ったときだけ、出す。
// 出すと、画像の面が、その分だけ下へずれる (拡大率は変わらない)。
void MainWindow::showFullScreenChrome() {
  if (fullScreenChromeShown_) {
    return;
  }
  fullScreenChromeShown_ = true;
  menuBar()->setVisible(true);
  for (auto *toolBar : std::as_const(toolBarsHiddenByFullScreen_)) {
    toolBar->setVisible(true);
  }
}

void MainWindow::hideFullScreenChrome() {
  if (!fullScreenChromeShown_) {
    return;
  }
  fullScreenChromeShown_ = false;
  fullScreenChromeTimer_.stop();
  menuBar()->setVisible(false);
  for (auto *toolBar : std::as_const(toolBarsHiddenByFullScreen_)) {
    toolBar->setVisible(false);
  }
}

void MainWindow::handleFullScreenPointer(const QPoint &globalPosition) {
  if (!isFullScreen() || QApplication::activeModalWidget() != nullptr) {
    return;
  }
  if (!fullScreenChromeShown_) {
    // 上端 (画面の最上段の行) へ寄せたら、出す。
    if (globalPosition.y() <= frameGeometry().top() + 1) {
      showFullScreenChrome();
    }
    return;
  }
  // 出ている間は、メニューとツールバーの上にいる限り、出したままにする。画像の面へ降りたら、少し待って引っ込める。
  const int chromeBottom = viewerGrid_->mapToGlobal(QPoint(0, 0)).y();
  if (globalPosition.y() < chromeBottom) {
    fullScreenChromeTimer_.stop();
  } else if (!fullScreenChromeTimer_.isActive()) {
    fullScreenChromeTimer_.start(chromeHideDelayMs_);
  }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  const auto type = event->type();
  if (type == QEvent::MouseMove || type == QEvent::MouseButtonPress) {
    showCursorAgain();
    if (isFullScreen()) {
      cursorIdleTimer_.start(cursorHideDelayMs_);
      if (type == QEvent::MouseMove) {
        handleFullScreenPointer(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::BackButton) {
    viewerGrid_->previousPage();
    event->accept();
    return;
  }
  if (event->button() == Qt::ForwardButton) {
    viewerGrid_->nextPage();
    event->accept();
    return;
  }
  QMainWindow::mousePressEvent(event);
}

void MainWindow::openImageFiles(const QStringList &paths) {
  if (paths.isEmpty()) {
    return;
  }

  // 既定セッションの上へ載せると、使わない画像セットとその名前が残る。
  // 画像を直接開く場合は空のセッションから始める。
  openPreparedImages(StartupPreparationController::prepareImages(paths, QDir::currentPath()));
}

void MainWindow::compareSelectedImages(const QStringList &paths) {
  if (paths.size() < 2 || paths.size() > SessionLayoutController::kMaxCellCount) {
    return;
  }
  syncCurrentSessionFromViewer();
  const auto prepared =
      StartupPreparationController::prepareImages(paths, currentSession_.sessionDirectory);
  pushUndo(captureUndoRecord(QCoreApplication::translate("MainWindow", "Compare selected images")));
  auto *imageList = findChild<ImageListPanelController *>();
  auto source = imageList->sourceSession();
  if (!source)
    source = currentSession_;
  const bool favoritesOnly = findChild<QAction *>("favoritesOnlyAction")->isChecked();
  imageList->beginSessionChange(std::move(source));
  currentSession_.viewer.layout = prepared.session.viewer.layout;
  currentSession_.images = prepared.session.images;
  currentSession_.cells = prepared.session.cells;
  cellLayoutCache_ = currentSession_.cells;
  viewerGrid_->setSession(currentSession_, prepared.initialPage);
  findChild<QAction *>("favoritesOnlyAction")->setChecked(favoritesOnly);
  imageList->endSessionChange();
  refreshSessionDependentUi();
  viewerGrid_->requestFitToCells();
  dirtyStateController_.setDirty(true);
}

void MainWindow::openPreparedImages(const PreparedImageSession &prepared,
                                    std::shared_ptr<DecodeScheduler> startupScheduler) {
  if (prepared.sourcePaths.isEmpty()) {
    return;
  }
  setSession(prepared.session, prepared.initialPage, std::move(startupScheduler));
  fitOnPageChangeOverride_ = true;
  updateFitOnPageChange();
  // 直接開いた画像は「普通に見る」使い方で、アニメーションは動くものと思って開く。
  // .hrn は比較が目的で、フレームを合わせて見るため止めたまま始める。
  // アニメーションが無いファイルでは何も動かない。
  viewerGrid_->setAnimationPlaying(true);
  updateAnimationActions();

  viewerGrid_->setCurrentPage(prepared.initialPage);
  if (!prepared.statusMessage.isEmpty()) {
    statusBar()->showMessage(prepared.statusMessage);
  }
  // 開いたこと自体は、戻せる変更ではない (開く前の状態は、もう無い)。
  clearUndoHistory();
  // 画像を直接開いた場合は必ず全体を出す。4K を等倍で出すと隅しか見えない。
  viewerGrid_->requestFitToCells();

  rememberRecentImages(prepared.sourcePaths);

  // 起動時に開いただけの状態を未保存扱いにすると、閉じるたびに保存を聞かれる。
  // .hrn として残したい場合は Save As を使う。
  dirtyStateController_.setDirty(false);
}

QString MainWindow::chooseSaveSessionPath(bool forceDialog) const {
  if (!forceDialog && !currentSession_.sessionFilePath.isEmpty()) {
    return currentSession_.sessionFilePath;
  }

  QFileDialog dialog(const_cast<MainWindow *>(this),
                     QCoreApplication::translate("MainWindow", "Save .hrn"),
                     currentSession_.sessionFilePath.isEmpty() ? QString("session.hrn")
                                                               : currentSession_.sessionFilePath,
                     QCoreApplication::translate("MainWindow", "Heiran Config (*.hrn)"));
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
  if (dialog.exec() != QDialog::Accepted) {
    return {};
  }

  const QStringList selectedFiles = dialog.selectedFiles();
  if (selectedFiles.isEmpty()) {
    return {};
  }

  return selectedFiles.first();
}

bool MainWindow::saveCurrentSession(bool forceDialog) {
  if (!saveSessionHandler_) {
    return false;
  }

  const QString filePath = chooseSaveSessionPath(forceDialog);
  if (filePath.isEmpty() || !confirmOverwrite(filePath)) {
    return false;
  }

  saveSessionHandler_(normalizedSessionForSave(), filePath);
  return true;
}

bool MainWindow::trySaveCurrentSession() {
  return saveCurrentSession(false) && !dirtyStateController_.isDirty();
}

bool MainWindow::confirmOverwrite(const QString &filePath) const {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
    return true;
  }

  return QMessageBox::question(
             const_cast<MainWindow *>(this),
             QCoreApplication::translate("MainWindow", "Confirm Overwrite"),
             QCoreApplication::translate("MainWindow", "%1 already exists. Overwrite it?")
                 .arg(QDir::toNativeSeparators(filePath)),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

// 開いた画像を履歴へ足す。ファイルが実在するものだけを覚える。
void MainWindow::rememberRecentImages(const QStringList &paths) {
  // 先頭の画像が履歴の先頭になるよう、後ろから足す。
  for (auto it = paths.crbegin(); it != paths.crend(); ++it) {
    if (QFileInfo(*it).isFile()) {
      recentImages_ = RecentImages::pushed(recentImages_, QFileInfo(*it).absoluteFilePath());
    }
  }
  RecentImages::save(recentImages_);
  rebuildRecentImagesMenu();
}

// 最近開いた画像を開く。無くなったファイルは、履歴から外す (残すと、押すたびに失敗する)。
void MainWindow::openRecentImage(const QString &path) {
  if (!QFileInfo::exists(path)) {
    recentImages_ = RecentImages::removed(recentImages_, path);
    RecentImages::save(recentImages_);
    rebuildRecentImagesMenu();
    announce(QCoreApplication::translate("MainWindow", "%1 no longer exists.")
                 .arg(QDir::toNativeSeparators(path)));
    return;
  }
  if (!dirtyStateController_.confirmContinueWithUnsavedChanges(
          [this]() { return saveCurrentSession(false) && !dirtyStateController_.isDirty(); })) {
    return;
  }
  openImageFiles({path});
}

void MainWindow::updateWelcomeVisibility() {
  if (welcome_ == nullptr) {
    return;
  }
  bool hasContent = false;
  for (const auto &cell : currentSession_.cells) {
    if ((cell.imageIndex >= 0 && cell.imageIndex < currentSession_.images.size()) ||
        cell.diff.has_value()) {
      hasContent = true;
      break;
    }
  }
  welcome_->setVisible(!hasContent);
  if (!hasContent) {
    welcome_->raise();
    // 通知 (操作の結果の短い文) は、案内の上に出す。
    messageOverlay_->raise();
  }
}

void MainWindow::rebuildRecentImagesMenu() {
  if (welcome_ != nullptr) {
    welcome_->setRecentImages(recentImages_);
  }
  if (recentImagesMenu_ == nullptr) {
    return;
  }

  recentImagesMenu_->clear();
  if (recentImages_.isEmpty()) {
    recentImagesMenu_->addAction(QCoreApplication::translate("MainWindow", "(empty)"))
        ->setEnabled(false);
    return;
  }

  for (const QString &path : std::as_const(recentImages_)) {
    // 長いパスはメニューが画面を超える。ファイル名を見せ、場所は説明に出す。
    auto *action = recentImagesMenu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(QDir::toNativeSeparators(path));
    action->setStatusTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path]() { openRecentImage(path); });
  }

  recentImagesMenu_->addSeparator();
  auto *clearAction = recentImagesMenu_->addAction(
      QCoreApplication::translate("MainWindow", "Clear Recent Images"));
  connect(clearAction, &QAction::triggered, this, [this]() {
    recentImages_.clear();
    RecentImages::save(recentImages_);
    rebuildRecentImagesMenu();
  });
}

void MainWindow::rebuildRecentSessionsMenu() {
  if (recentSessionsMenu_ == nullptr) {
    return;
  }

  recentSessionsMenu_->clear();
  if (recentSessions_.isEmpty()) {
    auto *emptyAction =
        recentSessionsMenu_->addAction(QCoreApplication::translate("MainWindow", "(empty)"));
    emptyAction->setEnabled(false);
    return;
  }

  for (const auto &sessionPath : recentSessions_) {
    auto *action = recentSessionsMenu_->addAction(sessionPath);
    connect(action, &QAction::triggered, this, [this, sessionPath]() {
      if (!openSessionHandler_ || !dirtyStateController_.confirmContinueWithUnsavedChanges(
                                      [this]() { return trySaveCurrentSession(); })) {
        return;
      }

      openSessionHandler_(sessionPath);
    });
  }
}

void MainWindow::updateViewerStatus(const QString &message) {
  statusBar()->showMessage(message);
  announcePageChangeInFullScreen();
}

void MainWindow::announcePageChangeInFullScreen() {
  const int page = viewerGrid_->currentPage();
  const bool changed = page != lastAnnouncedPage_;
  lastAnnouncedPage_ = page;
  if (!changed || !isFullScreen() || viewerGrid_->navigationPageCount() <= 0) {
    return;
  }

  const QString name = QFileInfo(viewerGrid_->targetCellFilePath()).fileName();
  messageOverlay_->showMessage(QString("%1 / %2  %3")
                                   .arg(viewerGrid_->navigationPageIndex() + 1)
                                   .arg(viewerGrid_->navigationPageCount())
                                   .arg(name));
}

void MainWindow::updatePixelInfoPanel(const QStringList &lines) {
  if (pixelInfoTextWidget_ == nullptr) {
    return;
  }

  static_cast<PixelInfoWidget *>(pixelInfoTextWidget_)->setLines(lines);
}

void MainWindow::updateLoupePanel(const LoupePreview &preview) {
  if (loupeWidget_ == nullptr) {
    return;
  }

  static_cast<LoupeWidget *>(loupeWidget_)->setPreview(preview);
}

void MainWindow::updatePageControls() {
  if (pageComboBox_ == nullptr) {
    return;
  }

  const int available = viewerGrid_->navigationPageCount();
  const auto allTexts =
      PageListing::displayTexts(viewerGrid_->sessionSnapshot(), viewerGrid_->totalPages());
  pageComboBox_->blockSignals(true);
  pageComboBox_->clear();
  for (int index = 0; index < available; ++index) {
    const int page = viewerGrid_->navigationPageAt(index);
    pageComboBox_->addItem(allTexts.value(page), page);
  }
  pageComboBox_->setCurrentIndex(viewerGrid_->navigationPageIndex());
  pageComboBox_->blockSignals(false);
  pageComboBox_->setEnabled(available > 0);
  if (viewerChrome_ != nullptr) {
    viewerChrome_->refreshPages();
  }
}

void MainWindow::refreshAnalysisWindows() {
  // 閉じている間は計算しない。解析は全画素を走査するため、開いていない窓のために
  // ページを送るたび走らせると操作が重くなる。窓は閉じても破棄しないため、
  // null 判定だけでは一度開いた後ずっと計算し続けることになる。
  const bool histogramActive = histogramWindow_ != nullptr && histogramWindow_->isVisible();
  const bool histogram3DActive = histogram3DWindow_ != nullptr && histogram3DWindow_->isVisible();
  const bool diffActive = diffWindow_ != nullptr && diffWindow_->isVisible();
  const bool changeGraphActive = changeGraphWindow_ != nullptr && changeGraphWindow_->isVisible();
  const bool imageInfoActive = imageInfoWindow_ != nullptr && imageInfoWindow_->isVisible();
  const bool waveformActive = waveformWindow_ != nullptr && waveformWindow_->isVisible();
  const bool vectorscopeActive = vectorscopeWindow_ != nullptr && vectorscopeWindow_->isVisible();
  if (!histogramActive && !histogram3DActive && !diffActive && !changeGraphActive &&
      !imageInfoActive && !waveformActive && !vectorscopeActive) {
    return;
  }

  // 収集は 1 度で済ませる。複数の窓が開いているときに何度も集めない。
  const int pageNumber = viewerGrid_->currentPage() + 1;
  const auto analysisImages = viewerGrid_->currentAnalysisImages();
  if (histogramActive) {
    histogramWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (histogram3DActive) {
    histogram3DWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (imageInfoActive) {
    imageInfoWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (diffActive) {
    diffWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (changeGraphActive) {
    changeGraphWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (waveformActive) {
    waveformWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
  if (vectorscopeActive) {
    vectorscopeWindow_->setAnalysisImages(pageNumber, analysisImages);
  }
}

// ビューアへセッションを渡した後に、その内容へ追従させる表示をまとめて更新する。
void MainWindow::addShortcutsToToolTips() {
  for (auto *toolBar : findChildren<QToolBar *>()) {
    for (auto *action : toolBar->actions()) {
      const auto shortcuts = action->shortcuts();
      if (shortcuts.isEmpty()) {
        continue;
      }
      // 説明が無いときは、名前 (action->text()) が出る。1 つ目のキーだけを添える
      // (全部を並べると長い)。
      QString tip = action->toolTip();
      const QString key = shortcuts.first().toString(QKeySequence::NativeText);
      if (!tip.contains(QString("(%1)").arg(key))) {
        tip = QString("%1 (%2)").arg(tip, key);
        action->setToolTip(tip);
      }
    }
  }
}

// 単独表示のメニューを、セルの数と、いまの状態に合わせる。
void MainWindow::updateSoloActions() {
  if (soloCellActionGroup_ == nullptr) {
    return;
  }
  const int cellCount = currentSession_.viewer.layout.rows * currentSession_.viewer.layout.cols;
  const int solo = viewerGrid_->soloCellIndex();
  for (auto *action : soloCellActionGroup_->actions()) {
    const int cellIndex = action->data().toInt();
    action->setEnabled(cellIndex < cellCount);
    action->setChecked(cellIndex == solo);
  }
}

void MainWindow::refreshSessionDependentUi() {
  updateWelcomeVisibility();
  updateSoloActions();
  updatePageControls();
  refreshAnalysisWindows();
  updateLayoutActions();
  updateChannelActions();
  updateInvertAction();
}

void MainWindow::applyCurrentSessionToUi() {
  viewerGrid_->setSession(currentSession_);
  refreshSessionDependentUi();
}

void MainWindow::applyCurrentSessionToUiPreservingView(int targetPage) {
  viewerGrid_->setSessionPreservingView(currentSession_);
  viewerGrid_->setCurrentPage(targetPage);
  refreshSessionDependentUi();
}

void MainWindow::scheduleSessionRefresh() {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        viewerGrid_->setSession(currentSession_);
        updatePageControls();
        refreshAnalysisWindows();
      },
      Qt::QueuedConnection);
}

void MainWindow::syncCurrentSessionFromViewer() {
  if (viewerGrid_ == nullptr) {
    return;
  }

  currentSession_ = viewerGrid_->sessionSnapshot();
  if (cellLayoutCache_.size() < currentSession_.cells.size()) {
    cellLayoutCache_.resize(currentSession_.cells.size());
  }
  for (int index = 0; index < currentSession_.cells.size(); ++index) {
    cellLayoutCache_[index] = currentSession_.cells.at(index);
  }
}

heiran::domain::SessionModel MainWindow::normalizedSessionForSave() const {
  auto session = viewerGrid_ != nullptr ? viewerGrid_->sessionSnapshot() : currentSession_;
  const int visibleCellCount = session.viewer.layout.rows * session.viewer.layout.cols;
  session.cells = session.cells.mid(0, visibleCellCount);
  return session;
}

MainWindow::UndoRecord MainWindow::captureUndoRecord(const QString &label) const {
  const auto *imageList = findChild<ImageListPanelController *>();
  const auto *favoritesOnly = findChild<QAction *>("favoritesOnlyAction");
  return {.label = label,
          .session = currentSession_,
          .cellLayoutCache = cellLayoutCache_,
          .page = viewerGrid_ != nullptr ? viewerGrid_->currentPage() : 0,
          .dirty = dirtyStateController_.isDirty(),
          .imageListSource = imageList ? imageList->sourceSession() : std::nullopt,
          .favoritesOnly = favoritesOnly && favoritesOnly->isChecked()};
}

void MainWindow::pushUndo(UndoRecord record) {
  if (undoSuppressDepth_ > 0) {
    return;
  }
  undoStack_.push_back(std::move(record));
  if (undoStack_.size() > kMaxUndoRecords) {
    undoStack_.removeFirst();
  }
  // 新しい変更をしたら、やり直しは、使えなくなる。
  redoStack_.clear();
  updateUndoActions();
}

void MainWindow::restoreUndoRecord(const UndoRecord &record) {
  auto *imageList = findChild<ImageListPanelController *>();
  imageList->beginSessionChange(record.imageListSource);
  currentSession_ = record.session;
  cellLayoutCache_ = record.cellLayoutCache;
  applyCurrentSessionToUiPreservingView(record.page);
  findChild<QAction *>("favoritesOnlyAction")->setChecked(record.favoritesOnly);
  imageList->endSessionChange();
  dirtyStateController_.setDirty(record.dirty);
}

void MainWindow::undo() {
  if (undoStack_.isEmpty()) {
    return;
  }
  syncCurrentSessionFromViewer();
  UndoRecord record = undoStack_.takeLast();
  // やり直しのために、いまの状態を控える。
  UndoRecord current = captureUndoRecord(record.label);
  redoStack_.push_back(std::move(current));
  restoreUndoRecord(record);
  updateUndoActions();
  announce(QCoreApplication::translate("MainWindow", "Undid: %1").arg(record.label));
}

void MainWindow::redo() {
  if (redoStack_.isEmpty()) {
    return;
  }
  syncCurrentSessionFromViewer();
  UndoRecord record = redoStack_.takeLast();
  UndoRecord current = captureUndoRecord(record.label);
  undoStack_.push_back(std::move(current));
  restoreUndoRecord(record);
  updateUndoActions();
  announce(QCoreApplication::translate("MainWindow", "Redid: %1").arg(record.label));
}

void MainWindow::updateUndoActions() {
  if (undoAction_ == nullptr || redoAction_ == nullptr) {
    return;
  }
  undoAction_->setEnabled(!undoStack_.isEmpty());
  undoAction_->setText(
      undoStack_.isEmpty()
          ? QCoreApplication::translate("MainWindow", "Undo")
          : QCoreApplication::translate("MainWindow", "Undo %1").arg(undoStack_.last().label));
  redoAction_->setEnabled(!redoStack_.isEmpty());
  redoAction_->setText(
      redoStack_.isEmpty()
          ? QCoreApplication::translate("MainWindow", "Redo")
          : QCoreApplication::translate("MainWindow", "Redo %1").arg(redoStack_.last().label));
}

// 別のセッションを開いたら、前の変更は、戻せない。
void MainWindow::clearUndoHistory() {
  undoStack_.clear();
  redoStack_.clear();
  updateUndoActions();
}

void MainWindow::applyDroppedItems(int targetCellIndex, const QStringList &paths) {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Drop Images"));
  const auto result = SessionMutationController::applyDroppedItems(
      &currentSession_, &cellLayoutCache_, targetCellIndex, paths, viewerGrid_->currentPage());
  if (!result.changed) {
    return;
  }
  pushUndo(std::move(before));

  applyCurrentSessionToUiPreservingView(result.targetPage);
  // 格子を広げたときは、セルの大きさが変わっている。全体が見えるようにフィットする。
  if (result.layoutChanged) {
    viewerGrid_->requestFitToCells();
  }
  dirtyStateController_.setDirty(true);
  if (!result.statusMessage.isEmpty()) {
    statusBar()->showMessage(result.statusMessage);
  }
}

void MainWindow::applyLabelVisibility(int cellIndex, bool visible) {
  syncCurrentSessionFromViewer();
  auto before =
      captureUndoRecord(visible ? QCoreApplication::translate("MainWindow", "Show Label")
                                : QCoreApplication::translate("MainWindow", "Hide Label"));
  if (!SessionMutationController::applyLabelVisibility(&currentSession_, cellIndex, visible)) {
    return;
  }
  pushUndo(std::move(before));
  dirtyStateController_.setDirty(true);
  scheduleSessionRefresh();
}

void MainWindow::editLabelForCell(int cellIndex) {
  syncCurrentSessionFromViewer();
  if (cellIndex < 0 || cellIndex >= currentSession_.cells.size()) {
    return;
  }

  const int imageIndex = currentSession_.cells.at(cellIndex).imageIndex;
  if (imageIndex < 0 || imageIndex >= currentSession_.images.size()) {
    return;
  }

  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Edit Label"));
  auto &imageSet = currentSession_.images[imageIndex];
  bool ok = false;
  const QString label =
      QInputDialog::getText(this, QCoreApplication::translate("MainWindow", "Edit Label"),
                            QCoreApplication::translate("MainWindow", "Label template"),
                            QLineEdit::Normal, imageSet.label, &ok);
  if (!ok) {
    return;
  }

  if (!SessionMutationController::applyLabelText(&currentSession_, cellIndex, label)) {
    return;
  }
  pushUndo(std::move(before));
  dirtyStateController_.setDirty(true);
  scheduleSessionRefresh();
}

void MainWindow::assignImageIndexToCell(int cellIndex, int imageIndex) {
  syncCurrentSessionFromViewer();
  auto before =
      captureUndoRecord(imageIndex < 0 ? QCoreApplication::translate("MainWindow", "Clear Cell")
                                       : QCoreApplication::translate("MainWindow", "Assign Image"));
  if (!SessionMutationController::assignImageIndexToCell(&currentSession_, &cellLayoutCache_,
                                                         cellIndex, imageIndex)) {
    return;
  }
  pushUndo(std::move(before));
  dirtyStateController_.setDirty(true);
  scheduleSessionRefresh();
}

void MainWindow::clearAllCells() {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Clear All Cells"));
  if (!SessionMutationController::clearAllCells(&currentSession_, &cellLayoutCache_)) {
    announce(QCoreApplication::translate("MainWindow", "There is nothing to clear."));
    return;
  }
  pushUndo(std::move(before));
  currentSession_.cells = cellLayoutCache_;
  applyCurrentSessionToUiPreservingView(viewerGrid_->currentPage());
  dirtyStateController_.setDirty(true);
  announce(QCoreApplication::translate("MainWindow", "Cleared all cells. (Ctrl+Z to undo)"));
}

void MainWindow::manageImageSets() {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Edit Image Sets"));
  ImageSetManagerDialog dialog(currentSession_, currentSession_.cells, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  pushUndo(std::move(before));

  const auto dialogResult = dialog.result();
  SessionMutationController::applyImageSetEdit(&currentSession_, dialogResult.images,
                                               dialogResult.cells);
  applyCurrentSessionToUi();
  dirtyStateController_.setDirty(true);
}

void MainWindow::applyLayout(int rows, int cols) {
  syncCurrentSessionFromViewer();
  // 格子を替えても、いま見ているページは変えない。セッションを入れ直すと 0 ページへ戻り、
  // 画像を直接開いたときのように、先頭がずれたページから始まる画像セットが空に見えてしまう。
  const int page = viewerGrid_->currentPage();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Change Layout"));
  if (!SessionLayoutController::applyLayout(&currentSession_, &cellLayoutCache_, rows, cols)) {
    return;
  }
  pushUndo(std::move(before));
  applyCurrentSessionToUi();
  viewerGrid_->setCurrentPage(page);
  dirtyStateController_.setDirty(true);
}

void MainWindow::addDifferenceCell(heiran::domain::DiffMode mode) {
  syncCurrentSessionFromViewer();
  auto before =
      captureUndoRecord(mode == heiran::domain::DiffMode::Wipe
                            ? QCoreApplication::translate("MainWindow", "Add Wipe Cell")
                            : QCoreApplication::translate("MainWindow", "Add Difference Cell"));
  if (viewerGrid_->firstEmptyCellIndex() < 0) {
    // 空きが無い。格子を 1 列 (列が 4 なら 1 行) 広げて、そこへ置く。
    auto grown = currentSession_.viewer.layout;
    if (!SessionLayoutController::growLayout(&grown)) {
      statusBar()->showMessage(QCoreApplication::translate(
          "MainWindow", "No room for a difference cell. Empty a cell first."));
      return;
    }
    const int rows = grown.rows;
    const int cols = grown.cols;
    // 格子を広げることも、1 つの変更として、控える (内側の変更は、控えない)。
    ++undoSuppressDepth_;
    applyLayout(rows, cols);
    --undoSuppressDepth_;
    // 格子を広げると、セルの大きさが変わる。全体が見えるようにフィットする。
    viewerGrid_->requestFitToCells();
  }
  if (!viewerGrid_->addDifferenceCell(mode)) {
    statusBar()->showMessage(
        mode == heiran::domain::DiffMode::Wipe
            ? QCoreApplication::translate("MainWindow", "A wipe needs two cells with images.")
            : QCoreApplication::translate("MainWindow",
                                          "A difference needs two cells with images."));
    // 格子を広げていたときは、広げたままになる。その分も、戻せるようにする。
    if (currentSession_.viewer.layout.rows != before.session.viewer.layout.rows ||
        currentSession_.viewer.layout.cols != before.session.viewer.layout.cols) {
      pushUndo(std::move(before));
    }
    return;
  }
  pushUndo(std::move(before));
  dirtyStateController_.setDirty(true);
}

void MainWindow::applyLayoutOrder(const QString &order) {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Change Fill Order"));
  if (!SessionLayoutController::applyLayoutOrder(&currentSession_, order)) {
    return;
  }
  pushUndo(std::move(before));
  applyCurrentSessionToUi();
  dirtyStateController_.setDirty(true);
}

void MainWindow::applyCustomImageOrder() {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Reorder Images"));
  const int cellCount = currentSession_.viewer.layout.rows * currentSession_.viewer.layout.cols;
  if (cellCount <= 0) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(QCoreApplication::translate("MainWindow", "Custom Image Order"));
  auto *layout = new QFormLayout(&dialog);
  QVector<QComboBox *> cellSelectors;
  cellSelectors.reserve(cellCount);

  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    auto *selector = new QComboBox(&dialog);
    selector->addItem(QCoreApplication::translate("MainWindow", "0: Empty"), -1);
    for (int imageIndex = 0; imageIndex < currentSession_.images.size(); ++imageIndex) {
      const auto &imageSet = currentSession_.images.at(imageIndex);
      selector->addItem(QString("%1: %2").arg(imageIndex + 1).arg(imageSet.name), imageIndex);
    }

    const int currentImageIndex = cellIndex < currentSession_.cells.size()
                                      ? currentSession_.cells.at(cellIndex).imageIndex
                                      : -1;
    const int selectedRow = std::max(0, currentImageIndex + 1);
    selector->setCurrentIndex(selectedRow);
    layout->addRow(QCoreApplication::translate("MainWindow", "Cell %1").arg(cellIndex + 1),
                   selector);
    cellSelectors.push_back(selector);
  }

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QVector<int> imageIndices;
  imageIndices.reserve(cellCount);
  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    imageIndices.push_back(cellSelectors.at(cellIndex)->currentData().toInt());
  }

  QVector<heiran::domain::CellConfig> newCells;
  QString errorMessage;
  if (!SessionLayoutController::applyCustomImageOrder(currentSession_, cellLayoutCache_,
                                                      imageIndices, &newCells, &errorMessage)) {
    if (!errorMessage.isEmpty()) {
      QMessageBox::warning(
          this, QCoreApplication::translate("MainWindow", "Duplicate Image Number"), errorMessage);
    }
    return;
  }
  pushUndo(std::move(before));
  cellLayoutCache_ = newCells;
  currentSession_.cells = cellLayoutCache_;
  applyCurrentSessionToUi();
  dirtyStateController_.setDirty(true);
}

// 並べ替えはどれも同じ手順になる。違うのは並べ方だけなので、それだけを受け取る。
void MainWindow::applyImageReorder(ImageReorder reorder) {
  syncCurrentSessionFromViewer();
  auto before = captureUndoRecord(QCoreApplication::translate("MainWindow", "Reorder Images"));
  if (!reorder(currentSession_, &cellLayoutCache_)) {
    return;
  }
  pushUndo(std::move(before));
  currentSession_.cells = cellLayoutCache_;
  applyCurrentSessionToUi();
  dirtyStateController_.setDirty(true);
}

void MainWindow::updateLayoutActions() {
  if (layoutActionGroup_ == nullptr) {
    return;
  }

  const QString currentLayout = QString("%1x%2")
                                    .arg(currentSession_.viewer.layout.rows)
                                    .arg(currentSession_.viewer.layout.cols);
  layoutActionGroup_->setExclusive(false);
  for (auto *action : layoutActionGroup_->actions()) {
    action->setChecked(action->data().toString() == currentLayout);
  }
  layoutActionGroup_->setExclusive(true);

  if (layoutOrderActionGroup_ != nullptr) {
    const QString currentOrder = currentSession_.viewer.layout.order;
    layoutOrderActionGroup_->setExclusive(false);
    for (auto *action : layoutOrderActionGroup_->actions()) {
      action->setChecked(action->data().toString() == currentOrder);
    }
    layoutOrderActionGroup_->setExclusive(true);
  }
}

void MainWindow::updateInvertAction() {
  if (invertAllAction_ == nullptr) {
    return;
  }

  QSignalBlocker blocker(invertAllAction_);
  invertAllAction_->setChecked(viewerGrid_->allCellsInverted());
}

void MainWindow::updateChannelActions() {
  if (channelActionGroup_ == nullptr) {
    return;
  }

  const bool hasCells = viewerGrid_->hasCells();
  for (auto *action : channelActionGroup_->actions()) {
    action->setEnabled(hasCells);
  }

  channelActionGroup_->setExclusive(false);
  if (channelColorAction_ != nullptr) {
    channelColorAction_->setChecked(false);
  }
  if (channelGrayAction_ != nullptr) {
    channelGrayAction_->setChecked(false);
  }
  if (channelRedAction_ != nullptr) {
    channelRedAction_->setChecked(false);
  }
  if (channelGreenAction_ != nullptr) {
    channelGreenAction_->setChecked(false);
  }
  if (channelBlueAction_ != nullptr) {
    channelBlueAction_->setChecked(false);
  }
  if (channelAlphaAction_ != nullptr) {
    channelAlphaAction_->setChecked(false);
  }
  channelActionGroup_->setExclusive(true);

  const auto uniformChannelMode = viewerGrid_->uniformChannelMode();
  if (!uniformChannelMode) {
    return;
  }

  switch (*uniformChannelMode) {
    case ChannelMode::Color:
      if (channelColorAction_ != nullptr) {
        channelColorAction_->setChecked(true);
      }
      break;
    case ChannelMode::Gray:
      if (channelGrayAction_ != nullptr) {
        channelGrayAction_->setChecked(true);
      }
      break;
    case ChannelMode::Red:
      if (channelRedAction_ != nullptr) {
        channelRedAction_->setChecked(true);
      }
      break;
    case ChannelMode::Green:
      if (channelGreenAction_ != nullptr) {
        channelGreenAction_->setChecked(true);
      }
      break;
    case ChannelMode::Blue:
      if (channelBlueAction_ != nullptr) {
        channelBlueAction_->setChecked(true);
      }
      break;
    case ChannelMode::Alpha:
      if (channelAlphaAction_ != nullptr) {
        channelAlphaAction_->setChecked(true);
      }
      break;
  }
}

void MainWindow::updateFitOnPageChange() {
  const bool enabled = fitOnPageChangeOverride_.value_or(appSettings_.fitOnPageChange);
  viewerGrid_->setFitOnPageChange(enabled);
  if (fitOnPageChangeAction_ != nullptr) {
    QSignalBlocker blocker(fitOnPageChangeAction_);
    fitOnPageChangeAction_->setChecked(enabled);
  }
}

void MainWindow::applyFitOnPageChange(bool enabled) {
  // 利用者が明示的に切り替えたら、それを優先する。直接開いた既定は外す。
  fitOnPageChangeOverride_.reset();
  appSettings_.fitOnPageChange = enabled;
  appSettings_.save();
  applyAppSettings();
}

void MainWindow::applyPixelValueFormat(PixelValueFormat pixelValueFormat) {
  appSettings_.pixelValueFormat = pixelValueFormat;
  appSettings_.save();
  applyAppSettings();
}

void MainWindow::applyCursorStyle(CursorStyle style) {
  appSettings_.cursorStyle = style;
  appSettings_.save();
  applyAppSettings();
}

void MainWindow::applyCellBorders(bool visible) {
  appSettings_.showCellBorders = visible;
  appSettings_.save();
  applyAppSettings();
}

void MainWindow::applyAnimationSyncMode(AnimationSyncMode mode) {
  appSettings_.animationSyncMode = mode;
  appSettings_.save();
  applyAppSettings();
}

// 設定の反映口を 1 か所にまとめる。メニューからでもダイアログからでも同じ経路を通す。
void MainWindow::applyAppSettings() {
  // 配色はアプリ全体に効く。ウィジェットの配色は Qt の仕組みへ委ねる。
  applyColorTheme(appSettings_.colorTheme);
  viewerGrid_->setPixelValueFormat(appSettings_.pixelValueFormat);
  viewerGrid_->setDisplayProfile(appSettings_.displayProfileMode, appSettings_.displayProfilePath);
  updateFitOnPageChange();
  viewerGrid_->setFilters(appSettings_.magnifyFilter, appSettings_.minifyFilter);
  viewerGrid_->setTransparencyBackground(appSettings_.transparencyBackground);
  viewerGrid_->setDecodeCacheBudgetBytes(
      resolveDecodeCacheBudgetBytes(appSettings_.decodeCacheBudgetMib,
                                    qEnvironmentVariableIntValue("HEIRAN_DECODE_CACHE_BUDGET_MB")));
  // 逐次展開の境目。すでに開いたアニメーションには効かず、これから開くものから変わる。
  heiran::infra::setAnimationStreamingThresholdBytes(resolveAnimationStreamingThresholdBytes(
      appSettings_.animationStreamingThresholdMib,
      qEnvironmentVariableIntValue("HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB")));
  TextureBudgetLedger::shared().setBudgetBytes(
      resolveGpuTextureBudgetBytes(appSettings_.gpuTextureBudgetMib,
                                   qEnvironmentVariableIntValue("HEIRAN_GPU_TEXTURE_BUDGET_MB")));

  if (pixelValueFormat255Action_ != nullptr) {
    QSignalBlocker blocker(pixelValueFormat255Action_);
    pixelValueFormat255Action_->setChecked(appSettings_.pixelValueFormat ==
                                           PixelValueFormat::Normalized255);
  }
  if (pixelValueFormat01Action_ != nullptr) {
    QSignalBlocker blocker(pixelValueFormat01Action_);
    pixelValueFormat01Action_->setChecked(appSettings_.pixelValueFormat ==
                                          PixelValueFormat::Normalized01);
  }
  if (loupeWidget_ != nullptr) {
    loupeWidget_->setPixelValueFormat(appSettings_.pixelValueFormat);
    loupeWidget_->setTransparencyBackground(appSettings_.transparencyBackground);
  }

  viewerGrid_->setCursorStyle(appSettings_.cursorStyle);
  if (cursorStyleActionGroup_ != nullptr) {
    for (auto *action : cursorStyleActionGroup_->actions()) {
      QSignalBlocker blocker(action);
      action->setChecked(action->data().toInt() == static_cast<int>(appSettings_.cursorStyle));
    }
  }
  viewerGrid_->setCellBordersVisible(appSettings_.showCellBorders);
  if (cellBordersAction_ != nullptr) {
    QSignalBlocker blocker(cellBordersAction_);
    cellBordersAction_->setChecked(appSettings_.showCellBorders);
  }
  viewerGrid_->setAnimationSyncMode(appSettings_.animationSyncMode);
  const std::pair<QAction *, AnimationSyncMode> syncActions[] = {
      {animationSyncAutoAction_, AnimationSyncMode::Auto},
      {animationSyncLockedAction_, AnimationSyncMode::Locked},
      {animationSyncIndependentAction_, AnimationSyncMode::Independent},
  };
  for (const auto &[action, mode] : syncActions) {
    if (action != nullptr) {
      QSignalBlocker blocker(action);
      action->setChecked(appSettings_.animationSyncMode == mode);
    }
  }
}

void MainWindow::openSettingsDialog() {
  const auto updated = showSettingsDialog(this, appSettings_);
  if (!updated) {
    return;
  }

  appSettings_ = *updated;
  appSettings_.save();
  applyAppSettings();
}

void MainWindow::applyChannelMode(ChannelMode channelMode) {
  viewerGrid_->setChannelModeForAllCells(channelMode);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (!dirtyStateController_.confirmContinueWithUnsavedChanges(
          [this]() { return trySaveCurrentSession(); })) {
    event->ignore();
    return;
  }

  saveWindowState();
  QMainWindow::closeEvent(event);
}

void MainWindow::updateAnimationActions() {
  if (playAnimationAction_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(playAnimationAction_);
  playAnimationAction_->setChecked(viewerGrid_->isAnimationPlaying());
}

void MainWindow::toggleAnimationPlayback() {
  if (viewerGrid_->isAnimationPlaying()) {
    viewerGrid_->setAnimationPlaying(false);
    updateAnimationActions();
    announce(QCoreApplication::translate("MainWindow", "Paused"));
    return;
  }

  // 動くものが無いのに再生にすると、押した結果が見えない。理由を知らせる。
  if (!viewerGrid_->hasAnimation()) {
    updateAnimationActions();
    announce(QCoreApplication::translate("MainWindow", "This file has no animation."));
    return;
  }
  viewerGrid_->setAnimationPlaying(true);
  updateAnimationActions();
  announce(QCoreApplication::translate("MainWindow", "Playing"));
}

void MainWindow::stepSubImage(int delta) {
  if (viewerGrid_->subImageCount() <= 1) {
    announce(QCoreApplication::translate("MainWindow", "This file has only one image."));
    return;
  }

  // 再生中に手で送ると、次のコマがすぐ来て見づらい。送ったら止める。
  if (viewerGrid_->isAnimationPlaying()) {
    viewerGrid_->setAnimationPlaying(false);
    updateAnimationActions();
  }
  viewerGrid_->stepSubImage(delta);
  announce(viewerGrid_->subImageStatusText());
}

void MainWindow::stepExposure(double stops) {
  if (exposureSpinBox_ != nullptr) {
    exposureSpinBox_->setValue(exposureSpinBox_->value() + stops);
  }
}

void MainWindow::announce(const QString &message) {
  statusBar()->showMessage(message, 4000);
  // 全画面ではステータスバーが見えない。画像の上へも出す。
  if (isFullScreen()) {
    messageOverlay_->showMessage(message);
  }
}

void MainWindow::refreshFileList(const QString &keepFilePath) {
  syncCurrentSessionFromViewer();
  const int targetCell = viewerGrid_->targetCellIndex();
  int page = viewerGrid_->currentPage();

  heiran::app::SessionPathResolver::resolveImagePaths(&currentSession_);
  if (!keepFilePath.isEmpty()) {
    const int keptPage = heiran::domain::pageOfFile(currentSession_, targetCell, keepFilePath);
    if (keptPage >= 0) {
      page = keptPage;
    }
  }
  // 画像セットの数やファイルの数は変わるが、設定そのものは変えない。未保存の印は付けない。
  applyCurrentSessionToUiPreservingView(page);
}

void MainWindow::renameCurrentFile() {
  const QString path = viewerGrid_->targetCellFilePath();
  if (path.isEmpty()) {
    announce(QCoreApplication::translate("MainWindow", "There is no file to rename."));
    return;
  }

  const QFileInfo info(path);
  QInputDialog dialog(this);
  dialog.setWindowTitle(QCoreApplication::translate("MainWindow", "Rename"));
  dialog.setLabelText(QCoreApplication::translate("MainWindow", "File name"));
  dialog.setTextValue(info.fileName());
  dialog.setMinimumWidth(420);
  // 拡張子は選ばずに、名前の部分だけを選んでおく。エクスプローラの F2 と同じ。
  // 表示のときに全選択されるため、その後に選び直す。
  if (auto *edit = dialog.findChild<QLineEdit *>()) {
    const int baseNameLength = static_cast<int>(info.completeBaseName().size());
    QTimer::singleShot(0, edit,
                       [edit, baseNameLength]() { edit->setSelection(0, baseNameLength); });
  }
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const auto result = FileOperations::rename(path, dialog.textValue());
  if (!result.ok) {
    QMessageBox::warning(this, QCoreApplication::translate("MainWindow", "Rename"),
                         result.errorMessage);
    return;
  }
  if (QFileInfo(result.newPath) == info) {
    return;
  }

  // 名前が変わると並びも変わる。同じファイルを表示し続ける。
  refreshFileList(result.newPath);
  announce(QCoreApplication::translate("MainWindow", "Renamed to %1")
               .arg(QFileInfo(result.newPath).fileName()));
}

void MainWindow::trashCurrentFile() {
  const QString path = viewerGrid_->targetCellFilePath();
  if (path.isEmpty()) {
    announce(QCoreApplication::translate("MainWindow", "There is no file to delete."));
    return;
  }

  const auto answer = QMessageBox::question(
      this, QCoreApplication::translate("MainWindow", "Move to Recycle Bin"),
      QCoreApplication::translate("MainWindow", "Move this file to the Recycle Bin?\n\n%1")
          .arg(QDir::toNativeSeparators(path)),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (answer != QMessageBox::Yes) {
    return;
  }

  const auto result = FileOperations::moveToTrash(path);
  if (!result.ok) {
    QMessageBox::warning(this, QCoreApplication::translate("MainWindow", "Move to Recycle Bin"),
                         result.errorMessage);
    return;
  }

  // 消したページの位置へ次のファイルが入る。最後のファイルなら 1 つ前へ寄る。
  refreshFileList();
  announce(QCoreApplication::translate("MainWindow", "Moved %1 to the Recycle Bin")
               .arg(QFileInfo(path).fileName()));
}

void MainWindow::copyCurrentFilePath() {
  const QString path = viewerGrid_->targetCellFilePath();
  if (path.isEmpty()) {
    announce(QCoreApplication::translate("MainWindow", "There is no file to copy the path of."));
    return;
  }

  QGuiApplication::clipboard()->setText(FileOperations::pathForClipboard(path));
  announce(QCoreApplication::translate("MainWindow", "Copied the file path."));
}

void MainWindow::printCurrentImage() {
  const QImage image = viewerGrid_->targetCellImage();
  if (image.isNull()) {
    announce(QCoreApplication::translate("MainWindow", "There is no image to print."));
    return;
  }

  QPrinter printer(QPrinter::HighResolution);
  QPrintDialog dialog(&printer, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  if (!ImagePrinter::print(&printer, image)) {
    QMessageBox::warning(this, QCoreApplication::translate("MainWindow", "Print"),
                         QCoreApplication::translate("MainWindow", "Could not print the image."));
    return;
  }
  announce(QCoreApplication::translate("MainWindow", "Sent the image to the printer."));
}

void MainWindow::saveWindowState() {
  // 全画面のまま保存すると、次の起動が全画面で始まって戻し方が分かりにくい。
  if (isFullScreen()) {
    return;
  }

  QSettings settings;
  settings.setValue("window/geometry", saveGeometry());
  settings.setValue("window/state", saveState());
}

void MainWindow::restoreWindowState() {
  QSettings settings;
  const QByteArray geometry = settings.value("window/geometry").toByteArray();
  if (geometry.isEmpty()) {
    // 初回はこの大きさで始める。
    resize(1200, 800);
    return;
  }

  restoreGeometry(geometry);
  // ドックとツールバーの配置。Pixel Info を閉じたまま終えた場合はそのまま閉じて始まる。
  restoreState(settings.value("window/state").toByteArray());
}

void MainWindow::updateWindowTitleForCurrentImage() {
  // 今どのファイルを見ているかをタイトルへ出す。1 セルのときだけ意味があるため、
  // 複数セルではセッション名の表示に留める。
  QString displayName;
  if (viewerGrid_ != nullptr && currentSession_.cells.size() == 1 &&
      currentSession_.sessionFilePath.isEmpty()) {
    const QString filePath = viewerGrid_->currentFilePathForCell(0);
    if (!filePath.isEmpty()) {
      displayName = QFileInfo(filePath).fileName();
    }
  }

  dirtyStateController_.setDisplayNameOverride(displayName);
}

}  // namespace heiran::ui
