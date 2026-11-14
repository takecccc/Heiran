#pragma once

#include "heiran/domain/SessionModel.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/DirtyStateController.hpp"
#include "heiran/ui/PixelValueFormat.hpp"
#include "heiran/ui/SessionIssueDialog.hpp"

#include <QMainWindow>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QToolButton;
class QMenu;
class QMouseEvent;
class QToolBar;
class QWidget;
QT_END_NAMESPACE

namespace heiran {
namespace ui {

class LoupeWidget;
class PixelInfoWidget;
class Histogram3DWindow;
class WaveformWindow;
class VectorscopeWindow;
class ImageInfoWindow;
class HistogramWindow;
class DiffWindow;
class ChangeGraphWindow;
class ImageSetManagerDialog;
class TransientMessageWidget;
class ViewerGridWidget;
class WelcomeWidget;
class ViewerChromeController;
struct LoupePreview;

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(QWidget *parent = nullptr);

  void setSession(const heiran::domain::SessionModel &session);
  void setStartupMessage(const QString &message);
  void setOpenSessionHandler(std::function<void(const QString &)> handler);
  void setSaveSessionHandler(
      std::function<void(const heiran::domain::SessionModel &, const QString &)> handler);
  void setRecentSessions(const QStringList &recentSessions);

  // 画像ファイルを直接開く。関連付けから起動された場合とコマンドラインで
  // 画像を渡された場合に使う。枚数に合う格子へ切り替えてから割り当てる。
  // その場限りのセッションのため、未保存の印は付けない。
  void openImageFiles(const QStringList &paths);

  // 画像セットのファイル一覧を読み直す。フォルダへファイルが増減したときや、ファイルの
  // 名前を変えたり消したりしたときに使う。表示中のページはそのままにする。
  // `keepFilePath` を渡すと、そのファイルが並びの中で動いても同じファイルを表示し続ける。
  void refreshFileList(const QString &keepFilePath = QString());

  // 読込や保存で問題が起きた場合の統一した報告口。
  // 致命的な場合は別ファイルを開く導線も出す。
  void reportSessionIssues(const SessionIssueReport &report);

  // 全画面でマウスが動かないまま経つと、カーソルを隠すまでの時間。試験で短くするために出している。
  void setFullScreenCursorHideDelay(int milliseconds) { cursorHideDelayMs_ = milliseconds; }

  // 全画面で、マウスが画面の上端へ寄ったら、メニューとツールバーを一時的に出す。離れたら、また隠す。
  // アプリ全体のマウスの動きから呼ばれる。位置は、画面の座標。試験からも呼べるように出している。
  void handleFullScreenPointer(const QPoint &globalPosition);
  // 全画面で、メニューとツールバーが一時的に出ているか。
  bool isFullScreenChromeShown() const { return fullScreenChromeShown_; }
  // メニューとツールバーが引っ込むまでの待ち。試験で短くするために出している。
  void setFullScreenChromeHideDelay(int milliseconds) { chromeHideDelayMs_ = milliseconds; }

protected:
  void closeEvent(QCloseEvent *event) override;
  // マウスの戻る / 進むボタンをページ送りに使う。描画面が受けなかったボタンがここへ届く。
  void mousePressEvent(QMouseEvent *event) override;
  // 全画面でカーソルを隠す判断のため、アプリ全体のマウスの動きを見る。
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void openImageFilesFromDialog();
  // 対象セルが表示しているファイルへの操作。全画面でもキーで使える。
  void renameCurrentFile();
  void trashCurrentFile();
  void copyCurrentFilePath();
  void printCurrentImage();
  // 操作の結果を短く知らせる。
  void announce(const QString &message);
  // 全画面ではステータスバーが隠れる。ページが変わったことを画像の上へ短く出す。
  void announcePageChangeInFullScreen();
  // アニメーションの再生 / 停止と、コマ送り (ページ送り)。全画面でもキーで使う。
  void toggleAnimationPlayback();
  void stepSubImage(int delta);
  void updateAnimationActions();
  void enterFullScreenChrome();
  void leaveFullScreenChrome();
  void hideCursorAfterIdle();
  void showCursorAgain();
  void stepExposure(double stops);
  void saveWindowState();
  void restoreWindowState();
  void updateWindowTitleForCurrentImage();
  void toggleFullScreen();
  QString chooseSaveSessionPath(bool forceDialog) const;
  bool saveCurrentSession(bool forceDialog);
  bool trySaveCurrentSession();
  bool confirmOverwrite(const QString &filePath) const;
  void rebuildRecentSessionsMenu();
  void rebuildRecentImagesMenu();
  void rememberRecentImages(const QStringList &paths);
  void updateViewerStatus(const QString &message);
  void updatePixelInfoPanel(const QStringList &lines);
  void updateLoupePanel(const LoupePreview &preview);
  void openPageThumbnails();
  void updatePageControls();
  // コンストラクタの段階分け。いずれも private メンバへ触れるため、別クラスへは
  // 出さずにメンバ関数のままにしてある。渡す依存が多すぎて、外へ出すと
  // かえって読みにくくなる。
  void connectViewerGrid();
  void createPixelInfoDock();
  void createFileMenu();
  void createImagesMenu();
  void createLayoutMenu();
  void createViewMenu();
  void createAnalysisMenu();
  void createHelpMenu();
  void openManual();
  void createNavigationToolBar();
  void createGlobalShortcuts();
  void applyInitialState();

  void refreshAnalysisWindows();
  void refreshSessionDependentUi();
  void applyCurrentSessionToUi();
  void applyCurrentSessionToUiPreservingView(int targetPage);
  void scheduleSessionRefresh();
  void syncCurrentSessionFromViewer();
  heiran::domain::SessionModel normalizedSessionForSave() const;
  void applyDroppedItems(int targetCellIndex, const QStringList &paths);
  void applyLabelVisibility(int cellIndex, bool visible);
  void editLabelForCell(int cellIndex);
  void assignImageIndexToCell(int cellIndex, int imageIndex);
  void manageImageSets();
  void clearAllCells();
  void applyLayout(int rows, int cols);
  // 空きセル (無ければ格子を広げて) へ、差分のセルを足す。
  void addDifferenceCell(heiran::domain::DiffMode mode = heiran::domain::DiffMode::Absolute);
  void applyLayoutOrder(const QString &order);
  void applyCustomImageOrder();
  // 並べ方だけを差し替えて使う。`SessionLayoutController` の並べ替えを渡す。
  using ImageReorder = bool (*)(const heiran::domain::SessionModel &,
                                QVector<heiran::domain::CellConfig> *);
  void applyImageReorder(ImageReorder reorder);
  void updateLayoutActions();
  void updateChannelActions();
  void updateInvertAction();
  void applyChannelMode(ChannelMode channelMode);
  void applyAppSettings();
  void openSettingsDialog();
  void applyPixelValueFormat(PixelValueFormat pixelValueFormat);
  void applyAnimationSyncMode(AnimationSyncMode mode);
  void applyCellBorders(bool visible);
  void applyCursorStyle(CursorStyle style);
  void applyFitOnPageChange(bool enabled);
  // ビューアと Fit on Page Change の表示を、いま有効な値へ揃える。
  void updateFitOnPageChange();

  DirtyStateController dirtyStateController_;
  ViewerGridWidget *viewerGrid_ = nullptr;
  HistogramWindow *histogramWindow_ = nullptr;
  Histogram3DWindow *histogram3DWindow_ = nullptr;
  WaveformWindow *waveformWindow_ = nullptr;
  VectorscopeWindow *vectorscopeWindow_ = nullptr;
  ImageInfoWindow *imageInfoWindow_ = nullptr;
  DiffWindow *diffWindow_ = nullptr;
  ChangeGraphWindow *changeGraphWindow_ = nullptr;
  QAction *clearSelectionAction_ = nullptr;
  QActionGroup *soloCellActionGroup_ = nullptr;
  void updateSoloActions();
  // ツールバーのボタンの説明へ、割り当てたキーを添える (アイコンだけでは、キーが分からない)。
  void addShortcutsToToolTips();
  // 画像を 1 枚も開いていないときだけ、案内を出す。
  void updateWelcomeVisibility();
  void openRecentImage(const QString &path);
  WelcomeWidget *welcome_ = nullptr;
  ViewerChromeController *viewerChrome_ = nullptr;
  QActionGroup *layoutActionGroup_ = nullptr;
  QActionGroup *layoutOrderActionGroup_ = nullptr;
  QActionGroup *channelActionGroup_ = nullptr;
  QActionGroup *pixelValueFormatActionGroup_ = nullptr;
  QAction *invertAllAction_ = nullptr;
  QAction *fitOnPageChangeAction_ = nullptr;
  QAction *pixelValueFormat255Action_ = nullptr;
  QAction *pixelValueFormat01Action_ = nullptr;
  AppSettings appSettings_;
  // 画像を直接開いたセッションだけで有効にする Fit on Page Change。設定は変えない。
  // 直接開く使い方はページごとに大きさの違う写真をめくることが多く、ズームとパンを
  // 引き継ぐと 2 枚目以降が隅しか見えないことがある。.hrn は比較が目的で、
  // 引き継ぐ方が正しいため、設定のままにする。
  std::optional<bool> fitOnPageChangeOverride_;
  QAction *channelColorAction_ = nullptr;
  QAction *channelGrayAction_ = nullptr;
  QAction *channelRedAction_ = nullptr;
  QAction *channelGreenAction_ = nullptr;
  QAction *channelBlueAction_ = nullptr;
  QAction *channelAlphaAction_ = nullptr;
  QComboBox *pageComboBox_ = nullptr;
  // 全画面へ入るときに隠したツールバー。抜けるときに、隠したものだけを戻す。
  QVector<QToolBar *> toolBarsHiddenByFullScreen_;
  QDockWidget *pixelInfoDock_ = nullptr;
  LoupeWidget *loupeWidget_ = nullptr;
  PixelInfoWidget *pixelInfoTextWidget_ = nullptr;
  QMenu *recentSessionsMenu_ = nullptr;
  QMenu *recentImagesMenu_ = nullptr;
  QStringList recentImages_;
  TransientMessageWidget *messageOverlay_ = nullptr;
  QDoubleSpinBox *exposureSpinBox_ = nullptr;
  QToolButton *exposureButton_ = nullptr;
  QAction *cellBordersAction_ = nullptr;
  QActionGroup *cursorStyleActionGroup_ = nullptr;
  QAction *playAnimationAction_ = nullptr;
  QAction *animationSyncAutoAction_ = nullptr;
  QAction *animationSyncLockedAction_ = nullptr;
  QAction *animationSyncIndependentAction_ = nullptr;
  QTimer cursorIdleTimer_;
  int cursorHideDelayMs_ = 2000;
  bool cursorHidden_ = false;
  bool fullScreenChromeShown_ = false;

  // 元に戻す: 画像の割り当て、格子、画像セット、並べ替えなどの変更を、1 つずつ戻す (セッションの構造の変更。拡大やパンの操作は含めない)。
  struct UndoRecord {
    QString label;
    heiran::domain::SessionModel session;
    QVector<heiran::domain::CellConfig> cellLayoutCache;
    int page = 0;
    bool dirty = false;
  };
  static constexpr int kMaxUndoRecords = 30;
  // 変更の前の状態を控える (syncCurrentSessionFromViewer() のあとで呼ぶ)。変更が済んだら、pushUndo() へ渡す。
  UndoRecord captureUndoRecord(const QString &label) const;
  void pushUndo(UndoRecord record);
  void undo();
  void redo();
  void restoreUndoRecord(const UndoRecord &record);
  void updateUndoActions();
  void clearUndoHistory();
  QVector<UndoRecord> undoStack_;
  QVector<UndoRecord> redoStack_;
  QAction *undoAction_ = nullptr;
  QAction *redoAction_ = nullptr;
  int undoSuppressDepth_ = 0;
  int chromeHideDelayMs_ = 350;
  QTimer fullScreenChromeTimer_;
  void showFullScreenChrome();
  void hideFullScreenChrome();
  // 最後に知らせたページ。同じページのまま状態欄が更新されても、知らせ直さない。
  int lastAnnouncedPage_ = -1;
  std::function<void(const QString &)> openSessionHandler_;
  std::function<void(const heiran::domain::SessionModel &, const QString &)> saveSessionHandler_;
  QStringList recentSessions_;
  heiran::domain::SessionModel currentSession_;
  QVector<heiran::domain::CellConfig> cellLayoutCache_;
};

}  // namespace ui
}  // namespace heiran
