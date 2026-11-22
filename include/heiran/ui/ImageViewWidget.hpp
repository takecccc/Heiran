#pragma once

#include "heiran/domain/DiffMetrics.hpp"
#include "heiran/domain/SessionModel.hpp"
#include "heiran/domain/ViewState.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/AnalysisSampler.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/CellStatus.hpp"
#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/CursorStyle.hpp"
#include "heiran/ui/DisplayTransform.hpp"
#include "heiran/ui/PixelValueFormat.hpp"

#include <QImage>
#include <QLabel>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QWidget>
#include <array>
#include <optional>

namespace heiran {
namespace ui {

class CellHeaderWidget;
class DiffBarWidget;
class ImageCanvasWidget;

class ImageViewWidget : public QWidget {
  Q_OBJECT

public:
  explicit ImageViewWidget(QWidget *parent = nullptr);
  void restrictToComparison();
  void setFavorite(bool favorite, bool available);

  void setCellInfo(const heiran::domain::ImageSet *imageSet, int imageSetIndex, int cellIndex,
                   const QString &sessionDirectory);
  void setAvailableImageNames(const QStringList &imageNames);
  // 差分の帯 (画像の上の小さな設定欄)。`diff` が空なら隠す。`cellLabels` は選べるセルの名前、
  // `ownsImage` は、このセル自身の画像が比べる側か。
  void setDiffBar(const std::optional<heiran::domain::CellDiff> &diff,
                  const QStringList &cellLabels, bool ownsImage);
  // 見出しに出す差分の題 ("Diff: Cell 2 - Cell 1")。空なら、通常の (ファイルの) 見出し。
  void setDiffTitle(const QString &title);
  void setDiffBarResult(const std::optional<heiran::domain::DiffMetrics> &metrics,
                        const QString &message);
  void setDiffRegion(const QRect &region = {}, qsizetype index = -1, qsizetype count = 0);
  QRectF diffRegionInDisplay() const;
  void setDiffBlinkFrames(std::array<QImage, 2> frames = {});
  void configureDiffBlinkPlayback(int intervalMs, std::optional<int> frameIndex);
  int diffBlinkFrame() const;
  // 右クリックの項目へ出す、セルの名前の一覧と、このセルの差分の有無。
  void setDiffMenuInfo(const QStringList &cellLabels, bool hasDiff, bool diffIsWipe = false);
  void setDecodedImage(const heiran::infra::DecodedImage &decodedImage, const QString &sourcePath,
                       const CellStatusInfo &statusInfo);
  void setChannelMode(ChannelMode channelMode);
  void setPixelValueFormat(PixelValueFormat pixelValueFormat);
  void setExposureStops(double exposureStops);
  void setFilters(MagnifyFilter magnifyFilter, MinifyFilter minifyFilter);
  void setTransparencyBackground(const TransparencyBackground &background);
  // セルの枠線 (画像の面の外周と、見出しの下の線) を描くか。
  void setBordersVisible(bool visible);
  void setHeaderVisible(bool visible);
  // ワイプの境目の線の位置
  // (このウィジェットの座標)。試験のために出している。線が出ていなければ、無効な値。
  QRect wipeHandleGeometry() const;
  // 境目の線を、ドラッグで動かす (指した位置は、このウィジェットの座標)。試験のために出している。
  void dragWipeLineTo(const QPoint &positionInThis) { dragWipeTo(positionInThis); }
  // 操作の対象のセルを、枠で示す (複数のセルが見えているときだけ、呼び出し側が真にする)。
  void setActiveFrame(bool visible);
  bool hasActiveFrame() const { return activeFrame_; }
  void setCursorStyle(CursorStyle style);
  bool bordersVisible() const { return bordersVisible_; }
  void setDisplayProfile(DisplayProfileMode mode, const QString &profilePath);
  void setAnalysisValueSpace(AnalysisValueSpace valueSpace);
  void setColorLutOptions(const ColorLutOptions &options);
  void setViewState(const heiran::domain::GlobalViewState &globalState,
                    const heiran::domain::CellViewState &cellState);
  void setHistogramHighlight(const QImage &mask);
  void setComparisonHighlight(const QImage &mask, bool scaleToImage);
  const QImage &comparisonHighlight() const { return comparisonHighlight_; }
  void setSelectionRect(const std::optional<QRectF> &selectionRect,
                        const std::optional<QRectF> &previewSelectionRect);
  void setSharedCursorPosition(const std::optional<QPointF> &imagePosition);
  QString pixelInfoAtImagePosition(const QPointF &imagePosition,
                                   std::optional<int> displayIndex = std::nullopt) const;

  int cellIndex() const { return cellIndex_; }

  // 今このセルが表示しているファイル。未読込や空欄では空文字列。
  QString currentFilePath() const { return sourcePath_; }

  // このセルが GPU へ載せているテクスチャのバイト数。描画するまでは 0。
  qint64 gpuTextureBytes() const;
  qint64 gpuCachedTextureBytes() const;
  QString displayLabel() const;
  const QImage &currentImage() const;
  // 表示している見た目をクリップボードへ渡す。解析用の値ではない。
  void copyImageToClipboard() const;
  const heiran::infra::DecodedImage &currentDecodedImage() const;
  AnalysisSampler analysisSampler() const;
  QSize canvasSize() const;
  // 表示する画像があるか。表示用画像を作らずに判定できる。
  bool hasImage() const;
  // 変換後の表示サイズ。表示用画像を作らずに求められる。
  QSize displayImageSize() const;
  CellStatus cellStatus() const;
  ChannelMode channelMode() const;

protected:
  void resizeEvent(QResizeEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;

Q_SIGNALS:
  void favoriteToggleRequested(int cellIndex);
  // セルの大きさが変わった
  // (ウィンドウの大きさ、格子、単独表示の変化)。フィットした状態で見ているなら、フィットし直すために使う。
  void cellResized();
  void panRequested(int cellIndex, double deltaImageX, double deltaImageY);
  void zoomRequested(int cellIndex, double zoomFactor, double anchorImageX, double anchorImageY);
  void pageScrollRequested(int pageOffset);
  // ダブルクリック。フィットと等倍の切り替えを求める。固定点はホイールと同じ座標系。
  void actualSizeToggleRequested(int cellIndex, double anchorImageX, double anchorImageY);
  void hoverInfoRequested(int cellIndex, const QPointF &imagePosition, bool active);
  void channelContextMenuRequested(int cellIndex, ChannelMode channelMode, const QPoint &globalPos);
  void syncToggleRequested(int cellIndex);
  void transparencyPatternRequested(TransparencyPattern pattern);
  void scaleContextMenuRequested(int cellIndex, double scale);
  void gammaContextMenuRequested(int cellIndex, double gamma);
  void invertContextMenuRequested(int cellIndex, bool invert);
  void reloadRequested(int cellIndex);
  void rotationContextMenuRequested(int cellIndex, int rotationQuarterTurns);
  void flipContextMenuRequested(int cellIndex, bool flipX, bool flipY);
  void labelVisibilityRequested(int cellIndex, bool visible);
  void editLabelRequested(int cellIndex);
  void assignImageIndexRequested(int cellIndex, int imageIndex);
  // 帯で差分の指定を変えた / 閉じた。右クリックから始める / やめる。
  void diffChangeRequested(int cellIndex, const heiran::domain::CellDiff &diff);
  void diffStopRequested(int cellIndex);
  void diffRegionStepRequested(int cellIndex, int direction);
  void diffBlinkRequested(int cellIndex, bool enabled);
  void diffStartRequested(int cellIndex, int baseCell);
  void wipeStartRequested(int cellIndex, int baseCell);
  void resetCellDisplayRequested(int cellIndex);
  void dropItemsRequested(int cellIndex, const QStringList &paths);
  void selectionRequested(int cellIndex, const QPointF &startImagePosition,
                          const QPointF &endImagePosition, bool final);
  void selectionClearRequested(int cellIndex);

private:
  friend class CellHeaderWidget;
  friend class ImageCanvasWidget;

  QPoint sourcePixelForDisplayPixel(const QPoint &displayPixel) const;
  // 画素値表示に出す座標。復号時に適用された EXIF の分も戻し、元ファイルの
  // 座標で答える。解析用バッファの読み出しには使わない。
  QPoint originalFilePixelForDisplayPixel(const QPoint &displayPixel) const;
  void rebuildDisplayImage();
  const QImage &displayImage() const;
  DisplayTransformParams displayTransformParams(const QColorSpace &sourceOverride = {}) const;
  void rebuildOverlayCache();
  // 差分の帯を、画像の面の上端へ重ねる (レイアウトには入れない。画像の位置をずらさないため)。
  void placeDiffBar();
  // 差分の帯は、マウスが乗っている間だけ出す (操作中の入力や開いた一覧がある間は出したまま)。
  void updateDiffBarVisibility();

  const heiran::domain::ImageSet *imageSet_ = nullptr;
  int imageSetIndex_ = -1;
  int cellIndex_ = -1;
  heiran::infra::DecodedImage decodedImage_;
  // 表示対象の世代。GPU テクスチャを作り直す契機の判定に使う。
  quint64 imageGeneration_ = 0;
  PixelValueFormat pixelValueFormat_ = PixelValueFormat::Normalized255;
  // 表示の露出。2^exposureStops_ を掛けてから 0.0-1.0 へ丸める。
  double exposureStops_ = 0.0;
  const QColorSpace &resolvedDisplayColorSpace() const;
  void updateLumaWeights();
  void updateColorSpaceOverride();
  const QColorSpace &effectiveColorSpace() const;
  std::shared_ptr<const ColorLut> colorLutFor(const QColorSpace &source,
                                              const QColorSpace &display) const;

  // `.hrn` で指定された色空間。無効なら埋め込みと形式の既定に従う。
  QColorSpace colorSpaceOverride_;
  // `.hrn` のあるディレクトリ。ICC ファイルの相対経路の基準にする。
  QString sessionDirectory_;
  // 輝度表示の重み。元画像の色空間から導く。
  heiran::domain::LumaWeights lumaWeights_{.red = 0.2126, .green = 0.7152, .blue = 0.0722};
  // 色差の計算に使う、元の線形 RGB から XYZ への行列。重みと同じ時機に求める。
  std::optional<heiran::domain::Matrix3> rgbToXyz_;
  AnalysisValueSpace analysisValueSpace_ = AnalysisValueSpace::Source;
  ColorLutOptions colorLutOptions_;
  DisplayProfileMode displayProfileMode_ = DisplayProfileMode::System;
  QString displayProfilePath_;
  // 解決した表示先の色空間。画面や設定が変わるまで使い回す。
  // プロファイルはファイルから読むため、描画のたびに解決し直せない。
  mutable QColorSpace displayColorSpace_;
  mutable const QScreen *displayColorSpaceScreen_ = nullptr;
  mutable bool displayColorSpaceDirty_ = true;
  // 焼いた対応表。組み合わせが変わるまで使い回す。
  mutable std::shared_ptr<const ColorLut> colorLut_;
  mutable QColorSpace colorLutSource_;
  mutable QColorSpace colorLutDisplay_;
  mutable ColorLutOptions colorLutBuiltOptions_;
  MagnifyFilter magnifyFilter_ = MagnifyFilter::Nearest;
  TransparencyBackground transparencyBackground_;
  bool bordersVisible_ = true;
  bool activeFrame_ = false;
  QWidget *activeFrameWidget_ = nullptr;
  // ワイプのセルで、境目の線を、ドラッグで動かす。画像と同じ向き (回転と反転が無いとき) だけ出す。
  QWidget *wipeHandleWidget_ = nullptr;
  std::optional<heiran::domain::CellDiff> wipeDiff_;
  void updateWipeHandle();
  void dragWipeTo(const QPoint &positionInThis);
  friend class WipeHandleWidget;
  CursorStyle cursorStyle_ = CursorStyle::Outline;
  MinifyFilter minifyFilter_ = MinifyFilter::Area;
  QImage sourceImage_;
  // 表示用画像。要求されたときに作る。GPU 描画では作らずに済ませたい。
  mutable QImage image_;
  mutable bool displayImageDirty_ = true;
  QString sourcePath_;
  CellStatusInfo statusInfo_;
  heiran::domain::GlobalViewState globalState_;
  heiran::domain::CellViewState cellState_;
  ChannelMode channelMode_ = ChannelMode::Color;
  QImage histogramHighlight_;
  QImage comparisonHighlight_;
  bool scaleComparisonHighlight_ = false;
  std::optional<QRectF> selectionRect_;
  std::optional<QRectF> previewSelectionRect_;
  std::optional<QPointF> sharedCursorPosition_;
  QString cachedScaleText_;
  QString cachedPathText_;
  // 幅、高さ、ファイルサイズ。見出しの右端に出す。ファイルサイズは、ファイルが替わったときだけ
  // 引き直す (アニメーションは 1 コマごとに届くため、毎回は引かない)。
  QString cachedInfoText_;
  QString cachedFileBytesPath_;
  qint64 cachedFileBytes_ = -1;
  QString cachedExpandedLabel_;
  QStringList availableImageNames_;
  // 右クリックの差分の項目に使う。
  QStringList diffMenuCellLabels_;
  bool diffMenuHasDiff_ = false;
  bool diffMenuIsWipe_ = false;
  DiffBarWidget *diffBar_ = nullptr;
  QRect diffRegion_;
  bool diffBarEnabled_ = false;
  QString diffTitle_;
  CellHeaderWidget *headerWidget_ = nullptr;
  ImageCanvasWidget *canvasWidget_ = nullptr;
  QLabel *favoriteBadge_ = nullptr;
  bool favorite_ = false;
  bool favoriteAvailable_ = false;
};

}  // namespace ui
}  // namespace heiran
