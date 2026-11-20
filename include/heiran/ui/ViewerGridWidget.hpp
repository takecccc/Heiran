#pragma once

#include "heiran/domain/DiffMetrics.hpp"
#include "heiran/domain/DiffSettings.hpp"
#include "heiran/domain/SessionModel.hpp"
#include "heiran/domain/ViewState.hpp"
#include "heiran/ui/AnalysisSampler.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/DecodeCache.hpp"
#include "heiran/ui/DecodeScheduler.hpp"
#include "heiran/ui/DiffRenderer.hpp"
#include "heiran/ui/DisplayProfile.hpp"
#include "heiran/ui/PageNavigationFilter.hpp"
#include "heiran/ui/PixelValueFormat.hpp"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QGridLayout;
QT_END_NAMESPACE

namespace heiran {
namespace ui {

class ImageViewWidget;

struct AnalysisImage {
  QString label;
  // 表示用画像。選択範囲の座標系であり、差分表示の重ね合わせにも使う。
  QImage image;
  std::optional<QRect> selectionRect;
  // 統計値は表示用画像ではなくこちらから求める。
  AnalysisSampler sampler;
  // 表示しているファイル。画像の情報を出すのに使う。
  QString filePath;
};

struct LoupePreview {
  // 拡大表示の塗りに使う表示用画像。
  QImage image;
  QPointF imagePosition;
  ChannelMode channelMode = ChannelMode::Color;
  bool valid = false;
  // 数値表示はこちらから求める。
  AnalysisSampler sampler;
};

class ViewerGridWidget : public QWidget {
  Q_OBJECT

public:
  explicit ViewerGridWidget(QWidget *parent = nullptr);
  ~ViewerGridWidget() override;

  void setSession(const heiran::domain::SessionModel &session, int initialPage = 0,
                  std::shared_ptr<DecodeScheduler> startupScheduler = {});
  void setSessionPreservingView(const heiran::domain::SessionModel &session, int page = -1,
                                bool refit = true);
  void nextPage();
  void previousPage();
  void resetView();
  // 見えている中心を固定点にして倍率を変える。マウス位置を使わない操作用。
  void zoomBy(double zoomFactor);
  void setGlobalScale(double scale);
  // 操作対象セルの表示倍率を 1.0 にする。中心は保つ。
  void setActualSize();
  // 操作対象セルを 90 度単位で回す。正が時計回り。ショートカットキー用。
  void rotateActiveCellBy(int quarterTurns);
  // 操作対象セルの見た目をクリップボードへ渡す。
  void copyActiveCellImage() const;
  // ---- 差分のセル ----
  //
  // セルは、画像の代わりに、他のセルが見せている画像の差分を見せられる。通常のセルと同じに
  // ズームとパンが同期し、画素値も読める。差分は、各セルが回転や反転を掛ける前の画像から作る。
  std::optional<heiran::domain::CellDiff> cellDiff(int cellIndex) const;
  // `std::nullopt` で、差分をやめて元の表示へ戻す。セッションへ保存される。
  void setCellDiff(int cellIndex, const std::optional<heiran::domain::CellDiff> &diff);
  // 差分の数値指標。まだ求められていなければ空。
  std::optional<heiran::domain::DiffMetrics> cellDiffMetrics(int cellIndex) const;
  // 差分を作れなかった理由 (大きさが違う、など)。作れていれば空。
  QString cellDiffMessage(int cellIndex) const;
  // 差分のセルとして使える空きセル。無ければ -1。
  int firstEmptyCellIndex() const;
  // `cellIndex` に、`baseCell` を基準にした差分を出す。画像のあるセルは自身の画像との差分、
  // 空きセルは、基準でない画像のセルとの差分になる。比べるセルが足りなければ偽。
  bool showDifference(int cellIndex, int baseCell,
                      heiran::domain::DiffMode mode = heiran::domain::DiffMode::Absolute);
  // 空きセルへ、(操作対象のセルを基準に) 他のセルとの差分を出す。空きセルが無い、
  // 比べるセルが足りなければ偽。
  bool addDifferenceCell(heiran::domain::DiffMode mode = heiran::domain::DiffMode::Absolute);

  // 回転、コピー、ファイル操作の対象になるセル。操作中のセルがあればそれ、無ければ先頭。
  int targetCellIndex() const;

  // ---- ファイルの中の画像 (ページ / フレーム) ----
  //
  // ページ (マルチページ TIFF など) とフレーム (アニメーション GIF など) は別のもの。
  // どちらも全セルで 1 つの番号を共有する。ファイルの連番 (currentPage) とは別の軸で、
  // ファイルを変えると 0 へ戻る。
  int subImageIndex() const { return subImageIndex_; }
  // 表示中のファイルが持つ画像の数の最大。持たなければ 1。
  int subImageCount() const;
  bool hasAnimation() const;
  // 動かせるものの種類。アニメーションがあればフレーム、無ければページ、無ければ None。
  heiran::infra::SubImageKind subImageKind() const;
  // アニメーションは範囲を超えたら巻き戻る。ページは端で止まる。
  void setSubImageIndex(int index);
  void stepSubImage(int delta);
  // 再生は、アニメーションがあるときだけ進む。無いファイルへ移っても指定は残り、
  // アニメーションのあるファイルへ来たら再び動く。
  void setAnimationPlaying(bool playing);
  bool isAnimationPlaying() const { return animationPlaying_; }
  // 複数のセルでアニメーションを進める方式。コマ送りは、どの方式でも対象セルの番号を基準に、
  // 全セルを同じ番号へそろえる。
  void setAnimationSyncMode(AnimationSyncMode mode);
  AnimationSyncMode animationSyncMode() const { return animationSyncMode_; }
  // 実際にセルごとに独立して再生しているか (Auto を解決した結果)。
  bool playsAnimationsIndependently() const;
  // "Frame 3 / 12" や "Subpage 2 / 5"。持たなければ空。
  QString subImageStatusText() const;
  // 対象セルが表示しているファイルと、その見た目。表示できるものが無ければ空。
  QString targetCellFilePath() const;
  QImage targetCellImage() const;
  // ページ切り替え時にセルへフィットさせる。
  void setFilters(MagnifyFilter magnifyFilter, MinifyFilter minifyFilter);
  void setTransparencyBackground(const TransparencyBackground &background);
  // セルの枠線を描くか。あとから作るセルにも引き継ぐ。
  void setCellBordersVisible(bool visible);
  bool cellBordersVisible() const { return cellBordersVisible_; }
  void setCellHeadersVisible(bool visible);
  // 共有カーソルの描き方。あとから作るセルにも引き継ぐ。
  void setCursorStyle(CursorStyle style);
  void setDisplayProfile(DisplayProfileMode mode, const QString &profilePath);
  void setAnalysisValueSpace(AnalysisValueSpace valueSpace);
  void setColorLutOptions(const ColorLutOptions &options);
  void setDecodeCacheBudgetBytes(qint64 budgetBytes);

  // 使用量の把握用。decode キャッシュは CPU 側、テクスチャは VRAM 側で別枠になる。
  int decodeCacheEntryCount() const;
  int decodeCacheHitCount() const;
  int decodeCacheMissCount() const;
  void resetDecodeCacheCounters();
  qint64 decodeCacheUsedBytes() const;
  qint64 decodeCacheBudgetBytes() const;
  qint64 gpuTextureBytes() const;
  qint64 gpuCachedTextureBytes() const;

  // 指定したセルが今表示しているファイル。ウィンドウのタイトルに使う。
  QString currentFilePathForCell(int cellIndex) const;
  void setFitOnPageChange(bool enabled);
  bool fitOnPageChange() const;
  // 今すぐフィットさせる。画像が揃っていない場合は揃った時点で適用する。
  void requestFitToCells();
  void clearSelection();
  bool hasSelection() const { return selectionRect_.has_value(); }

  // 単独表示: 1 つのセルだけを、格子の全体へ広げて見せる (-1 で、全セルへ戻す)。
  // セッションは変えない (未保存の印も付かない)。ほかのセルは隠れるだけで、解析と画素値の対象には残る。
  // 拡大縮小と移動は全セル共通の基準で動くため、セルを切り替えても、同じ場所を同じ倍率で見続けられる。
  void setSoloCell(int cellIndex);
  int soloCellIndex() const { return soloCell_; }
  // 差の場 (差分の重い計算) を作った回数。倍率や見せ方の変更で、作り直していないことを、試験で確かめるために出している。
  int diffFieldComputeCount() const { return diffFieldComputeCount_; }
  // 全セル共通の表示の基準 (倍率と位置)。試験と、状態の確認に使う。
  heiran::domain::GlobalViewState globalViewState() const { return globalState_; }
  // 次 (1) / 前 (-1) の、画像のあるセルへ切り替える。単独表示でなければ、操作対象のセルから始める。
  void cycleSoloCell(int direction);
  heiran::domain::SessionModel sessionSnapshot() const;
  int currentPage() const;
  int totalPages() const;
  void setCurrentPage(int pageIndex, bool forceRefresh = false);
  void stepPages(int offset);
  void setPageFilter(std::optional<QVector<int>> pages);
  int navigationPageCount() const;
  int navigationPageIndex() const;
  int navigationPageAt(int index) const;
  QVector<AnalysisImage> currentAnalysisImages() const;
  bool hasCells() const;
  std::optional<ChannelMode> uniformChannelMode() const;
  int activeCellIndex() const;
  ChannelMode activeCellChannelMode() const;
  void setChannelModeForActiveCell(ChannelMode channelMode);
  void setChannelModeForCell(int cellIndex, ChannelMode channelMode);
  void setChannelModeForAllCells(ChannelMode channelMode);
  void setPixelValueFormat(PixelValueFormat pixelValueFormat);
  PixelValueFormat pixelValueFormat() const;
  void setExposureStops(double exposureStops);
  void setInvertForAllCells(bool invert);
  bool allCellsInverted() const;
  double exposureStops() const;

Q_SIGNALS:
  void sessionAboutToChange();
  void pageNavigationChanged();
  void sessionChanged(bool reset);
  void favoriteToggleRequested(int cellIndex);
  void transparencyPatternRequested(TransparencyPattern pattern);
  void statusChanged(const QString &message);
  void pixelInfoChanged(const QStringList &lines);
  void pixelPreviewChanged(const LoupePreview &preview);
  void viewChanged();
  void imageContentChanged();
  void activeCellChanged();
  // 単独表示の対象が変わった (-1 は、全セルの表示)。
  void soloCellChanged(int cellIndex);
  void persistentStateChanged();
  void dropItemsRequested(int targetCellIndex, const QStringList &paths);
  void labelVisibilityRequested(int cellIndex, bool visible);
  void editLabelRequested(int cellIndex);
  void assignImageIndexRequested(int cellIndex, int imageIndex);
  // 差分の指定か、求めた結果 (指標や理由) が変わった。
  void cellDiffChanged(int cellIndex);

protected:
  // セルの間の溝を、暗い色で塗る。
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void handleDecodeCompleted(const DecodeCompletion &completion);
  void rebuildCellStates();
  void refreshCellStatesFromSession();
  void rebuildGrid();
  // 単独表示の状態に合わせて、セルを格子へ置き直す。
  void applySoloLayout();
  // 操作の対象のセルの枠を、いまの状態に合わせる (複数のセルが見えているときだけ出す)。
  void updateActiveFrames();
  // 全セルが、いまの大きさに合わせてフィットした状態か (パンとズームで、動かしていない)。
  bool isViewFitted() const;
  // 指定の大きさの面に、フィットした状態か。
  bool isViewFitted(const QSize &cellSize) const;
  // 基準のセルの大きさが変わったとき、変わる前にフィットしていたなら、フィットし直す。動かした状態なら、そのまま保つ。
  void followCellResize();
  // 表示の基準になるセル (単独表示ならそのセル、でなければ先頭)。大きさは、その画面の面から取る。
  int sizeReferenceCell() const;
  int zoomAnchorCellIndex() const;
  QSet<QString> cacheKeysForPage(int page) const;
  QSet<QString> currentPageCacheKeys() const;
  QSet<QString> analysisWindowCacheKeys() const;
  bool imageSetNeedsAnalysisBuffer(const heiran::domain::ImageSet &imageSet) const;
  void refreshCellImages();
  // 差分のセルを作り直す。ソースの変化を 1 回にまとめるため、少し遅らせて走る。
  // 差分の帯と、右クリックの項目を、いまの指定へ合わせる。
  void updateDiffUi(int cellIndex);
  QStringList cellLabelsForDiff() const;
  void scheduleDiffRefresh();
  void refreshDiffCells();
  void refreshDiffCell(int cellIndex);
  // 差分の入力にする解析用の読み出し。各セルの回転や反転は掛けない。
  AnalysisSampler samplerForSource(const heiran::infra::DecodedImage &source) const;
  // 1 つのセルの画像と状態を、キャッシュにある結果から組み立て直す。
  // `countCacheAccess` が偽なら、効いているかの計測に数えない (再生の 1 コマごとなど)。
  void updateCellImage(int cellIndex, bool countCacheAccess);
  // セルの現在のファイルの、0 ページ目の復号結果。まだ無ければ nullptr。
  // キャッシュではなく、セルが自分で持っているものを返す。
  const heiran::infra::DecodeResult *baseResultForCell(int cellIndex) const;
  void applySubImageChange();
  // 後から作るアニメーション (FrameStream) の、作り終えの通知を受けるようにする。
  // 通知は作る側のスレッドから来るため、この窓のスレッドへ寄せて処理する。
  void observeStream(int cellIndex, std::shared_ptr<heiran::infra::FrameStream> stream);
  void releaseObservedStreams();
  void handleStreamFrameReady();
  // どのセルも、`index` 番目のフレームを作り終えているか。まだなら作らせる。
  bool prepareFrameForAllCells(int index);
  void updatePlaybackTimer();
  // ファイルを替えたときなど、ページとフレームの番号を先頭へ戻し、進めているタイマーを止める。
  void resetSubImage();
  // 動くセルをいまの番号から進めるタイマー。独立して再生するときだけ使う。
  void scheduleCell(int cellIndex);
  void advanceCell(int cellIndex);
  QTimer *timerForCell(int cellIndex);
  void stopAllPlaybackTimers();
  // アニメーションを持つセルの数と、そのフレーム数が揃っているか。
  bool animatedCellCountsDiffer() const;
  void scheduleNextFrame();
  void advanceFrame();
  void clearDecodeCache();
  heiran::infra::DecodeResult decodeImageCached(const QString &filePath, bool honorExifOrientation);
  void requestDecodeAsync(const QString &filePath, bool honorExifOrientation, bool prefetchRequest,
                          int subImageIndex = 0);
  bool currentPageUsesFile(const QString &filePath, bool honorExifOrientation) const;
  void applyDecodeResultToCurrentPage(const QString &filePath, bool honorExifOrientation,
                                      const heiran::infra::DecodeResult &result);
  void schedulePrefetchAroundCurrentPage();
  void prefetchAroundCurrentPage();
  void refreshCellViewStates();
  int pageCount() const;
  void updateStatus();
  void notifyPersistentStateChanged();
  void handleSyncToggleRequest(int cellIndex);
  void handleCellScaleRequest(int cellIndex, double scale);
  void handleCellGammaRequest(int cellIndex, double gamma);
  void handleCellInvertRequest(int cellIndex, bool invert);
  void handleReloadRequest(int cellIndex);
  void applyPendingFit();
  void handleCellRotationRequest(int cellIndex, int rotationQuarterTurns);
  void handleCellFlipRequest(int cellIndex, bool flipX, bool flipY);
  void handleResetCellDisplayRequest(int cellIndex);
  void handlePanRequest(int cellIndex, double deltaImageX, double deltaImageY);
  void handleZoomRequest(int cellIndex, double zoomFactor, double anchorImageX,
                         double anchorImageY);
  void handleActualSizeToggleRequest(int cellIndex, double anchorImageX, double anchorImageY);
  void handleHoverInfo(int cellIndex, const QPointF &imagePosition, bool active);
  void clearHoverInfo();
  void updateSharedCursorPositions(int referenceCellIndex, const QPointF &imagePosition);
  QStringList buildHoverDetails(int referenceCellIndex, const QPointF &imagePosition) const;
  LoupePreview buildLoupePreview(int cellIndex, const QPointF &imagePosition) const;
  void handleSelectionRequest(int cellIndex, const QPointF &startImagePosition,
                              const QPointF &endImagePosition, bool final);
  void setActiveCell(int cellIndex);
  void refreshSelectionOverlays();
  // 基準セルの画像座標を、対象セルで同じ表示位置を指す画像座標へ写す。
  // 選択範囲の重ね描き、カーソルの同期、解析対象の切り出しが使う。
  QRectF mapImageRectToCell(const QRectF &imageRect, int referenceCellIndex,
                            int targetCellIndex) const;
  QPointF mapImagePointToCell(const QPointF &point, int referenceCellIndex,
                              int targetCellIndex) const;

  heiran::domain::SessionModel session_;
  heiran::domain::GlobalViewState globalState_;
  QVector<heiran::domain::CellViewState> cellStates_;
  QVector<ImageViewWidget *> cells_;
  QGridLayout *layout_ = nullptr;
  QString hoverInfoSummary_;
  QVector<ChannelMode> channelModes_;
  PixelValueFormat pixelValueFormat_ = PixelValueFormat::Normalized255;
  double exposureStops_ = 0.0;
  AnalysisValueSpace analysisValueSpace_ = AnalysisValueSpace::Source;
  ColorLutOptions colorLutOptions_;
  DisplayProfileMode displayProfileMode_ = DisplayProfileMode::System;
  QString displayProfilePath_;
  MagnifyFilter magnifyFilter_ = MagnifyFilter::Nearest;
  TransparencyBackground transparencyBackground_;
  bool cellBordersVisible_ = true;
  bool cellHeadersVisible_ = true;
  CursorStyle cursorStyle_ = CursorStyle::Outline;
  MinifyFilter minifyFilter_ = MinifyFilter::Area;
  int activeCellIndex_ = -1;
  int soloCell_ = -1;
  int diffFieldComputeCount_ = 0;
  std::optional<QRectF> selectionRect_;
  std::optional<QRectF> previewSelectionRect_;
  int selectionReferenceCellIndex_ = -1;
  int previewSelectionReferenceCellIndex_ = -1;
  std::optional<QPointF> hoverCursorPosition_;
  int hoverReferenceCellIndex_ = -1;
  heiran::infra::ImageDecoder imageDecoder_;
  DecodeCache decodeCache_;
  std::shared_ptr<DecodeScheduler> decodeScheduler_;
  PageNavigationFilter pageNavigation_;
  bool prefetchScheduled_ = false;
  // セルごとの、いま見せているファイルの 0 ページ目の復号結果。
  //
  // **キャッシュに頼らずセルが抱える。** アニメーションは全フレームを持つ結果で、1 つが数百 MB に
  // なる。フォルダ内の他のアニメーションを先読みしただけで、キャッシュの予算を超えて表示中の
  // ものが追い出され、再生が止まる。追い出されても、セルが持つ限りは再生を続けられる。
  QVector<heiran::infra::DecodeResult> cellBaseResults_;
  // セルが、差分にする前に見せる画像 (差分のセルの元にもなる)。差分のセルでも、自身の画像を持つ。
  QVector<heiran::infra::DecodedImage> cellSources_;
  // `cellSources_` が替わるたびに増やす。差分を作り直すかの判断に使う。
  QVector<quint64> cellSourceSerial_;
  struct DiffState {
    bool computed = false;
    quint64 baseSerial = 0;
    quint64 targetSerial = 0;
    heiran::domain::DiffSettings settings;
    std::optional<heiran::domain::DiffMetrics> metrics;
    QString message;
    // 差の場 (計算が重い部分)。倍率、見せ方、閾値だけが変わったときは、これを使い回す。
    std::shared_ptr<const DiffField> field;
  };
  QVector<DiffState> diffStates_;
  bool diffRefreshScheduled_ = false;
  // セルが作り終えを待っている後から作るアニメーション。セルごとに 1 つ。
  QVector<std::shared_ptr<heiran::infra::FrameStream>> observedStreams_;
  // セルがいま見せているフレームの番号。後から作るアニメーションで、見せ直すかの判断に使う。
  QVector<int> cellShownFrame_;
  std::atomic_bool streamNotifyPending_{false};
  int subImageIndex_ = 0;
  // セルごとの、いま見せているフレームの番号。同期して進めるときは全セルが同じ番号になる。
  QVector<int> cellFrame_;
  // セルごとのタイマー。独立して再生するとき、それぞれの表示時間で進める。
  QVector<QTimer *> cellTimers_;
  AnimationSyncMode animationSyncMode_ = AnimationSyncMode::Auto;
  // タイマーがいま独立の側で動いているか。切り替わったときに揃え直すために覚える。
  bool independentActive_ = false;
  bool animationPlaying_ = false;
  QTimer playbackTimer_;
  bool fitOnPageChange_ = false;
  bool fitPending_ = false;
  QSize lastReferenceCanvasSize_;
};

}  // namespace ui
}  // namespace heiran
