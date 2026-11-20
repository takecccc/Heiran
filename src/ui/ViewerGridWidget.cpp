#include "heiran/ui/ViewerGridWidget.hpp"
#include "heiran/ui/PixelInfoText.hpp"

#include "heiran/domain/CellConfigMapping.hpp"
#include "heiran/domain/ImageTransform.hpp"
#include "heiran/domain/SessionPages.hpp"
#include "heiran/ui/CellStatus.hpp"
#include "heiran/ui/DiffRenderer.hpp"
#include "heiran/ui/ImageViewWidget.hpp"

#include "heiran/ui/PrefetchPlanner.hpp"
#include "heiran/ui/ProfileAccumulator.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QMetaObject>
#include <QPainter>
#include <QScopeGuard>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

// hover 1 回分の内訳。`hoverProfiler()` へ渡す並びと同じ順に持つ。
struct HoverProfileSample {
  qint64 totalNs = 0;
  qint64 cursorNs = 0;
  qint64 detailsNs = 0;
  qint64 infoHandlerNs = 0;
  qint64 previewNs = 0;
  qint64 statusNs = 0;
};

ProfileAccumulator &panProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "pan", {"total", "refresh", "status"},
                                     30);
  return profiler;
}

ProfileAccumulator &hoverProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "hover",
                                     {"total", "cursor", "details", "info", "preview", "status"},
                                     60);
  return profiler;
}

ProfileAccumulator &loadProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_LOAD", "load",
                                     {"decode", "apply", "handler", "finish"}, 20);
  return profiler;
}

bool profilingEnabled() {
  return panProfiler().enabled();
}

bool loadProfilingEnabled() {
  return loadProfiler().enabled();
}

bool decodeBackendTraceEnabled() {
  static const bool enabled = qEnvironmentVariableIntValue("HEIRAN_TRACE_DECODER") != 0;
  return enabled;
}

// 使用量をステータスへ出す。実セッションで確かめたいときに使う。
bool memoryTraceEnabled() {
  static const bool enabled = qEnvironmentVariableIntValue("HEIRAN_TRACE_MEMORY") != 0;
  return enabled;
}

constexpr int kPrefetchRadiusPages = 4;

// 解析用バッファを持つ画像を残すページ範囲。
// 4K の 16bit 画像は解析用バッファだけで 1 枚 66 MiB あり、先読み分まで抱えると
// セル数とページ数の積で効いてくる。1 ページ送りは滑らかに保ちたいので前後 1 とする。
constexpr int kAnalysisWindowRadiusPages = 1;

// 隣のセルとの境目。線 1 本では、背景が近い色で見分けにくい。暗い溝を挟む。
constexpr int kCellGutterPixels = 4;
constexpr QColor kCellGutterColor(14, 15, 18);

}  // namespace

ViewerGridWidget::ViewerGridWidget(QWidget *parent) : QWidget(parent), layout_(new QGridLayout) {
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(kCellGutterPixels);
  setLayout(layout_);
  // 矢印キーがツールバーの入力欄へ吸われないよう、既定のフォーカス先をここにする。
  setFocusPolicy(Qt::StrongFocus);
  playbackTimer_.setSingleShot(true);
  playbackTimer_.setTimerType(Qt::PreciseTimer);
  connect(&playbackTimer_, &QTimer::timeout, this, [this]() { advanceFrame(); });
  decodeScheduler_ = std::make_shared<DecodeScheduler>();
  connect(decodeScheduler_.get(), &DecodeScheduler::decodeCompleted, this,
          &ViewerGridWidget::handleDecodeCompleted);
}

ViewerGridWidget::~ViewerGridWidget() {
  // 作る側のスレッドから、破棄済みのこの窓を呼ばせない。
  releaseObservedStreams();
}

void ViewerGridWidget::handleDecodeCompleted(const DecodeCompletion &notification) {
  const auto ready = decodeScheduler_->takeCompleted(notification.cacheKey);
  if (!ready) {
    return;
  }
  const auto &completion = *ready;
  QElapsedTimer totalTimer;
  QElapsedTimer timer;
  if (loadProfilingEnabled()) {
    totalTimer.start();
    timer.start();
  }

  decodeCache_.insert(completion.cacheKey, completion.result);
  if (!currentPageUsesFile(completion.filePath, completion.honorExifOrientation)) {
    loadProfiler().record(
        {completion.decodeDurationNs, 0, 0, loadProfilingEnabled() ? timer.nsecsElapsed() : 0});
    return;
  }

  qint64 applyDurationNs = 0;
  if (loadProfilingEnabled()) {
    timer.restart();
  }
  applyDecodeResultToCurrentPage(completion.filePath, completion.honorExifOrientation,
                                 completion.result);
  if (loadProfilingEnabled()) {
    applyDurationNs = timer.nsecsElapsed();
    timer.restart();
  }
  updateStatus();
  applyPendingFit();
  qint64 handlerDurationNs = 0;
  Q_EMIT imageContentChanged();
  if (loadProfilingEnabled()) {
    handlerDurationNs = timer.nsecsElapsed();
  }
  loadProfiler().record({completion.decodeDurationNs, applyDurationNs, handlerDurationNs,
                         loadProfilingEnabled() ? totalTimer.nsecsElapsed() : 0});
  schedulePrefetchAroundCurrentPage();
}

void ViewerGridWidget::setSession(const heiran::domain::SessionModel &session, int initialPage,
                                  std::shared_ptr<DecodeScheduler> startupScheduler) {
  Q_EMIT sessionAboutToChange();
  session_ = session;
  globalState_ = {};
  pageNavigation_.setPages(std::nullopt);
  globalState_.currentPage =
      std::clamp(initialPage, 0, std::max(0, heiran::domain::totalPageCount(session_) - 1));
  // 作り直しでは先頭のフレームへ戻す。再生の指定は残す。レイアウトの変更やドロップでも
  // 呼ばれるため、ここで止めると画像を直接開いた後の編集で再生が止まる。
  // 新しいセッションを止めた状態で始める方針は、開く側 (MainWindow) が持つ。
  resetSubImage();
  releaseObservedStreams();
  hoverInfoSummary_.clear();
  activeCellIndex_ = -1;
  selectionRect_.reset();
  previewSelectionRect_.reset();
  selectionReferenceCellIndex_ = -1;
  previewSelectionReferenceCellIndex_ = -1;
  hoverCursorPosition_.reset();
  hoverReferenceCellIndex_ = -1;
  clearDecodeCache();
  if (startupScheduler) {
    disconnect(decodeScheduler_.get(), nullptr, this, nullptr);
    decodeScheduler_ = std::move(startupScheduler);
    connect(decodeScheduler_.get(), &DecodeScheduler::decodeCompleted, this,
            &ViewerGridWidget::handleDecodeCompleted);
    for (const auto &completion : decodeScheduler_->activate()) {
      decodeCache_.insert(completion.cacheKey, completion.result);
    }
  }
  rebuildCellStates();
  // 作り直すと、セルは新しくなる。単独表示は、いったん抜ける
  // (ファイルを開く、格子の変更、画像セットの編集)。
  const bool wasSolo = soloCell_ >= 0;
  soloCell_ = -1;
  rebuildGrid();
  if (wasSolo) {
    Q_EMIT soloCellChanged(-1);
  }
  // 表示する画像が変わる点はページ送りと同じ。有効なら最初の表示も合わせる。
  // ここで合わせないと、開いた直後だけ等倍の左上表示になり設定と食い違う。
  if (fitOnPageChange_) {
    fitPending_ = true;
    applyPendingFit();
  }
  updateStatus();
  Q_EMIT sessionChanged(true);
}

void ViewerGridWidget::setSessionPreservingView(const heiran::domain::SessionModel &session,
                                                int page, bool refit) {
  const bool layoutChanged = session_.viewer.layout.rows != session.viewer.layout.rows ||
                             session_.viewer.layout.cols != session.viewer.layout.cols ||
                             session_.viewer.layout.order != session.viewer.layout.order;
  if (layoutChanged || cells_.size() != session.viewer.layout.rows * session.viewer.layout.cols) {
    setSession(session, std::max(0, page));
    return;
  }

  session_ = session;
  if (page >= 0)
    globalState_.currentPage = std::clamp(page, 0, std::max(0, pageCount() - 1));
  refreshCellStatesFromSession();
  refreshCellImages();
  refreshCellViewStates();
  refreshSelectionOverlays();
  if (refit && fitOnPageChange_) {
    fitPending_ = true;
    applyPendingFit();
  }
  updateStatus();
  schedulePrefetchAroundCurrentPage();
  Q_EMIT sessionChanged(false);
  if (page >= 0) {
    Q_EMIT pageNavigationChanged();
    Q_EMIT viewChanged();
  }
}

void ViewerGridWidget::nextPage() {
  stepPages(1);
}

void ViewerGridWidget::previousPage() {
  stepPages(-1);
}

int ViewerGridWidget::navigationPageCount() const {
  return pageNavigation_.count(pageCount());
}
int ViewerGridWidget::navigationPageIndex() const {
  return pageNavigation_.indexOf(globalState_.currentPage, pageCount());
}
int ViewerGridWidget::navigationPageAt(int index) const {
  return pageNavigation_.pageAt(index, pageCount());
}
void ViewerGridWidget::stepPages(int offset) {
  if (navigationPageCount() <= 0) {
    return;
  }
  const qint64 target = qint64(navigationPageIndex()) + offset;
  setCurrentPage(navigationPageAt(
      static_cast<int>(std::clamp(target, qint64(0), qint64(navigationPageCount() - 1)))));
}
void ViewerGridWidget::setPageFilter(std::optional<QVector<int>> pages) {
  pageNavigation_.setPages(std::move(pages));
  setCurrentPage(globalState_.currentPage, true);
  Q_EMIT pageNavigationChanged();
}

void ViewerGridWidget::setCurrentPage(int pageIndex, bool forceRefresh) {
  const int totalPages = navigationPageCount();
  if (totalPages <= 0) {
    globalState_.currentPage = 0;
    refreshCellImages();
    refreshCellViewStates();
    updateStatus();
    schedulePrefetchAroundCurrentPage();
    Q_EMIT viewChanged();
    return;
  }

  const int clampedPage =
      pageNavigation_.pageAt(pageNavigation_.indexOf(pageIndex, pageCount()), pageCount());
  if (globalState_.currentPage == clampedPage && !forceRefresh) {
    return;
  }

  globalState_.currentPage = clampedPage;
  resetSubImage();
  if (fitOnPageChange_) {
    fitPending_ = true;
  }
  refreshCellImages();
  // 読み込み済みのページへ移るときは、読み終えの通知が来ない。ここで合わせる
  // (読み込み中のセルがあれば、待って、読み終えたときに合わせる)。
  applyPendingFit();
  refreshCellViewStates();
  updateStatus();
  schedulePrefetchAroundCurrentPage();
  Q_EMIT viewChanged();
}

void ViewerGridWidget::setColorLutOptions(const ColorLutOptions &options) {
  colorLutOptions_ = options;
  for (auto *cell : cells_) {
    cell->setColorLutOptions(colorLutOptions_);
  }
}

void ViewerGridWidget::setAnalysisValueSpace(AnalysisValueSpace valueSpace) {
  if (analysisValueSpace_ == valueSpace) {
    return;
  }

  analysisValueSpace_ = valueSpace;
  for (auto *cell : cells_) {
    cell->setAnalysisValueSpace(analysisValueSpace_);
  }
  // 解析ウィンドウへ作り直しを促す。表示は変わらない。
  Q_EMIT imageContentChanged();
}

void ViewerGridWidget::setDisplayProfile(DisplayProfileMode mode, const QString &profilePath) {
  displayProfileMode_ = mode;
  displayProfilePath_ = profilePath;
  for (auto *cell : cells_) {
    cell->setDisplayProfile(displayProfileMode_, displayProfilePath_);
  }
}

void ViewerGridWidget::setTransparencyBackground(const TransparencyBackground &background) {
  transparencyBackground_ = background;
  for (auto *cell : cells_) {
    cell->setTransparencyBackground(background);
  }
}

void ViewerGridWidget::setFilters(MagnifyFilter magnifyFilter, MinifyFilter minifyFilter) {
  magnifyFilter_ = magnifyFilter;
  minifyFilter_ = minifyFilter;
  for (auto *cell : cells_) {
    cell->setFilters(magnifyFilter_, minifyFilter_);
  }
}

void ViewerGridWidget::paintEvent(QPaintEvent *) {
  if (!cellBordersVisible_) {
    return;
  }
  QPainter painter(this);
  painter.fillRect(rect(), kCellGutterColor);
}

void ViewerGridWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  QMetaObject::invokeMethod(this, [this]() { applyPendingFit(); }, Qt::QueuedConnection);
}

void ViewerGridWidget::setCursorStyle(CursorStyle style) {
  cursorStyle_ = style;
  for (auto *cell : cells_) {
    cell->setCursorStyle(style);
  }
}

void ViewerGridWidget::setCellHeadersVisible(bool visible) {
  cellHeadersVisible_ = visible;
  for (auto *cell : cells_) {
    cell->setHeaderVisible(visible);
  }
}

void ViewerGridWidget::setCellBordersVisible(bool visible) {
  cellBordersVisible_ = visible;
  layout_->setSpacing(visible ? kCellGutterPixels : 0);
  update();
  for (auto *cell : cells_) {
    cell->setBordersVisible(visible);
  }
}

int ViewerGridWidget::decodeCacheEntryCount() const {
  return decodeCache_.entryCount();
}

int ViewerGridWidget::decodeCacheHitCount() const {
  return decodeCache_.hitCount();
}

int ViewerGridWidget::decodeCacheMissCount() const {
  return decodeCache_.missCount();
}

void ViewerGridWidget::resetDecodeCacheCounters() {
  decodeCache_.resetCounters();
}

qint64 ViewerGridWidget::decodeCacheUsedBytes() const {
  return decodeCache_.usedBytes();
}

qint64 ViewerGridWidget::decodeCacheBudgetBytes() const {
  return decodeCache_.budgetBytes();
}

QString ViewerGridWidget::currentFilePathForCell(int cellIndex) const {
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return {};
  }
  return cells_.at(cellIndex)->currentFilePath();
}

qint64 ViewerGridWidget::gpuTextureBytes() const {
  qint64 total = 0;
  for (const auto *cell : cells_) {
    total += cell->gpuTextureBytes();
  }
  return total;
}

qint64 ViewerGridWidget::gpuCachedTextureBytes() const {
  qint64 total = 0;
  for (const auto *cell : cells_) {
    total += cell->gpuCachedTextureBytes();
  }
  return total;
}

void ViewerGridWidget::setDecodeCacheBudgetBytes(qint64 budgetBytes) {
  decodeCache_.setBudgetBytes(budgetBytes);
}

void ViewerGridWidget::setFitOnPageChange(bool enabled) {
  if (fitOnPageChange_ == enabled) {
    return;
  }

  fitOnPageChange_ = enabled;
  if (fitOnPageChange_) {
    requestFitToCells();
  }
}

bool ViewerGridWidget::fitOnPageChange() const {
  return fitOnPageChange_;
}

void ViewerGridWidget::requestFitToCells() {
  fitPending_ = true;
  applyPendingFit();
}

// 画像は非同期で読み込まれるため、対象が揃うまで待ってから適用する。
void ViewerGridWidget::applyPendingFit() {
  if (!fitPending_ || cells_.isEmpty() || !isVisible()) {
    return;
  }

  std::vector<heiran::domain::FitTarget> targets;
  targets.reserve(static_cast<size_t>(cells_.size()));
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    if (view->cellStatus() == CellStatus::Loading) {
      // 読込中のセルがある間は、確定したサイズが分からないので待つ。
      return;
    }

    const QSize imageSize = view->currentImage().size();
    if (imageSize.isEmpty()) {
      continue;
    }

    const double cellScale = cellIndex < cellStates_.size() ? cellStates_.at(cellIndex).scale : 1.0;
    targets.push_back({.imageWidth = imageSize.width(),
                       .imageHeight = imageSize.height(),
                       .cellScale = cellScale});
  }

  const QSize cellSize = cells_.at(sizeReferenceCell())->canvasSize();
  const auto fitResult =
      heiran::domain::computeFitView(targets, cellSize.width(), cellSize.height());
  if (!fitResult.valid) {
    return;
  }

  fitPending_ = false;
  lastReferenceCanvasSize_ = cellSize;
  globalState_.baseScale = fitResult.baseScale;
  globalState_.baseOffsetX = fitResult.baseOffsetX;
  globalState_.baseOffsetY = fitResult.baseOffsetY;

  // セルごとのパン差分は保持する。位置合わせのために付けた差分を消さないため。
  refreshCellViewStates();
  updateStatus();
  Q_EMIT viewChanged();
}

int ViewerGridWidget::sizeReferenceCell() const {
  return soloCell_ >= 0 && soloCell_ < cells_.size() ? soloCell_ : 0;
}

bool ViewerGridWidget::isViewFitted() const {
  return !cells_.isEmpty() && isViewFitted(cells_.at(sizeReferenceCell())->canvasSize());
}

bool ViewerGridWidget::isViewFitted(const QSize &cellSize) const {
  if (cells_.isEmpty() || cellSize.isEmpty()) {
    return false;
  }
  std::vector<heiran::domain::FitTarget> targets;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const QSize imageSize = cells_.at(cellIndex)->currentImage().size();
    if (imageSize.isEmpty()) {
      continue;
    }
    const double cellScale = cellIndex < cellStates_.size() ? cellStates_.at(cellIndex).scale : 1.0;
    targets.push_back({.imageWidth = imageSize.width(),
                       .imageHeight = imageSize.height(),
                       .cellScale = cellScale});
  }
  const auto fit = heiran::domain::computeFitView(targets, cellSize.width(), cellSize.height());
  if (!fit.valid) {
    return false;
  }
  const auto close = [](double a, double b) {
    return std::abs(a - b) <= 1e-3 * std::max(1.0, std::abs(b));
  };
  return close(globalState_.baseScale, fit.baseScale) &&
         close(globalState_.baseOffsetX, fit.baseOffsetX) &&
         close(globalState_.baseOffsetY, fit.baseOffsetY);
}

void ViewerGridWidget::followCellResize() {
  if (cells_.isEmpty()) {
    return;
  }
  const QSize previous = lastReferenceCanvasSize_;
  lastReferenceCanvasSize_ = cells_.at(sizeReferenceCell())->canvasSize();
  if (previous.isEmpty() || previous == lastReferenceCanvasSize_) {
    return;
  }
  // 変わる前の大きさでフィットしていたなら、新しい大きさへフィットし直す。ズームやパンで動かした状態は、そのまま保つ。
  if (isViewFitted(previous)) {
    fitPending_ = true;
    QMetaObject::invokeMethod(this, [this]() { applyPendingFit(); }, Qt::QueuedConnection);
  }
}

int ViewerGridWidget::zoomAnchorCellIndex() const {
  if (activeCellIndex_ >= 0 && activeCellIndex_ < cellStates_.size()) {
    return activeCellIndex_;
  }
  return cellStates_.isEmpty() ? -1 : 0;
}

void ViewerGridWidget::zoomBy(double zoomFactor) {
  const int cellIndex = zoomAnchorCellIndex();
  if (cellIndex < 0 || zoomFactor <= 0.0 || cellIndex >= cells_.size()) {
    return;
  }

  const auto &cellState = cellStates_.at(cellIndex);
  const auto finalState = heiran::domain::computeFinalViewState(globalState_, cellState);
  if (finalState.scale <= 0.0) {
    return;
  }

  // 見えている中心を固定点にする。マウス位置が無い操作の基準として自然。
  const QSize canvasSize = cells_.at(cellIndex)->size();
  const double anchorImageX = heiran::domain::viewToImageX(finalState, canvasSize.width() / 2.0);
  const double anchorImageY = heiran::domain::viewToImageY(finalState, canvasSize.height() / 2.0);
  const double cellScale = cellState.scale > 0.0 ? cellState.scale : 1.0;
  handleZoomRequest(cellIndex, zoomFactor, anchorImageX * cellScale, anchorImageY * cellScale);
}

void ViewerGridWidget::setGlobalScale(double scale) {
  if (!std::isfinite(scale) || scale <= 0.0 || navigationPageCount() <= 0 ||
      !std::isfinite(globalState_.baseScale) || globalState_.baseScale <= 0.0)
    return;
  const double factor = scale / globalState_.baseScale;
  if (std::isfinite(factor) && factor > 0.0)
    zoomBy(factor);
}

void ViewerGridWidget::setActualSize() {
  const int cellIndex = zoomAnchorCellIndex();
  if (cellIndex < 0) {
    return;
  }

  const auto finalState =
      heiran::domain::computeFinalViewState(globalState_, cellStates_.at(cellIndex));
  if (finalState.scale <= 0.0) {
    return;
  }
  zoomBy(1.0 / finalState.scale);
}

void ViewerGridWidget::rotateActiveCellBy(int quarterTurns) {
  const int cellIndex = zoomAnchorCellIndex();
  if (cellIndex < 0) {
    return;
  }
  const int current = cellStates_.at(cellIndex).rotationQuarterTurns;
  handleCellRotationRequest(cellIndex,
                            heiran::domain::normalizeQuarterTurns(current + quarterTurns));
}

void ViewerGridWidget::rotateAllCellsBy(int quarterTurns) {
  const int delta = heiran::domain::normalizeQuarterTurns(quarterTurns);
  if (delta == 0 || cellStates_.isEmpty()) {
    return;
  }
  for (auto &cellState : cellStates_) {
    cellState.rotationQuarterTurns =
        heiran::domain::normalizeQuarterTurns(cellState.rotationQuarterTurns + delta);
    if (delta % 2 != 0) {
      std::swap(cellState.flipX, cellState.flipY);
    }
  }
  refreshAfterCellRotation();
}

void ViewerGridWidget::copyActiveCellImage() const {
  const int cellIndex = zoomAnchorCellIndex();
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return;
  }
  cells_.at(cellIndex)->copyImageToClipboard();
}

int ViewerGridWidget::subImageCount() const {
  int count = 1;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base != nullptr && base->ok) {
      count = std::max(count, base->decodedImage.subImageCount);
    }
  }
  return count;
}

bool ViewerGridWidget::hasAnimation() const {
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base != nullptr && base->ok && base->decodedImage.isAnimation()) {
      return true;
    }
  }
  return false;
}

heiran::infra::SubImageKind ViewerGridWidget::subImageKind() const {
  if (hasAnimation()) {
    return heiran::infra::SubImageKind::Frames;
  }
  return subImageCount() > 1 ? heiran::infra::SubImageKind::Pages
                             : heiran::infra::SubImageKind::None;
}

QString ViewerGridWidget::subImageStatusText() const {
  switch (subImageKind()) {
    case heiran::infra::SubImageKind::Frames: {
      // 対象のセルが動くならそのセル、そうでなければ最初に動くセルの番号を出す。
      // セルごとに長さも進みも違い得るため、代表を 1 つに決める。
      int cellIndex = targetCellIndex();
      const auto *base = baseResultForCell(cellIndex);
      if (base == nullptr || !base->decodedImage.isAnimation()) {
        cellIndex = -1;
        for (int candidate = 0; candidate < cells_.size(); ++candidate) {
          const auto *candidateBase = baseResultForCell(candidate);
          if (candidateBase != nullptr && candidateBase->decodedImage.isAnimation()) {
            cellIndex = candidate;
            base = candidateBase;
            break;
          }
        }
      }
      if (cellIndex < 0 || base == nullptr) {
        return {};
      }
      const int count = base->decodedImage.subImageCount;
      const int frame = cellIndex < cellFrame_.size() ? cellFrame_.at(cellIndex) : 0;
      return tr("Frame %1 / %2").arg(((frame % count) + count) % count + 1).arg(count);
    }
    case heiran::infra::SubImageKind::Pages: {
      const int count = subImageCount();
      return tr("Subpage %1 / %2").arg(std::min(subImageIndex_, count - 1) + 1).arg(count);
    }
    case heiran::infra::SubImageKind::None:
    default:
      return {};
  }
}

void ViewerGridWidget::resetSubImage() {
  subImageIndex_ = 0;
  cellFrame_.fill(0);
  stopAllPlaybackTimers();
}

void ViewerGridWidget::setSubImageIndex(int index) {
  const int count = subImageCount();
  if (count <= 1) {
    subImageIndex_ = 0;
    return;
  }

  // アニメーションは巻き戻り、ページは端で止まる。
  const int normalized =
      hasAnimation() ? ((index % count) + count) % count : std::clamp(index, 0, count - 1);
  // 全セルを同じ番号へそろえる。独立して再生した後は、セルごとに番号が違う。
  // 番号が同じでも、セルがずれていれば、そろえ直す。
  bool aligned = normalized == subImageIndex_;
  for (const int frame : std::as_const(cellFrame_)) {
    aligned = aligned && frame == normalized;
  }
  if (aligned) {
    return;
  }
  subImageIndex_ = normalized;
  cellFrame_.fill(normalized);
  applySubImageChange();
}

void ViewerGridWidget::stepSubImage(int delta) {
  // 独立して再生しているなら、番号は対象のセルのもの。そこを基準に、全セルをそろえて進める。
  int base = subImageIndex_;
  if (playsAnimationsIndependently()) {
    const int target = targetCellIndex();
    if (target >= 0 && target < cellFrame_.size()) {
      base = cellFrame_.at(target);
    }
  }
  setSubImageIndex(base + delta);
}

void ViewerGridWidget::setAnimationPlaying(bool playing) {
  animationPlaying_ = playing;
  if (!playing) {
    stopAllPlaybackTimers();
    return;
  }
  updatePlaybackTimer();
}

void ViewerGridWidget::setAnimationSyncMode(AnimationSyncMode mode) {
  if (animationSyncMode_ == mode) {
    return;
  }
  animationSyncMode_ = mode;
  updatePlaybackTimer();
}

bool ViewerGridWidget::animatedCellCountsDiffer() const {
  int firstCount = -1;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base == nullptr || !base->decodedImage.isAnimation()) {
      continue;
    }
    if (firstCount < 0) {
      firstCount = base->decodedImage.subImageCount;
    } else if (base->decodedImage.subImageCount != firstCount) {
      return true;
    }
  }
  return false;
}

bool ViewerGridWidget::playsAnimationsIndependently() const {
  switch (animationSyncMode_) {
    case AnimationSyncMode::Locked:
      return false;
    case AnimationSyncMode::Independent:
      return true;
    case AnimationSyncMode::Auto:
    default:
      // フレーム数が同じなら、同じ内容の別バージョンとみなして同期する。違えば、無関係な
      // アニメーションとみなして、それぞれの速さで動かす。
      return animatedCellCountsDiffer();
  }
}

void ViewerGridWidget::applySubImageChange() {
  decodeCache_.setRetainedKeys(currentPageCacheKeys());
  const bool showsPages = subImageKind() == heiran::infra::SubImageKind::Pages;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base != nullptr && base->ok && base->decodedImage.subImageCount > 1) {
      updateCellImage(cellIndex, false);
    }
  }
  // ページは大きさが変わることがある。フレームは同じ大きさで、フィットし直さない。
  if (showsPages && fitOnPageChange_) {
    fitPending_ = true;
    applyPendingFit();
  }
  updateStatus();
  Q_EMIT imageContentChanged();
}

void ViewerGridWidget::stopAllPlaybackTimers() {
  playbackTimer_.stop();
  for (auto *timer : std::as_const(cellTimers_)) {
    if (timer != nullptr) {
      timer->stop();
    }
  }
}

QTimer *ViewerGridWidget::timerForCell(int cellIndex) {
  if (cellTimers_.size() <= cellIndex) {
    cellTimers_.resize(cellIndex + 1);
  }
  if (cellTimers_.at(cellIndex) == nullptr) {
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, [this, cellIndex]() { advanceCell(cellIndex); });
    cellTimers_[cellIndex] = timer;
  }
  return cellTimers_.at(cellIndex);
}

// アニメーションがあるときだけ動かす。
void ViewerGridWidget::updatePlaybackTimer() {
  if (!animationPlaying_ || !hasAnimation()) {
    stopAllPlaybackTimers();
    return;
  }

  const bool independent = playsAnimationsIndependently();
  if (independent != independentActive_) {
    // 方式が切り替わった。同期へ移るなら、対象のセルの番号へ全セルをそろえる。
    // 独立へ移るなら、そろった番号から、それぞれが進み始める。
    stopAllPlaybackTimers();
    independentActive_ = independent;
    if (!independent) {
      const int target = targetCellIndex();
      if (target >= 0 && target < cellFrame_.size()) {
        subImageIndex_ = cellFrame_.at(target);
        cellFrame_.fill(subImageIndex_);
        applySubImageChange();
      }
    }
  }

  if (independent) {
    for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
      const auto *base = baseResultForCell(cellIndex);
      if (base != nullptr && base->decodedImage.isAnimation() &&
          !timerForCell(cellIndex)->isActive()) {
        scheduleCell(cellIndex);
      }
    }
    playbackTimer_.stop();
    return;
  }

  if (!playbackTimer_.isActive()) {
    scheduleNextFrame();
  }
}

// 同期して進めるときの、次のフレームまでの待ち。いま見せているフレームの表示時間で、
// フレームごとに違う。
void ViewerGridWidget::scheduleNextFrame() {
  constexpr int kMinimumDelayMs = 20;
  int delayMs = 100;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base != nullptr && base->ok && base->decodedImage.isAnimation()) {
      delayMs = heiran::infra::frameDelayMsOf(base->decodedImage, subImageIndex_);
      break;
    }
  }
  playbackTimer_.start(std::max(kMinimumDelayMs, delayMs));
}

void ViewerGridWidget::advanceFrame() {
  if (!animationPlaying_ || !hasAnimation()) {
    return;
  }

  // 次のフレームを、どのセルも作り終えていなければ、進めずに短く待って確かめ直す。
  // 進めてしまうと、作っていないフレームの分だけ絵が止まって、番号だけが先へ進む。
  constexpr int kStallRetryMs = 8;
  const int count = subImageCount();
  const int next = count > 0 ? (subImageIndex_ + 1) % count : 0;
  if (!prepareFrameForAllCells(next)) {
    playbackTimer_.start(kStallRetryMs);
    return;
  }
  setSubImageIndex(subImageIndex_ + 1);
  scheduleNextFrame();
}

// 1 つのセルの次のフレームまでの待ち。そのセルのフレームの表示時間で、他のセルとは無関係。
void ViewerGridWidget::scheduleCell(int cellIndex) {
  constexpr int kMinimumDelayMs = 20;
  const auto *base = baseResultForCell(cellIndex);
  if (base == nullptr || !base->decodedImage.isAnimation() || cellIndex >= cellFrame_.size()) {
    return;
  }
  const int delayMs = heiran::infra::frameDelayMsOf(base->decodedImage, cellFrame_.at(cellIndex));
  timerForCell(cellIndex)->start(std::max(kMinimumDelayMs, delayMs));
}

void ViewerGridWidget::advanceCell(int cellIndex) {
  if (!animationPlaying_ || !playsAnimationsIndependently()) {
    return;
  }
  const auto *base = baseResultForCell(cellIndex);
  if (base == nullptr || !base->decodedImage.isAnimation() || cellIndex >= cellFrame_.size()) {
    return;
  }

  // 作り終えていなければ進めず、短く待って確かめ直す。同期のときと同じ理由。
  constexpr int kStallRetryMs = 8;
  const int count = base->decodedImage.subImageCount;
  const int next = (cellFrame_.at(cellIndex) + 1) % count;
  if (const auto &stream = base->decodedImage.frameStream; stream != nullptr) {
    stream->request(next);
    if (stream->frameIfReady(next).isNull()) {
      timerForCell(cellIndex)->start(kStallRetryMs);
      return;
    }
  }

  cellFrame_[cellIndex] = next;
  updateCellImage(cellIndex, false);
  // 対象のセルの番号を、代表の番号として持つ。コマ送りの基準と、状態欄に使う。
  if (cellIndex == targetCellIndex()) {
    subImageIndex_ = next;
    updateStatus();
  }
  Q_EMIT imageContentChanged();
  scheduleCell(cellIndex);
}

bool ViewerGridWidget::prepareFrameForAllCells(int index) {
  bool allReady = true;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base == nullptr || base->decodedImage.frameStream == nullptr) {
      continue;
    }
    const auto &stream = base->decodedImage.frameStream;
    stream->request(index);
    if (stream->frameIfReady(index).isNull()) {
      allReady = false;
    }
  }
  return allReady;
}

void ViewerGridWidget::observeStream(int cellIndex,
                                     std::shared_ptr<heiran::infra::FrameStream> stream) {
  if (observedStreams_.size() != cells_.size()) {
    // セルの数が変わった。古い分の登録を外してから並べ直す。
    releaseObservedStreams();
    observedStreams_.resize(cells_.size());
  }
  if (cellIndex < 0 || cellIndex >= observedStreams_.size()) {
    return;
  }

  auto &current = observedStreams_[cellIndex];
  if (current == stream) {
    return;
  }
  if (current != nullptr) {
    current->removeListener(this);
  }
  current = std::move(stream);
  if (current != nullptr) {
    // 作る側のスレッドから呼ばれる。溜め込まないよう、未処理の通知があれば重ねない。
    current->setListener(this, [this]() {
      if (!streamNotifyPending_.exchange(true)) {
        QMetaObject::invokeMethod(
            this, [this]() { handleStreamFrameReady(); }, Qt::QueuedConnection);
      }
    });
  }
}

void ViewerGridWidget::releaseObservedStreams() {
  for (auto &stream : observedStreams_) {
    if (stream != nullptr) {
      stream->removeListener(this);
    }
  }
  observedStreams_.clear();
  cellShownFrame_.clear();
}

void ViewerGridWidget::handleStreamFrameReady() {
  streamNotifyPending_ = false;

  // 見せたいのに見せられずにいたフレームを、作り終えていれば見せ直す。
  bool updated = false;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto *base = baseResultForCell(cellIndex);
    if (base == nullptr || base->decodedImage.frameStream == nullptr ||
        cellIndex >= cellShownFrame_.size()) {
      continue;
    }
    const auto &stream = base->decodedImage.frameStream;
    const int frameCount = stream->frameCount();
    const int frameIndex = cellIndex < cellFrame_.size() ? cellFrame_.at(cellIndex) : 0;
    const int wrapped = ((frameIndex % frameCount) + frameCount) % frameCount;
    if (cellShownFrame_.at(cellIndex) != wrapped && !stream->frameIfReady(wrapped).isNull()) {
      updateCellImage(cellIndex, false);
      updated = true;
    }
  }
  if (updated) {
    Q_EMIT imageContentChanged();
  }
}

int ViewerGridWidget::targetCellIndex() const {
  return zoomAnchorCellIndex();
}

QString ViewerGridWidget::targetCellFilePath() const {
  return navigationPageCount() > 0 ? currentFilePathForCell(targetCellIndex()) : QString();
}

QImage ViewerGridWidget::targetCellImage() const {
  const int cellIndex = targetCellIndex();
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return {};
  }
  return cells_.at(cellIndex)->currentImage();
}

void ViewerGridWidget::resetView() {
  globalState_.baseScale = 1.0;
  globalState_.baseOffsetX = 0.0;
  globalState_.baseOffsetY = 0.0;

  for (auto &cellState : cellStates_) {
    cellState.offsetDeltaX = 0.0;
    cellState.offsetDeltaY = 0.0;
  }

  refreshCellViewStates();
  updateStatus();
  Q_EMIT viewChanged();
}

void ViewerGridWidget::applySoloLayout() {
  const int rows = session_.viewer.layout.rows;
  const int cols = session_.viewer.layout.cols;
  const bool columnFirst = session_.viewer.layout.order == "col-major";
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    layout_->removeWidget(view);
    if (soloCell_ < 0) {
      const int row = columnFirst ? (cellIndex % rows) : (cellIndex / cols);
      const int col = columnFirst ? (cellIndex / rows) : (cellIndex % cols);
      layout_->addWidget(view, row, col);
      view->show();
    } else if (cellIndex == soloCell_) {
      layout_->addWidget(view, 0, 0, rows, cols);
      view->show();
    } else {
      view->hide();
    }
  }
  layout_->invalidate();
  updateActiveFrames();
  update();
}

void ViewerGridWidget::setSoloCell(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    cellIndex = -1;
  }
  if (cellIndex == soloCell_) {
    return;
  }
  // フィットした状態で見ていたなら、広がった (縮んだ)
  // 面へ、フィットし直す。動かした状態なら、そのまま保つ
  // (切り替えても、同じ場所を同じ倍率で見るため)。フィットは、セルの大きさが定まったあとに行う。
  const bool wasFitted = isViewFitted();
  soloCell_ = cellIndex;
  applySoloLayout();
  clearHoverInfo();
  if (wasFitted) {
    fitPending_ = true;
    QMetaObject::invokeMethod(this, [this]() { applyPendingFit(); }, Qt::QueuedConnection);
  }
  Q_EMIT soloCellChanged(soloCell_);
  Q_EMIT viewChanged();
}

void ViewerGridWidget::cycleSoloCell(int direction) {
  const int count = cells_.size();
  if (count <= 0) {
    return;
  }
  // 空のセルは、見るものが無いので飛ばす。差分のセルは、見るものがある。
  const auto hasContent = [this](int cellIndex) {
    const auto config = heiran::domain::cellConfigAt(session_, cellIndex);
    return config.imageIndex >= 0 || config.diff.has_value();
  };
  const int step = direction < 0 ? -1 : 1;
  int candidate = soloCell_;
  if (candidate < 0) {
    candidate = activeCellIndex_ >= 0 && activeCellIndex_ < count ? activeCellIndex_ : 0;
    if (hasContent(candidate)) {
      setSoloCell(candidate);
      return;
    }
  }
  for (int tried = 0; tried < count; ++tried) {
    candidate = ((candidate + step) % count + count) % count;
    if (hasContent(candidate)) {
      setSoloCell(candidate);
      return;
    }
  }
}

void ViewerGridWidget::clearSelection() {
  selectionRect_.reset();
  previewSelectionRect_.reset();
  selectionReferenceCellIndex_ = -1;
  previewSelectionReferenceCellIndex_ = -1;
  refreshSelectionOverlays();
  Q_EMIT viewChanged();
}

heiran::domain::SessionModel ViewerGridWidget::sessionSnapshot() const {
  auto snapshot = session_;
  const int cellCount = std::min(snapshot.cells.size(), cellStates_.size());
  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    const auto &cellState = cellStates_.at(cellIndex);
    auto &cellConfig = snapshot.cells[cellIndex];
    heiran::domain::writeCellViewStateToConfig(cellState, &cellConfig);
    if (cellIndex < channelModes_.size()) {
      cellConfig.channel = channelModes_.at(cellIndex);
    }
  }
  return snapshot;
}

void ViewerGridWidget::rebuildCellStates() {
  cellStates_.clear();
  const int cellCount = session_.viewer.layout.rows * session_.viewer.layout.cols;
  cellStates_.resize(cellCount);
  channelModes_ = QVector<ChannelMode>(cellCount, ChannelMode::Color);

  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    auto &cellState = cellStates_[cellIndex];
    cellState.offsetDeltaX = 0.0;
    cellState.offsetDeltaY = 0.0;
    heiran::domain::applyCellConfigToViewState(session_, cellIndex, &cellState);
    if (cellIndex < channelModes_.size()) {
      channelModes_[cellIndex] = heiran::domain::cellConfigAt(session_, cellIndex).channel;
    }
  }
}

void ViewerGridWidget::refreshCellStatesFromSession() {
  const int cellCount = session_.viewer.layout.rows * session_.viewer.layout.cols;
  if (cellStates_.size() != cellCount) {
    rebuildCellStates();
    return;
  }

  if (channelModes_.size() != cellCount) {
    channelModes_.resize(cellCount);
  }

  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    heiran::domain::applyCellConfigToViewState(session_, cellIndex, &cellStates_[cellIndex]);
    channelModes_[cellIndex] = heiran::domain::cellConfigAt(session_, cellIndex).channel;
  }
}

void ViewerGridWidget::rebuildGrid() {
  cellSources_.clear();
  cellSourceSerial_.clear();
  diffStates_.clear();
  qDeleteAll(cells_);
  cells_.clear();

  while (QLayoutItem *item = layout_->takeAt(0)) {
    delete item;
  }
  // 格子を減らしたとき、前の格子の行と列の伸び率が、残る。空の列や行が、場所を取り続けて、セルが広がらなくなる。
  // 項目を外しても、QGridLayout は、伸び率と最小の大きさを覚えているため、全部を戻す。
  for (int index = 0; index < layout_->columnCount(); ++index) {
    layout_->setColumnStretch(index, 0);
    layout_->setColumnMinimumWidth(index, 0);
  }
  for (int index = 0; index < layout_->rowCount(); ++index) {
    layout_->setRowStretch(index, 0);
    layout_->setRowMinimumHeight(index, 0);
  }

  const int rows = session_.viewer.layout.rows;
  const int cols = session_.viewer.layout.cols;
  const int cellCount = rows * cols;
  const bool columnFirst = session_.viewer.layout.order == "col-major";

  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    auto *view = new ImageViewWidget(this);
    connect(view, &ImageViewWidget::cellResized, this, [this, view]() {
      if (!cells_.isEmpty() && cells_.at(sizeReferenceCell()) == view) {
        followCellResize();
      }
    });
    view->setPixelValueFormat(pixelValueFormat_);
    view->setExposureStops(exposureStops_);
    view->setFilters(magnifyFilter_, minifyFilter_);
    view->setTransparencyBackground(transparencyBackground_);
    connect(view, &ImageViewWidget::favoriteToggleRequested, this,
            &ViewerGridWidget::favoriteToggleRequested);
    connect(view, &ImageViewWidget::transparencyPatternRequested, this,
            &ViewerGridWidget::transparencyPatternRequested);
    view->setBordersVisible(cellBordersVisible_);
    view->setHeaderVisible(cellHeadersVisible_);
    view->setCursorStyle(cursorStyle_);
    view->setDisplayProfile(displayProfileMode_, displayProfilePath_);
    view->setAnalysisValueSpace(analysisValueSpace_);
    view->setColorLutOptions(colorLutOptions_);
    connect(view, &ImageViewWidget::panRequested, this,
            [this](int requestedCellIndex, double deltaImageX, double deltaImageY) {
              handlePanRequest(requestedCellIndex, deltaImageX, deltaImageY);
            });
    connect(view, &ImageViewWidget::zoomRequested, this,
            [this](int requestedCellIndex, double zoomFactor, double anchorImageX,
                   double anchorImageY) {
              handleZoomRequest(requestedCellIndex, zoomFactor, anchorImageX, anchorImageY);
            });
    connect(view, &ImageViewWidget::pageScrollRequested, this,
            [this](int offset) { stepPages(offset); });
    connect(view, &ImageViewWidget::actualSizeToggleRequested, this,
            [this](int requestedCellIndex, double anchorImageX, double anchorImageY) {
              handleActualSizeToggleRequest(requestedCellIndex, anchorImageX, anchorImageY);
            });
    connect(view, &ImageViewWidget::hoverInfoRequested, this,
            [this](int requestedCellIndex, const QPointF &imagePosition, bool active) {
              handleHoverInfo(requestedCellIndex, imagePosition, active);
            });
    connect(view, &ImageViewWidget::channelContextMenuRequested, this,
            [this](int requestedCellIndex, ChannelMode channelMode, const QPoint &) {
              setChannelModeForCell(requestedCellIndex, channelMode);
            });
    connect(view, &ImageViewWidget::syncToggleRequested, this,
            [this](int requestedCellIndex) { handleSyncToggleRequest(requestedCellIndex); });
    connect(view, &ImageViewWidget::scaleContextMenuRequested, this,
            [this](int requestedCellIndex, double scale) {
              handleCellScaleRequest(requestedCellIndex, scale);
            });
    connect(view, &ImageViewWidget::gammaContextMenuRequested, this,
            [this](int requestedCellIndex, double gamma) {
              handleCellGammaRequest(requestedCellIndex, gamma);
            });
    connect(view, &ImageViewWidget::reloadRequested, this,
            [this](int requestedCellIndex) { handleReloadRequest(requestedCellIndex); });
    connect(view, &ImageViewWidget::invertContextMenuRequested, this,
            [this](int requestedCellIndex, bool invert) {
              handleCellInvertRequest(requestedCellIndex, invert);
            });
    connect(view, &ImageViewWidget::rotationContextMenuRequested, this,
            [this](int requestedCellIndex, int rotationQuarterTurns) {
              handleCellRotationRequest(requestedCellIndex, rotationQuarterTurns);
            });
    connect(view, &ImageViewWidget::flipContextMenuRequested, this,
            [this](int requestedCellIndex, bool flipX, bool flipY) {
              handleCellFlipRequest(requestedCellIndex, flipX, flipY);
            });
    connect(view, &ImageViewWidget::labelVisibilityRequested, this,
            [this](int requestedCellIndex, bool visible) {
              Q_EMIT labelVisibilityRequested(requestedCellIndex, visible);
            });
    connect(view, &ImageViewWidget::editLabelRequested, this,
            [this](int requestedCellIndex) { Q_EMIT editLabelRequested(requestedCellIndex); });
    connect(view, &ImageViewWidget::diffChangeRequested, this,
            [this](int requestedCellIndex, const heiran::domain::CellDiff &diff) {
              setCellDiff(requestedCellIndex, diff);
            });
    connect(view, &ImageViewWidget::diffStopRequested, this,
            [this](int requestedCellIndex) { setCellDiff(requestedCellIndex, std::nullopt); });
    connect(view, &ImageViewWidget::wipeStartRequested, this,
            [this](int requestedCellIndex, int baseCell) {
              if (!showDifference(requestedCellIndex, baseCell, heiran::domain::DiffMode::Wipe)) {
                Q_EMIT statusChanged(tr("A wipe needs two cells with images."));
              }
            });
    connect(view, &ImageViewWidget::diffStartRequested, this,
            [this](int requestedCellIndex, int baseCell) {
              if (!showDifference(requestedCellIndex, baseCell)) {
                Q_EMIT statusChanged(tr("A difference needs two cells with images."));
              }
            });
    connect(view, &ImageViewWidget::assignImageIndexRequested, this,
            [this](int requestedCellIndex, int imageIndex) {
              Q_EMIT assignImageIndexRequested(requestedCellIndex, imageIndex);
            });
    connect(view, &ImageViewWidget::resetCellDisplayRequested, this,
            [this](int requestedCellIndex) { handleResetCellDisplayRequest(requestedCellIndex); });
    connect(view, &ImageViewWidget::dropItemsRequested, this,
            [this](int requestedCellIndex, const QStringList &paths) {
              Q_EMIT dropItemsRequested(requestedCellIndex, paths);
            });
    connect(view, &ImageViewWidget::selectionRequested, this,
            [this](int requestedCellIndex, const QPointF &startImagePosition,
                   const QPointF &endImagePosition, bool final) {
              handleSelectionRequest(requestedCellIndex, startImagePosition, endImagePosition,
                                     final);
            });
    view->setChannelMode(cellIndex < channelModes_.size() ? channelModes_.at(cellIndex)
                                                          : ChannelMode::Color);

    const int row = columnFirst ? (cellIndex % rows) : (cellIndex / cols);
    const int col = columnFirst ? (cellIndex / rows) : (cellIndex % cols);
    layout_->addWidget(view, row, col);
    cells_.push_back(view);
  }
  // どのセルも同じ大きさにする。差分の帯のように、中身の希望の大きさが違っても揃える。
  for (int row = 0; row < rows; ++row) {
    layout_->setRowStretch(row, 1);
  }
  for (int col = 0; col < cols; ++col) {
    layout_->setColumnStretch(col, 1);
  }

  refreshCellImages();
  refreshCellViewStates();
  refreshSelectionOverlays();
  schedulePrefetchAroundCurrentPage();
}

QSet<QString> ViewerGridWidget::cacheKeysForPage(int page) const {
  QSet<QString> cacheKeys;
  for (int cellIndex = 0; cellIndex < session_.cells.size(); ++cellIndex) {
    const auto cellPage = heiran::domain::cellPageAt(session_, cellIndex, page);
    if (!cellPage.hasFile()) {
      continue;
    }
    cacheKeys.insert(DecodeCache::makeKey(cellPage.path, cellPage.imageSet->honorExifOrientation));
  }

  return cacheKeys;
}

QSet<QString> ViewerGridWidget::currentPageCacheKeys() const {
  QSet<QString> cacheKeys = cacheKeysForPage(globalState_.currentPage);
  // ページ (TIFF など) を見ている間は、そのページの結果も残す。0 ページ目だけ残すと、
  // 見ているページが予算の都合で先に捨てられる。
  if (subImageIndex_ > 0) {
    for (int cellIndex = 0; cellIndex < session_.cells.size(); ++cellIndex) {
      const auto cellPage =
          heiran::domain::cellPageAt(session_, cellIndex, globalState_.currentPage);
      if (cellPage.hasFile()) {
        cacheKeys.insert(DecodeCache::makeKey(
            cellPage.path, cellPage.imageSet->honorExifOrientation, subImageIndex_));
      }
    }
  }
  return cacheKeys;
}

QSet<QString> ViewerGridWidget::analysisWindowCacheKeys() const {
  QSet<QString> cacheKeys;
  for (int offset = -kAnalysisWindowRadiusPages; offset <= kAnalysisWindowRadiusPages; ++offset) {
    cacheKeys.unite(cacheKeysForPage(globalState_.currentPage + offset));
  }
  return cacheKeys;
}

void ViewerGridWidget::setPixelValueFormat(PixelValueFormat pixelValueFormat) {
  if (pixelValueFormat_ == pixelValueFormat) {
    return;
  }

  pixelValueFormat_ = pixelValueFormat;
  for (auto *cell : cells_) {
    cell->setPixelValueFormat(pixelValueFormat_);
  }
  if (hoverCursorPosition_ && hoverReferenceCellIndex_ >= 0) {
    handleHoverInfo(hoverReferenceCellIndex_, *hoverCursorPosition_, true);
  }
}

void ViewerGridWidget::setExposureStops(double exposureStops) {
  if (std::abs(exposureStops_ - exposureStops) < 0.0001) {
    return;
  }

  exposureStops_ = exposureStops;
  for (auto *cell : cells_) {
    cell->setExposureStops(exposureStops_);
  }
  Q_EMIT imageContentChanged();
}

double ViewerGridWidget::exposureStops() const {
  return exposureStops_;
}

PixelValueFormat ViewerGridWidget::pixelValueFormat() const {
  return pixelValueFormat_;
}

void ViewerGridWidget::refreshCellImages() {
  decodeCache_.setRetainedKeys(currentPageCacheKeys());
  decodeCache_.setAnalysisWindowKeys(analysisWindowCacheKeys());

  QStringList imageNames;
  imageNames.reserve(session_.images.size());
  for (const auto &imageSet : session_.images) {
    imageNames.push_back(imageSet.name);
  }

  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    view->setAvailableImageNames(imageNames);
    const auto cellConfig = heiran::domain::cellConfigAt(session_, cellIndex);

    // ICC ファイルの相対経路は `.hrn` のある場所を基準にする。
    view->setSessionDirectory(session_.sessionDirectory);
    view->setCellInfo(heiran::domain::imageSetForCell(session_, cellIndex), cellConfig.imageIndex,
                      cellIndex);
    updateCellImage(cellIndex, true);
    updateDiffUi(cellIndex);
  }
  updatePlaybackTimer();
}

const heiran::infra::DecodeResult *ViewerGridWidget::baseResultForCell(int cellIndex) const {
  if (cellIndex < 0 || cellIndex >= cellBaseResults_.size()) {
    return nullptr;
  }
  const auto &base = cellBaseResults_.at(cellIndex);
  return base.ok ? &base : nullptr;
}

void ViewerGridWidget::updateCellImage(int cellIndex, bool countCacheAccess) {
  auto *view = cells_.at(cellIndex);
  const heiran::domain::ImageSet *imageSet = heiran::domain::imageSetForCell(session_, cellIndex);

  // 見せられるものが決まるまでは、持たない。読み込み中や失敗のセルを動かさない。
  if (cellBaseResults_.size() != cells_.size()) {
    cellBaseResults_.resize(cells_.size());
  }
  cellBaseResults_[cellIndex] = {};
  if (cellShownFrame_.size() != cells_.size()) {
    cellShownFrame_.fill(-1, cells_.size());
  }
  if (cellFrame_.size() != cells_.size()) {
    cellFrame_.fill(0, cells_.size());
  }
  cellShownFrame_[cellIndex] = -1;
  if (cellSources_.size() != cells_.size()) {
    cellSources_.resize(cells_.size());
    cellSourceSerial_.fill(0, cells_.size());
  }
  if (!cellSources_[cellIndex].displayImage.isNull()) {
    cellSources_[cellIndex] = {};
    ++cellSourceSerial_[cellIndex];
  }
  // 差分のセルは、自分で絵を出さない (差分が出す)。読み込み中や失敗の表示だけは通常どおり。
  const bool showsDiff = heiran::domain::cellConfigAt(session_, cellIndex).diff.has_value();
  // ソースが替わったら、差分を作り直す。
  const auto scheduleDiff = qScopeGuard([&]() { scheduleDiffRefresh(); });
  // 見せられるものが決まるまで、作り終えの通知は受けない。決まった経路でだけ受け直す。
  std::shared_ptr<heiran::infra::FrameStream> streamToObserve;
  const auto applyObservation =
      qScopeGuard([&]() { observeStream(cellIndex, std::move(streamToObserve)); });

  if (navigationPageCount() == 0) {
    view->setDecodedImage({}, {}, CellStatusInfo::empty(tr("No images to display.")));
    return;
  }
  if (imageSet == nullptr) {
    if (!showsDiff) {
      view->setDecodedImage({}, QString(), CellStatusInfo::empty(tr("Drop an image here.")));
    }
    return;
  }
  if (imageSet->resolvedPaths.isEmpty()) {
    view->setDecodedImage({}, QString(),
                          CellStatusInfo::empty(tr("No files matched the path pattern.")));
    return;
  }

  const QString sourcePath =
      heiran::domain::pagePathInImageSet(*imageSet, globalState_.currentPage);
  if (sourcePath.isEmpty()) {
    view->setDecodedImage({}, QString(),
                          CellStatusInfo::empty(tr("This image set has no image on this page.")));
    return;
  }

  const bool honorExif = imageSet->honorExifOrientation;
  const ChannelMode channelMode =
      cellIndex < channelModes_.size() ? channelModes_.at(cellIndex) : ChannelMode::Color;

  if (!decodeCache_.contains(sourcePath, honorExif)) {
    // 非同期で読みに行く経路。ここも引けなかった回数として数える。
    // `decodeImageCached` の中だけで数えると、この分岐が入らず 0 ミスに見える。
    if (countCacheAccess) {
      decodeCache_.recordMiss();
    }
    requestDecodeAsync(sourcePath, honorExif, false);
    view->setDecodedImage({}, sourcePath, CellStatusInfo::loading());
    view->setChannelMode(channelMode);
    return;
  }

  const auto baseResult = countCacheAccess
                              ? decodeImageCached(sourcePath, honorExif)
                              : *decodeCache_.peek(DecodeCache::makeKey(sourcePath, honorExif));
  if (!baseResult.ok) {
    view->setDecodedImage(
        {}, sourcePath,
        CellStatusInfo::failed(QFileInfo(sourcePath).fileName(), baseResult.errorMessage));
    view->setChannelMode(channelMode);
    return;
  }

  cellBaseResults_[cellIndex] = baseResult;

  // ファイルが複数の画像を持つなら、全セルで共有する番号の 1 枚を見せる。
  const auto &baseImage = baseResult.decodedImage;
  heiran::infra::DecodedImage shown = baseImage;
  if (baseImage.subImageKind == heiran::infra::SubImageKind::Frames &&
      baseImage.frameStream != nullptr) {
    // 後から作るアニメーション。見せたいフレームがまだなら、作らせて、いまの絵を見せたままにする。
    // 作り終えたら通知が来て、handleStreamFrameReady が見せ直す。
    streamToObserve = baseImage.frameStream;
    const int frameIndex = cellFrame_.at(cellIndex);
    baseImage.frameStream->request(frameIndex);
    const int frameCount = baseImage.frameStream->frameCount();
    const int wrapped = ((frameIndex % frameCount) + frameCount) % frameCount;
    if (auto frame = heiran::infra::tryFrameOf(baseImage, frameIndex)) {
      shown = *frame;
    } else if (wrapped == 0) {
      // 先頭は結果そのものが持っている。窓から落ちていても、これは見せられる。
      shown = baseImage;
    } else {
      view->setChannelMode(channelMode);
      return;
    }
    cellShownFrame_[cellIndex] = wrapped;
  } else if (baseImage.subImageKind == heiran::infra::SubImageKind::Frames) {
    shown = heiran::infra::frameOf(baseImage, cellFrame_.at(cellIndex));
  } else if (baseImage.subImageKind == heiran::infra::SubImageKind::Pages) {
    // ページは個数を超えたら最後のページで止める。長さの違うファイルを並べても足りない側が
    // 空になるより、最後のページを見せ続ける方が比べやすい。
    const int page = std::clamp(subImageIndex_, 0, baseImage.subImageCount - 1);
    if (page > 0) {
      const QString pageKey = DecodeCache::makeKey(sourcePath, honorExif, page);
      const auto *pageResult = decodeCache_.peek(pageKey);
      if (pageResult == nullptr) {
        requestDecodeAsync(sourcePath, honorExif, false, page);
        view->setDecodedImage({}, sourcePath, CellStatusInfo::loading());
        view->setChannelMode(channelMode);
        return;
      }
      if (!pageResult->ok) {
        view->setDecodedImage(
            {}, sourcePath,
            CellStatusInfo::failed(QFileInfo(sourcePath).fileName(), pageResult->errorMessage));
        view->setChannelMode(channelMode);
        return;
      }
      shown = pageResult->decodedImage;
    }
  }

  cellSources_[cellIndex] = shown;
  ++cellSourceSerial_[cellIndex];
  if (!showsDiff) {
    view->setDecodedImage(shown, sourcePath, CellStatusInfo::ready());
  }
  view->setChannelMode(channelMode);
}

void ViewerGridWidget::clearDecodeCache() {
  decodeCache_.clear();
  decodeScheduler_->reset();
  prefetchScheduled_ = false;
}

heiran::infra::DecodeResult ViewerGridWidget::decodeImageCached(const QString &filePath,
                                                                bool honorExifOrientation) {
  // `value()` が引けた回数と引けなかった回数を数える。ここで containsKey を
  // 先に見ると数に入らないため、引きに行ってから判断する。
  const QString cacheKey = DecodeCache::makeKey(filePath, honorExifOrientation);
  const auto cached = decodeCache_.value(filePath, honorExifOrientation);
  if (cached.ok) {
    return cached;
  }

  const auto result = imageDecoder_.decode(filePath, honorExifOrientation);
  decodeCache_.insert(cacheKey, result);
  return result;
}

void ViewerGridWidget::requestDecodeAsync(const QString &filePath, bool honorExifOrientation,
                                          bool prefetchRequest, int subImageIndex) {
  if (filePath.isEmpty() || decodeCache_.contains(filePath, honorExifOrientation, subImageIndex)) {
    return;
  }
  decodeScheduler_->request(filePath, honorExifOrientation, prefetchRequest, subImageIndex);
}

bool ViewerGridWidget::currentPageUsesFile(const QString &filePath,
                                           bool honorExifOrientation) const {
  if (navigationPageCount() == 0) {
    return false;
  }
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto cellPage = heiran::domain::cellPageAt(session_, cellIndex, globalState_.currentPage);
    if (cellPage.hasFile() && cellPage.path == filePath &&
        cellPage.imageSet->honorExifOrientation == honorExifOrientation) {
      return true;
    }
  }

  return false;
}

void ViewerGridWidget::applyDecodeResultToCurrentPage(const QString &filePath,
                                                      bool honorExifOrientation,
                                                      const heiran::infra::DecodeResult &result) {
  if (decodeBackendTraceEnabled() && result.ok) {
    qDebug().noquote() << QString("[HeiranDecode] backend=%1 exif=%2 file=%3")
                              .arg(result.decodedImage.decoderBackend,
                                   honorExifOrientation ? QStringLiteral("on")
                                                        : QStringLiteral("off"),
                                   filePath);
  }

  // 結果はキャッシュへ入れてある。表示の組み立ては、ページ替えと同じ経路を通す。
  // ページ (TIFF など) の結果が届いた場合や、アニメーションの先頭が届いてから
  // 見せるページを選ぶ場合も、そこで面倒を見る。
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto cellPage = heiran::domain::cellPageAt(session_, cellIndex, globalState_.currentPage);
    if (!cellPage.hasFile() || cellPage.path != filePath ||
        cellPage.imageSet->honorExifOrientation != honorExifOrientation) {
      continue;
    }
    updateCellImage(cellIndex, false);
  }
  updatePlaybackTimer();
}

void ViewerGridWidget::schedulePrefetchAroundCurrentPage() {
  if (prefetchScheduled_) {
    return;
  }

  prefetchScheduled_ = true;
  QMetaObject::invokeMethod(
      this,
      [this]() {
        prefetchScheduled_ = false;
        prefetchAroundCurrentPage();
      },
      Qt::QueuedConnection);
}

// 現在ページの結果から、この画像セットが解析用バッファを伴うかを見る。
// 現在ページは必ず読み込まれ保持されているため、別に状態を持たなくても判定できる。
bool ViewerGridWidget::imageSetNeedsAnalysisBuffer(const heiran::domain::ImageSet &imageSet) const {
  const QString path = heiran::domain::pagePathInImageSet(imageSet, globalState_.currentPage);
  if (path.isEmpty()) {
    return false;
  }

  return decodeCache_.hasAnalysisBuffer(DecodeCache::makeKey(path, imageSet.honorExifOrientation));
}

// 予算に収まる範囲までしか先読みしない。入りきらない分を読んでも、届いたそば
// から捨てられて無駄になる。どこまで、どの順で読むかは `PrefetchPlanner` にある。
void ViewerGridWidget::prefetchAroundCurrentPage() {
  // アニメーションを見せている間は先読みしない。1 つが数百 MB になり、隣のファイルを
  // 読んでは捨てる無駄になる。ページを送れば、その時点で読む。
  if (hasAnimation()) {
    return;
  }
  // 読み込み中のセルがある間も待つ。アニメーションかは、読み終わるまで分からない。
  // 読み終えたときに先読みを予約し直す (handleDecodeCompleted)。
  for (const auto *view : cells_) {
    if (view->cellStatus() == CellStatus::Loading) {
      return;
    }
  }

  const int radius = prefetchRadiusForBudget(
      decodeCache_.entryCount(), decodeCache_.usedBytes(), decodeCache_.budgetBytes(),
      static_cast<int>(session_.images.size()), kPrefetchRadiusPages);

  for (const int position :
       prefetchPageOrder(navigationPageIndex(), navigationPageCount(), radius)) {
    const int targetPage = navigationPageAt(position);
    for (const auto &imageSet : session_.images) {
      const QString path = heiran::domain::pagePathInImageSet(imageSet, targetPage);
      if (path.isEmpty()) {
        continue;
      }

      requestDecodeAsync(path, imageSet.honorExifOrientation, true);
    }
  }
}

void ViewerGridWidget::refreshCellViewStates() {
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    const auto cellState = cellIndex < cellStates_.size() ? cellStates_.at(cellIndex)
                                                          : heiran::domain::CellViewState{};
    view->setViewState(globalState_, cellState);
  }
}

int ViewerGridWidget::currentPage() const {
  return globalState_.currentPage;
}

int ViewerGridWidget::totalPages() const {
  return pageCount();
}

QVector<AnalysisImage> ViewerGridWidget::currentAnalysisImages() const {
  QVector<AnalysisImage> images;
  images.reserve(cells_.size());
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    const std::optional<QRect> selectionRect =
        selectionRect_ && selectionReferenceCellIndex_ >= 0
            ? heiran::domain::clipSelectionRect(
                  mapImageRectToCell(*selectionRect_, selectionReferenceCellIndex_, cellIndex),
                  view->currentImage().size())
            : std::optional<QRect>{};
    images.push_back({.label = view->displayLabel(),
                      .image = view->currentImage(),
                      .selectionRect = selectionRect,
                      .sampler = view->analysisSampler(),
                      .filePath = view->currentFilePath()});
  }
  return images;
}

bool ViewerGridWidget::hasCells() const {
  return !cells_.isEmpty();
}

std::optional<ChannelMode> ViewerGridWidget::uniformChannelMode() const {
  if (channelModes_.isEmpty()) {
    return std::nullopt;
  }

  const ChannelMode firstMode = channelModes_.first();
  for (const auto channelMode : channelModes_) {
    if (channelMode != firstMode) {
      return std::nullopt;
    }
  }

  return firstMode;
}

int ViewerGridWidget::activeCellIndex() const {
  return activeCellIndex_;
}

ChannelMode ViewerGridWidget::activeCellChannelMode() const {
  if (activeCellIndex_ < 0 || activeCellIndex_ >= channelModes_.size()) {
    return ChannelMode::Color;
  }
  return channelModes_.at(activeCellIndex_);
}

void ViewerGridWidget::setChannelModeForActiveCell(ChannelMode channelMode) {
  if (activeCellIndex_ < 0 || activeCellIndex_ >= cells_.size() ||
      activeCellIndex_ >= channelModes_.size()) {
    return;
  }

  channelModes_[activeCellIndex_] = channelMode;
  cells_.at(activeCellIndex_)->setChannelMode(channelMode);
  updateStatus();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::setChannelModeForCell(int cellIndex, ChannelMode channelMode) {
  if (cellIndex < 0 || cellIndex >= cells_.size() || cellIndex >= channelModes_.size()) {
    return;
  }

  setActiveCell(cellIndex);
  channelModes_[cellIndex] = channelMode;
  cells_.at(cellIndex)->setChannelMode(channelMode);
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::setChannelModeForAllCells(ChannelMode channelMode) {
  if (cells_.isEmpty()) {
    return;
  }

  for (int cellIndex = 0; cellIndex < cells_.size() && cellIndex < channelModes_.size();
       ++cellIndex) {
    channelModes_[cellIndex] = channelMode;
    cells_.at(cellIndex)->setChannelMode(channelMode);
  }

  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

int ViewerGridWidget::pageCount() const {
  return heiran::domain::totalPageCount(session_);
}

void ViewerGridWidget::updateStatus() {
  const int totalPages = navigationPageCount();
  if (totalPages <= 0) {
    Q_EMIT statusChanged(tr("No images loaded"));
    return;
  }

  const int pageNumber = navigationPageIndex() + 1;
  QString status = tr("Page %1 / %2").arg(pageNumber).arg(totalPages);
  const QString subImageText = subImageStatusText();
  if (!subImageText.isEmpty()) {
    status += QString("  %1").arg(subImageText);
  }
  status += tr("  Zoom %1%").arg(globalState_.baseScale * 100.0, 0, 'f', 2);
  if (!hoverInfoSummary_.isEmpty()) {
    status += QString("  |  %1").arg(hoverInfoSummary_);
  }
  if (memoryTraceEnabled()) {
    const auto toMebibytes = [](qint64 bytes) {
      return static_cast<double>(bytes) / (1024.0 * 1024.0);
    };
    status += tr("  |  cache %1 / %2 MiB  tex %3 MiB")
                  .arg(toMebibytes(decodeCacheUsedBytes()), 0, 'f', 1)
                  .arg(toMebibytes(decodeCacheBudgetBytes()), 0, 'f', 0)
                  .arg(toMebibytes(gpuTextureBytes()), 0, 'f', 1);
  }

  Q_EMIT statusChanged(status);
}

void ViewerGridWidget::notifyPersistentStateChanged() {
  Q_EMIT persistentStateChanged();
}

void ViewerGridWidget::handleSyncToggleRequest(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  setActiveCell(cellIndex);
  cellStates_[cellIndex].syncEnabled = !cellStates_[cellIndex].syncEnabled;
  refreshCellViewStates();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handleCellScaleRequest(int cellIndex, double scale) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size() || scale <= 0.0) {
    return;
  }

  setActiveCell(cellIndex);
  cellStates_[cellIndex].scale = scale;
  refreshCellViewStates();
  refreshSelectionOverlays();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handleReloadRequest(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= session_.cells.size()) {
    return;
  }

  const auto &cellConfig = session_.cells.at(cellIndex);
  if (cellConfig.imageIndex < 0 || cellConfig.imageIndex >= session_.images.size()) {
    return;
  }

  const auto &imageSet = session_.images.at(cellConfig.imageIndex);
  const int localPageIndex = globalState_.currentPage - imageSet.pageOffset;
  if (localPageIndex < 0 || localPageIndex >= imageSet.resolvedPaths.size()) {
    return;
  }

  // 失敗した結果がキャッシュに残っていると再試行できないため、取り除いてから要求する。
  const QString &sourcePath = imageSet.resolvedPaths.at(localPageIndex);
  decodeCache_.remove(DecodeCache::makeKey(sourcePath, imageSet.honorExifOrientation));
  if (subImageIndex_ > 0) {
    decodeCache_.remove(
        DecodeCache::makeKey(sourcePath, imageSet.honorExifOrientation, subImageIndex_));
  }
  cells_.at(cellIndex)->setDecodedImage({}, sourcePath, CellStatusInfo::loading());
  requestDecodeAsync(sourcePath, imageSet.honorExifOrientation, false);
}

void ViewerGridWidget::handleCellInvertRequest(int cellIndex, bool invert) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  cellStates_[cellIndex].invert = invert;
  refreshCellViewStates();
  notifyPersistentStateChanged();
  Q_EMIT imageContentChanged();
}

void ViewerGridWidget::setInvertForAllCells(bool invert) {
  for (auto &cellState : cellStates_) {
    cellState.invert = invert;
  }
  refreshCellViewStates();
  notifyPersistentStateChanged();
  Q_EMIT imageContentChanged();
}

bool ViewerGridWidget::allCellsInverted() const {
  if (cellStates_.isEmpty()) {
    return false;
  }
  for (const auto &cellState : cellStates_) {
    if (!cellState.invert) {
      return false;
    }
  }
  return true;
}

void ViewerGridWidget::handleCellGammaRequest(int cellIndex, double gamma) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size() || gamma <= 0.0) {
    return;
  }

  setActiveCell(cellIndex);
  cellStates_[cellIndex].gamma = gamma;
  refreshCellViewStates();
  refreshSelectionOverlays();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handleCellRotationRequest(int cellIndex, int rotationQuarterTurns) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  setActiveCell(cellIndex);
  cellStates_[cellIndex].rotationQuarterTurns = rotationQuarterTurns;
  refreshAfterCellRotation();
}

void ViewerGridWidget::refreshAfterCellRotation() {
  refreshCellViewStates();
  refreshSelectionOverlays();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handleCellFlipRequest(int cellIndex, bool flipX, bool flipY) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  setActiveCell(cellIndex);
  cellStates_[cellIndex].flipX = flipX;
  cellStates_[cellIndex].flipY = flipY;
  refreshCellViewStates();
  refreshSelectionOverlays();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handleResetCellDisplayRequest(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  setActiveCell(cellIndex);
  auto &cellState = cellStates_[cellIndex];
  cellState.offsetDeltaX = 0.0;
  cellState.offsetDeltaY = 0.0;
  heiran::domain::applyCellConfigDisplayFields(heiran::domain::cellConfigAt(session_, cellIndex),
                                               &cellState);
  if (cellIndex < channelModes_.size()) {
    channelModes_[cellIndex] = heiran::domain::cellConfigAt(session_, cellIndex).channel;
    cells_.at(cellIndex)->setChannelMode(channelModes_[cellIndex]);
  }

  refreshCellViewStates();
  refreshSelectionOverlays();
  updateStatus();
  notifyPersistentStateChanged();
  Q_EMIT viewChanged();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::handlePanRequest(int cellIndex, double deltaImageX, double deltaImageY) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }
  setActiveCell(cellIndex);
  QElapsedTimer totalTimer;
  QElapsedTimer stepTimer;
  if (profilingEnabled()) {
    totalTimer.start();
  }

  if (cellStates_[cellIndex].syncEnabled) {
    heiran::domain::applyPanToGlobal(&globalState_, deltaImageX, deltaImageY);
  } else {
    heiran::domain::applyPanToCell(&cellStates_[cellIndex], deltaImageX, deltaImageY);
  }

  if (profilingEnabled()) {
    stepTimer.start();
  }
  refreshCellViewStates();
  const qint64 refreshDurationNs = profilingEnabled() ? stepTimer.nsecsElapsed() : 0;
  if (profilingEnabled()) {
    stepTimer.restart();
  }
  updateStatus();
  const qint64 statusDurationNs = profilingEnabled() ? stepTimer.nsecsElapsed() : 0;
  if (profilingEnabled()) {
    panProfiler().record({totalTimer.nsecsElapsed(), refreshDurationNs, statusDurationNs});
  }
}

void ViewerGridWidget::handleZoomRequest(int cellIndex, double zoomFactor, double anchorImageX,
                                         double anchorImageY) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }
  setActiveCell(cellIndex);

  const auto &cellState = cellStates_.at(cellIndex);
  heiran::domain::GlobalViewState adjustedState = globalState_;
  adjustedState.baseOffsetX += cellState.offsetDeltaX;
  adjustedState.baseOffsetY += cellState.offsetDeltaY;
  heiran::domain::applyZoom(&adjustedState, zoomFactor, anchorImageX, anchorImageY);
  globalState_.baseScale = adjustedState.baseScale;
  globalState_.baseOffsetX = adjustedState.baseOffsetX - cellState.offsetDeltaX;
  globalState_.baseOffsetY = adjustedState.baseOffsetY - cellState.offsetDeltaY;
  refreshCellViewStates();
  updateStatus();
}

// 等倍ならフィットへ、それ以外なら等倍へ切り替える。等倍へは押した位置を固定点にする。
// 拡大したい場所をダブルクリックするのが自然なため。
void ViewerGridWidget::handleActualSizeToggleRequest(int cellIndex, double anchorImageX,
                                                     double anchorImageY) {
  if (cellIndex < 0 || cellIndex >= cellStates_.size()) {
    return;
  }

  const auto finalState =
      heiran::domain::computeFinalViewState(globalState_, cellStates_.at(cellIndex));
  if (finalState.scale <= 0.0) {
    return;
  }

  constexpr double kActualSizeTolerance = 0.001;
  if (std::abs(finalState.scale - 1.0) < kActualSizeTolerance) {
    setActiveCell(cellIndex);
    requestFitToCells();
    return;
  }
  handleZoomRequest(cellIndex, 1.0 / finalState.scale, anchorImageX, anchorImageY);
}

void ViewerGridWidget::clearHoverInfo() {
  hoverInfoSummary_.clear();
  hoverCursorPosition_.reset();
  hoverReferenceCellIndex_ = -1;
  for (auto *view : cells_) {
    view->setSharedCursorPosition(std::nullopt);
  }
  Q_EMIT pixelInfoChanged({});
  Q_EMIT pixelPreviewChanged(LoupePreview{});
  updateStatus();
}

// 基準セルの位置を各セルへ写して渡す。セルごとに大きさと表示変換が違うため、
// 同じ画像座標が同じ場所を指すとは限らない。
void ViewerGridWidget::updateSharedCursorPositions(int referenceCellIndex,
                                                   const QPointF &imagePosition) {
  for (int targetCellIndex = 0; targetCellIndex < cells_.size(); ++targetCellIndex) {
    cells_.at(targetCellIndex)
        ->setSharedCursorPosition(
            mapImagePointToCell(imagePosition, referenceCellIndex, targetCellIndex));
  }
}

// 画素値表示の行を組み立てる。先頭にページと座標、続けてセルごとの値を並べる。
QStringList ViewerGridWidget::buildHoverDetails(int referenceCellIndex,
                                                const QPointF &imagePosition) const {
  const int imageX = static_cast<int>(std::floor(imagePosition.x()));
  const int imageY = static_cast<int>(std::floor(imagePosition.y()));

  QStringList details;
  details << QString("page=%1").arg(globalState_.currentPage + 1);
  details << QString("x=%1 y=%2").arg(imageX).arg(imageY);
  for (int targetCellIndex = 0; targetCellIndex < cells_.size(); ++targetCellIndex) {
    const QPointF mappedImagePosition =
        mapImagePointToCell(imagePosition, referenceCellIndex, targetCellIndex);
    details << cells_.at(targetCellIndex)->pixelInfoAtImagePosition(mappedImagePosition);
  }
  return details;
}

// ルーペの入力。塗りは表示用画像から、数値は解析用バッファから取るため両方を渡す。
LoupePreview ViewerGridWidget::buildLoupePreview(int cellIndex,
                                                 const QPointF &imagePosition) const {
  auto *view = cells_.at(cellIndex);
  return {.image = view->currentImage(),
          .imagePosition = imagePosition,
          .channelMode = view->channelMode(),
          .valid = true,
          .sampler = view->analysisSampler()};
}

void ViewerGridWidget::handleHoverInfo(int cellIndex, const QPointF &imagePosition, bool active) {
  if (!active) {
    clearHoverInfo();
    return;
  }

  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return;
  }
  setActiveCell(cellIndex);
  hoverCursorPosition_ = imagePosition;
  hoverReferenceCellIndex_ = cellIndex;
  hoverInfoSummary_ = QString("x=%1 y=%2")
                          .arg(static_cast<int>(std::floor(imagePosition.x())))
                          .arg(static_cast<int>(std::floor(imagePosition.y())));

  // 計測が要らないときは時計を触らない。hover はマウスが動くたびに走る。
  const bool profiling = profilingEnabled();
  QElapsedTimer totalTimer;
  QElapsedTimer stepTimer;
  if (profiling) {
    totalTimer.start();
    stepTimer.start();
  }
  HoverProfileSample sample;
  const auto recordStep = [profiling, &stepTimer](qint64 &destination) {
    if (!profiling) {
      return;
    }
    destination = stepTimer.nsecsElapsed();
    stepTimer.restart();
  };

  updateSharedCursorPositions(cellIndex, imagePosition);
  recordStep(sample.cursorNs);

  const QStringList details = buildHoverDetails(cellIndex, imagePosition);
  recordStep(sample.detailsNs);
  // ステータスバーへ、位置と各セルの値を出す (Pixel Info を閉じていても、値を読める)。
  hoverInfoSummary_ =
      buildPixelStatusSummary(static_cast<int>(std::floor(imagePosition.x())),
                              static_cast<int>(std::floor(imagePosition.y())), details);

  Q_EMIT pixelInfoChanged(details);
  recordStep(sample.infoHandlerNs);

  Q_EMIT pixelPreviewChanged(buildLoupePreview(cellIndex, imagePosition));
  recordStep(sample.previewNs);

  updateStatus();
  recordStep(sample.statusNs);

  if (profiling) {
    sample.totalNs = totalTimer.nsecsElapsed();
    hoverProfiler().record({sample.totalNs, sample.cursorNs, sample.detailsNs, sample.infoHandlerNs,
                            sample.previewNs, sample.statusNs});
  }
}

void ViewerGridWidget::handleSelectionRequest(int cellIndex, const QPointF &startImagePosition,
                                              const QPointF &endImagePosition, bool final) {
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return;
  }
  setActiveCell(cellIndex);

  const QRectF normalizedRect =
      heiran::domain::normalizeSelectionRect(startImagePosition, endImagePosition);
  if (final) {
    previewSelectionRect_.reset();
    selectionRect_ = normalizedRect;
    selectionReferenceCellIndex_ = cellIndex;
    previewSelectionReferenceCellIndex_ = -1;
  } else {
    previewSelectionRect_ = normalizedRect;
    previewSelectionReferenceCellIndex_ = cellIndex;
  }

  refreshSelectionOverlays();
  if (final) {
    Q_EMIT viewChanged();
  }
}

void ViewerGridWidget::setActiveCell(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cells_.size() || activeCellIndex_ == cellIndex) {
    return;
  }

  activeCellIndex_ = cellIndex;
  updateActiveFrames();
  Q_EMIT activeCellChanged();
}

void ViewerGridWidget::updateActiveFrames() {
  // 見えているセルが 1 つ (1 つしか無い、単独表示) なら、枠は要らない。
  const bool multiple = soloCell_ < 0 && cells_.size() > 1;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    cells_.at(cellIndex)->setActiveFrame(multiple && cellIndex == activeCellIndex_);
  }
}

void ViewerGridWidget::refreshSelectionOverlays() {
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    auto *view = cells_.at(cellIndex);
    const std::optional<QRectF> cellSelectionRect =
        selectionRect_ && selectionReferenceCellIndex_ >= 0
            ? std::optional<QRectF>(
                  mapImageRectToCell(*selectionRect_, selectionReferenceCellIndex_, cellIndex))
            : std::nullopt;
    const std::optional<QRectF> cellPreviewSelectionRect =
        previewSelectionRect_ && previewSelectionReferenceCellIndex_ >= 0
            ? std::optional<QRectF>(mapImageRectToCell(
                  *previewSelectionRect_, previewSelectionReferenceCellIndex_, cellIndex))
            : std::nullopt;
    view->setSelectionRect(cellSelectionRect, cellPreviewSelectionRect);
    if (hoverCursorPosition_ && hoverReferenceCellIndex_ >= 0) {
      view->setSharedCursorPosition(
          mapImagePointToCell(*hoverCursorPosition_, hoverReferenceCellIndex_, cellIndex));
    } else {
      view->setSharedCursorPosition(std::nullopt);
    }
  }
}

QRectF ViewerGridWidget::mapImageRectToCell(const QRectF &imageRect, int referenceCellIndex,
                                            int targetCellIndex) const {
  return QRectF(mapImagePointToCell(imageRect.topLeft(), referenceCellIndex, targetCellIndex),
                mapImagePointToCell(imageRect.bottomRight(), referenceCellIndex, targetCellIndex))
      .normalized();
}

// セルごとに拡大率とオフセットが違うため、同じ画像座標が画面上の同じ場所を
// 指すとは限らない。写す計算そのものは `domain` にある。
QPointF ViewerGridWidget::mapImagePointToCell(const QPointF &point, int referenceCellIndex,
                                              int targetCellIndex) const {
  if (referenceCellIndex < 0 || referenceCellIndex >= cellStates_.size() || targetCellIndex < 0 ||
      targetCellIndex >= cellStates_.size()) {
    return point;
  }

  const auto referenceState =
      heiran::domain::computeFinalViewState(globalState_, cellStates_.at(referenceCellIndex));
  const auto targetState =
      heiran::domain::computeFinalViewState(globalState_, cellStates_.at(targetCellIndex));
  return QPointF(heiran::domain::mapImageXBetweenCells(referenceState, targetState, point.x()),
                 heiran::domain::mapImageYBetweenCells(referenceState, targetState, point.y()));
}

// ---- 差分のセル ----

std::optional<heiran::domain::CellDiff> ViewerGridWidget::cellDiff(int cellIndex) const {
  return heiran::domain::cellConfigAt(session_, cellIndex).diff;
}

std::optional<heiran::domain::DiffMetrics> ViewerGridWidget::cellDiffMetrics(int cellIndex) const {
  if (cellIndex < 0 || cellIndex >= diffStates_.size()) {
    return std::nullopt;
  }
  return diffStates_.at(cellIndex).metrics;
}

QString ViewerGridWidget::cellDiffMessage(int cellIndex) const {
  if (cellIndex < 0 || cellIndex >= diffStates_.size()) {
    return {};
  }
  return diffStates_.at(cellIndex).message;
}

int ViewerGridWidget::firstEmptyCellIndex() const {
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto config = heiran::domain::cellConfigAt(session_, cellIndex);
    if (config.imageIndex < 0 && !config.diff) {
      return cellIndex;
    }
  }
  return -1;
}

void ViewerGridWidget::setCellDiff(int cellIndex,
                                   const std::optional<heiran::domain::CellDiff> &diff) {
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return;
  }
  while (session_.cells.size() <= cellIndex) {
    session_.cells.push_back({});
  }
  auto &config = session_.cells[cellIndex];
  if (config.diff == diff) {
    return;
  }
  const bool wasDiff = config.diff.has_value();
  // 基準と比べる側が同じで、設定だけが変わったときは、前の結果 (差の場)
  // を残す。倍率、見せ方、閾値の変更で、
  // 重い計算をやり直さないため。差分をやめる、始める、基準や比べる側を替えるときは、捨てる。
  const bool sameSources = wasDiff && diff.has_value() && config.diff->baseCell == diff->baseCell &&
                           config.diff->targetCell == diff->targetCell;
  config.diff = diff;
  if (!sameSources && cellIndex < diffStates_.size()) {
    diffStates_[cellIndex] = {};
  }

  if (wasDiff != diff.has_value()) {
    // 差分を出す / やめる。元の表示へ戻すか、差分の絵を出させる。
    updateCellImage(cellIndex, false);
    if (!diff) {
      // 空きセルだったものは、空欄の表示へ戻る。
      if (heiran::domain::imageSetForCell(session_, cellIndex) == nullptr) {
        cells_.at(cellIndex)->setDecodedImage({}, QString(),
                                              CellStatusInfo::empty(tr("Drop an image here.")));
      }
    }
  }
  // 差分の有無は、全セルの右クリックの項目に関わる。
  for (int index = 0; index < cells_.size(); ++index) {
    updateDiffUi(index);
  }
  scheduleDiffRefresh();
  notifyPersistentStateChanged();
  Q_EMIT cellDiffChanged(cellIndex);
  Q_EMIT imageContentChanged();
}

QStringList ViewerGridWidget::cellLabelsForDiff() const {
  QStringList labels;
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    const auto config = heiran::domain::cellConfigAt(session_, cellIndex);
    const auto *imageSet = heiran::domain::imageSetForCell(session_, cellIndex);
    QString label = tr("Cell %1").arg(cellIndex + 1);
    if (config.diff && config.diff->targetCell >= 0) {
      label += tr(" (diff)");
    } else if (imageSet != nullptr) {
      label += QString(": %1").arg(imageSet->name);
    } else {
      label += tr(" (empty)");
    }
    labels << label;
  }
  return labels;
}

void ViewerGridWidget::updateDiffUi(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cells_.size()) {
    return;
  }
  auto *view = cells_.at(cellIndex);
  const auto config = heiran::domain::cellConfigAt(session_, cellIndex);
  const auto labels = cellLabelsForDiff();
  view->setDiffMenuInfo(labels, config.diff.has_value(),
                        config.diff &&
                            config.diff->settings.mode == heiran::domain::DiffMode::Wipe);
  if (config.diff) {
    // 差は、比べる側から基準を引いたもの。
    const int target = config.diff->targetCell >= 0 ? config.diff->targetCell : cellIndex;
    if (config.diff->settings.mode == heiran::domain::DiffMode::Wipe) {
      // 基準は、左 / 上の側。
      view->setDiffTitle(
          tr("Wipe: Cell %1 | Cell %2").arg(config.diff->baseCell + 1).arg(target + 1));
    } else {
      view->setDiffTitle(
          tr("Diff: Cell %1 - Cell %2").arg(target + 1).arg(config.diff->baseCell + 1));
    }
  } else {
    view->setDiffTitle(QString());
  }
  view->setDiffBar(config.diff, labels, config.diff && config.diff->targetCell < 0);
  if (config.diff) {
    view->setDiffBarResult(cellDiffMetrics(cellIndex), cellDiffMessage(cellIndex));
  }
}

bool ViewerGridWidget::showDifference(int cellIndex, int baseCell, heiran::domain::DiffMode mode) {
  if (cellIndex < 0 || cellIndex >= cells_.size() || baseCell < 0 || baseCell >= cells_.size() ||
      baseCell == cellIndex) {
    return false;
  }
  const bool ownsImage = heiran::domain::imageSetForCell(session_, cellIndex) != nullptr;
  heiran::domain::CellDiff diff;
  diff.baseCell = baseCell;
  diff.settings.mode = mode;
  if (!ownsImage) {
    // 比べる側は、基準でも自分でもない、画像のあるセル。
    for (int candidate = 0; candidate < cells_.size(); ++candidate) {
      const auto config = heiran::domain::cellConfigAt(session_, candidate);
      if (candidate != cellIndex && candidate != baseCell && config.imageIndex >= 0 &&
          !config.diff) {
        diff.targetCell = candidate;
        break;
      }
    }
    if (diff.targetCell < 0) {
      return false;
    }
  }
  setCellDiff(cellIndex, diff);
  return true;
}

bool ViewerGridWidget::addDifferenceCell(heiran::domain::DiffMode mode) {
  const int emptyCell = firstEmptyCellIndex();
  if (emptyCell < 0) {
    return false;
  }
  // 基準は、操作対象のセル (画像があれば)。無ければ、先頭の画像のセル。
  const auto hasImage = [&](int cell) {
    const auto config = heiran::domain::cellConfigAt(session_, cell);
    return config.imageIndex >= 0 && !config.diff;
  };
  int baseCell = targetCellIndex();
  if (baseCell < 0 || baseCell >= cells_.size() || !hasImage(baseCell)) {
    baseCell = -1;
    for (int cell = 0; cell < cells_.size(); ++cell) {
      if (hasImage(cell)) {
        baseCell = cell;
        break;
      }
    }
  }
  return baseCell >= 0 && showDifference(emptyCell, baseCell, mode);
}

AnalysisSampler ViewerGridWidget::samplerForSource(
    const heiran::infra::DecodedImage &source) const {
  const auto transfer = makeColorTransformParams(source.colorSpace, source.colorSpace);
  return {
      .decodedImage = source,
      .sourceSize = source.displayImage.size(),
      .lumaWeights = lumaWeightsFor(source.colorSpace),
      .rgbToXyz = rgbToXyzFor(source.colorSpace),
      .valueSpace = analysisValueSpace_,
      .sourceTransfer = transfer.sourceTransfer,
      .sourceGamma = transfer.sourceGamma,
  };
}

void ViewerGridWidget::scheduleDiffRefresh() {
  if (diffRefreshScheduled_) {
    return;
  }
  diffRefreshScheduled_ = true;
  QMetaObject::invokeMethod(
      this,
      [this]() {
        diffRefreshScheduled_ = false;
        refreshDiffCells();
      },
      Qt::QueuedConnection);
}

void ViewerGridWidget::refreshDiffCells() {
  if (navigationPageCount() == 0) {
    return;
  }
  for (int cellIndex = 0; cellIndex < cells_.size(); ++cellIndex) {
    if (heiran::domain::cellConfigAt(session_, cellIndex).diff) {
      refreshDiffCell(cellIndex);
    }
  }
}

void ViewerGridWidget::refreshDiffCell(int cellIndex) {
  const auto config = heiran::domain::cellConfigAt(session_, cellIndex);
  if (!config.diff || cellIndex >= cells_.size()) {
    return;
  }
  if (diffStates_.size() != cells_.size()) {
    diffStates_.resize(cells_.size());
  }
  if (cellSources_.size() != cells_.size()) {
    return;  // まだ、どのセルも画像を組み立てていない。
  }

  const auto &diff = *config.diff;
  const int baseCell = diff.baseCell;
  const int targetCell = diff.targetCell >= 0 ? diff.targetCell : cellIndex;
  auto *view = cells_.at(cellIndex);
  auto &state = diffStates_[cellIndex];

  const auto fail = [&](const QString &message) {
    if (state.message == message && !state.metrics) {
      return;
    }
    state = {};
    state.message = message;
    view->setDecodedImage({}, QString(), CellStatusInfo::empty(message));
    updateDiffUi(cellIndex);
    Q_EMIT cellDiffChanged(cellIndex);
  };

  if (baseCell < 0 || baseCell >= cells_.size() || targetCell >= cells_.size()) {
    fail(tr("Diff: the cell does not exist."));
    return;
  }
  const auto &baseSource = cellSources_.at(baseCell);
  const auto &targetSource = cellSources_.at(targetCell);
  if (baseSource.displayImage.isNull() || targetSource.displayImage.isNull()) {
    // 自身の画像を比べる側にしたセルは、読み込み中と失敗の表示を、通常のセルが出している。
    if (diff.targetCell < 0 && cellSources_.at(cellIndex).displayImage.isNull() &&
        view->cellStatus() != CellStatus::Empty) {
      return;
    }
    fail(tr("Diff: waiting for Cell %1 and Cell %2.").arg(baseCell + 1).arg(targetCell + 1));
    return;
  }

  // 入力も設定も変わっていなければ、作り直さない。
  const bool sameInputs = state.computed && state.baseSerial == cellSourceSerial_.at(baseCell) &&
                          state.targetSerial == cellSourceSerial_.at(targetCell);
  if (sameInputs && state.settings == diff.settings) {
    return;
  }

  const DiffInput baseInput{.image = baseSource.displayImage,
                            .sampler = samplerForSource(baseSource)};
  DiffOutput output;
  if (sameInputs && state.field != nullptr && diffFieldServes(*state.field, diff.settings)) {
    // 倍率、見せ方、閾値だけが変わった。重い計算 (差の場) は使い回し、絵と指標だけを求め直す。
    output = renderDiffFromField(*state.field, baseInput, diff.settings);
  } else {
    const DiffInput targetInput{.image = targetSource.displayImage,
                                .sampler = samplerForSource(targetSource)};
    if (diff.settings.mode == heiran::domain::DiffMode::Wipe) {
      state.field.reset();
      output = renderDiff(baseInput, targetInput, diff.settings);
    } else {
      QString error;
      ++diffFieldComputeCount_;
      auto field = computeDiffField(baseInput, targetInput, diff.settings, &error);
      if (field == nullptr) {
        fail(error);
        return;
      }
      output = renderDiffFromField(*field, baseInput, diff.settings);
      state.field = std::move(field);
    }
  }
  if (!output.ok) {
    fail(output.error);
    return;
  }

  heiran::infra::DecodedImage image;
  image.displayImage = output.image.convertToFormat(QImage::Format_RGBA8888);
  image.decoderBackend = diff.settings.mode == heiran::domain::DiffMode::Wipe
                             ? QStringLiteral("wipe")
                             : QStringLiteral("diff");
  image.sourceSampleType = heiran::infra::PixelSampleType::UInt8;
  image.sourceColorModel = heiran::infra::PixelColorModel::RGB;
  image.sourceChannelCount = 3;
  image.colorSpace = QColorSpace(QColorSpace::SRgb);
  const QString path = diff.targetCell < 0 ? view->currentFilePath() : QString();
  view->setDecodedImage(image, path, CellStatusInfo::ready());
  view->setChannelMode(cellIndex < channelModes_.size() ? channelModes_.at(cellIndex)
                                                        : ChannelMode::Color);

  state.computed = true;
  state.baseSerial = cellSourceSerial_.at(baseCell);
  state.targetSerial = cellSourceSerial_.at(targetCell);
  state.settings = diff.settings;
  state.metrics = output.metrics;
  state.message.clear();
  updateDiffUi(cellIndex);
  Q_EMIT cellDiffChanged(cellIndex);
  Q_EMIT imageContentChanged();
}

}  // namespace heiran::ui
