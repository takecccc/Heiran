#include "heiran/ui/ImageViewWidget.hpp"

#include "heiran/ui/DiffBarWidget.hpp"

#include "heiran/ui/FileDragController.hpp"
#include "heiran/ui/FileOperations.hpp"

#include "heiran/ui/CellOverlayPainter.hpp"
#include "heiran/ui/CellStatusPainter.hpp"
#include "heiran/ui/ColorManagement.hpp"
#include "heiran/ui/DisplayProfile.hpp"
#include "heiran/ui/ImageInfoText.hpp"
#include "heiran/ui/PixelInfoText.hpp"

#include "heiran/domain/ImageTransform.hpp"
#include "heiran/ui/CellContextMenu.hpp"
#include "heiran/ui/DisplayLink.hpp"
#include "heiran/ui/DisplayTransform.hpp"
#include "heiran/ui/GpuImageRenderer.hpp"
#include "heiran/ui/PageWheelController.hpp"
#include "heiran/ui/ProfileAccumulator.hpp"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QWindow>

#include <QCoreApplication>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace heiran::ui {

namespace {

constexpr int kHeaderHeight = 28;
constexpr int kSyncButtonWidth = 44;
constexpr int kInnerPadding = 8;

// 透過画素の下地。市松模様ではなく単色にしている。

ProfileAccumulator &paintProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "paint", {"total", "image"}, 60);
  return profiler;
}

// 画面へ実際に出た間隔。
//
// `paint` は UI スレッドが使う時間しか見ていない。GL の命令は非同期に積まれるため、
// GPU 側の完了と vsync の待ちが入らない。なめらかさはこちらで決まる。
// 何枚で回しているか。既定はドライバ任せのため、実際の値を出して確かめる。
QString swapBehaviorName(QSurfaceFormat::SwapBehavior behavior) {
  switch (behavior) {
    case QSurfaceFormat::SingleBuffer:
      return QStringLiteral("single");
    case QSurfaceFormat::DoubleBuffer:
      return QStringLiteral("double");
    case QSurfaceFormat::TripleBuffer:
      return QStringLiteral("triple");
    case QSurfaceFormat::DefaultSwapBehavior:
      break;
  }
  return QStringLiteral("default");
}

ProfileAccumulator &presentProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "present", {"interval"}, 60);
  return profiler;
}

// 描き直したいと思ってから、実際に画面へ出るまで。
//
// `present` には穴がある。マウスが動いていない間も間隔として数えるため、手が
// 止まった 30 ms と、本当に詰まった 30 ms を区別できない。こちらは描き直しの
// 要求が出ている間しか測らないため、詰まりだけが出る。
// 合図が UI スレッドへ届く間隔。出す側の `vblank` と差があれば、
// スレッド跨ぎの待ち行列で遅れていることになる。
// 画面の更新へ揃えるかどうか。`HEIRAN_PAN_SYNC=0` で切る。
bool displaySyncEnabled() {
  static const bool enabled = qEnvironmentVariable("HEIRAN_PAN_SYNC", "1") != QLatin1String("0");
  return enabled;
}

ProfileAccumulator &tickProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "tick", {"interval"}, 60);
  return profiler;
}

ProfileAccumulator &latencyProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "latency", {"to_screen"}, 60);
  return profiler;
}

ProfileAccumulator &displayImageProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_LOAD", "display",
                                     {"total", "pixels", "flip", "rotate"}, 20);
  return profiler;
}

bool profilingEnabled() {
  return paintProfiler().enabled();
}

bool loadProfilingEnabled() {
  return displayImageProfiler().enabled();
}

QString formatFileSize(qint64 sizeBytes, const QString &unitText) {
  if (sizeBytes < 0) {
    return "-";
  }

  const QString normalizedUnit = unitText.trimmed().toUpper();
  double divisor = 1.0;
  QString suffix = "B";
  if (normalizedUnit == "KB") {
    divisor = 1024.0;
    suffix = "KB";
  } else if (normalizedUnit == "MB") {
    divisor = 1024.0 * 1024.0;
    suffix = "MB";
  } else if (normalizedUnit == "GB") {
    divisor = 1024.0 * 1024.0 * 1024.0;
    suffix = "GB";
  }

  const double value = static_cast<double>(sizeBytes) / divisor;
  if (suffix == "B") {
    return QString::number(sizeBytes);
  }
  return QString("%1 %2").arg(value, 0, 'f', 2).arg(suffix);
}

QString syncButtonText(int imageSetIndex) {
  return imageSetIndex >= 0 ? QString::number(imageSetIndex + 1) : QString("-");
}

}  // namespace

class CellHeaderWidget final : public QWidget {
public:
  explicit CellHeaderWidget(ImageViewWidget *owner) : QWidget(owner), owner_(owner) {
    setFixedHeight(kHeaderHeight);
    setMouseTracking(true);
  }

  QRect syncButtonRect() const { return QRect(kInnerPadding, 4, kSyncButtonWidth, height() - 8); }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    // 画像を見比べる面の一部。配色に関わらず、中立で控えめな暗さに揃える。
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(33, 35, 39));
    if (owner_->bordersVisible_) {
      painter.setPen(QColor(52, 55, 61));
      painter.drawLine(rect().bottomLeft(), rect().bottomRight());
    }

    // 同期の印。連動しているセルは落ち着いた青緑、外したセルは灰色にして、警告色にはしない。
    const QRect syncRect = syncButtonRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(owner_->cellState_.syncEnabled ? QColor(52, 104, 120) : QColor(58, 61, 67));
    painter.drawRoundedRect(syncRect, 4, 4);
    painter.setPen(owner_->cellState_.syncEnabled ? QColor(226, 240, 246) : QColor(150, 155, 164));
    painter.drawText(syncRect, Qt::AlignCenter, syncButtonText(owner_->imageSetIndex_));

    const QRect scaleRect(syncRect.right() + 10, 0, 92, height());
    painter.setPen(QColor(150, 155, 164));
    painter.drawText(scaleRect, Qt::AlignVCenter | Qt::AlignLeft, owner_->cachedScaleText_);

    if (owner_->statusInfo_.status == CellStatus::Failed) {
      painter.setPen(QColor(240, 150, 150));
    } else if (!owner_->sourcePath_.isEmpty()) {
      painter.setPen(QColor(214, 217, 223));
    } else {
      painter.setPen(QColor(120, 125, 134));
    }

    // 右端に、幅と高さとファイルサイズ。狭いときは、パスを優先して出さない。
    int infoWidth = 0;
    const QString &infoText = owner_->cachedInfoText_;
    if (!infoText.isEmpty()) {
      const int wanted = painter.fontMetrics().horizontalAdvance(infoText) + 8;
      if (width() - scaleRect.right() - 16 - wanted >= 140) {
        infoWidth = wanted;
      }
    }

    const QRect pathRect(scaleRect.right() + 8, 0, width() - scaleRect.right() - 16 - infoWidth,
                         height());
    const QString elidedPathText = painter.fontMetrics().elidedText(
        owner_->cachedPathText_, Qt::ElideMiddle, pathRect.width());
    painter.drawText(pathRect, Qt::AlignVCenter | Qt::AlignLeft, elidedPathText);

    if (infoWidth > 0) {
      painter.setPen(QColor(130, 136, 146));
      painter.drawText(QRect(width() - infoWidth - 8, 0, infoWidth, height()),
                       Qt::AlignVCenter | Qt::AlignRight, infoText);
    }
  }

  // 同期の印は、押せる部品に見えない。何をするものかを、ツールチップで示す。
  bool event(QEvent *event) override {
    if (event->type() == QEvent::ToolTip) {
      const auto *help = static_cast<QHelpEvent *>(event);
      if (syncButtonRect().contains(help->pos())) {
        const QString tip =
            owner_->cellState_.syncEnabled
                ? QCoreApplication::translate(
                      "ImageViewWidget", "In sync: panning and zooming follow the other cells.\n"
                                         "Click to move only this cell (turn sync off).")
                : QCoreApplication::translate("ImageViewWidget",
                                              "Sync is off: this cell moves on its own.\n"
                                              "Click to follow the other cells again.");
        QToolTip::showText(help->globalPos(), tip, this, syncButtonRect());
      } else {
        QToolTip::hideText();
        event->ignore();
      }
      return true;
    }
    return QWidget::event(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    // 押せることが、カーソルで分かるようにする。
    setCursor(syncButtonRect().contains(event->position().toPoint()) ? Qt::PointingHandCursor
                                                                     : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton &&
        syncButtonRect().contains(event->position().toPoint())) {
      Q_EMIT owner_->syncToggleRequested(owner_->cellIndex_);
      event->accept();
      return;
    }

    QWidget::mousePressEvent(event);
  }

private:
  ImageViewWidget *owner_;
};

// ワイプの境目を示す線と、つかむ部分。つかんで動かすと、境目の位置が変わる。
class WipeHandleWidget final : public QWidget {
public:
  WipeHandleWidget(ImageViewWidget *owner, bool vertical)
      : QWidget(owner), owner_(owner), vertical_(vertical) {
    setCursor(vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    setToolTip(QCoreApplication::translate("ImageViewWidget", "Drag to move the wipe line"));
  }

  void setVertical(bool vertical) {
    vertical_ = vertical;
    setCursor(vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center = QRectF(rect()).center();
    // 暗い縁取りの上に、白い線を引く (どの画像の上でも見える)。
    for (const auto &[lineWidth, color] :
         {std::pair{4.0, QColor(0, 0, 0, 150)}, std::pair{2.0, QColor(255, 255, 255, 235)}}) {
      painter.setPen(QPen(color, lineWidth));
      if (vertical_) {
        painter.drawLine(QPointF(center.x(), 0), QPointF(center.x(), height()));
      } else {
        painter.drawLine(QPointF(0, center.y()), QPointF(width(), center.y()));
      }
    }
    // 中央のつまみ。
    painter.setPen(QPen(QColor(0, 0, 0, 170), 1));
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawEllipse(center, 6.0, 6.0);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = true;
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (dragging_) {
      owner_->dragWipeTo(mapToParent(event->position().toPoint()));
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
  }

private:
  ImageViewWidget *owner_;
  bool vertical_;
  bool dragging_ = false;
};

// 操作の対象のセルを示す、縁の細い枠。
class ActiveFrameWidget final : public QWidget {
public:
  explicit ActiveFrameWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setPen(QPen(QColor(86, 156, 214, 220), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
  }
};

// 画像の描画は GPU で行う。選択範囲やラベルなどの重ね描きだけ QPainter を使う。
class ImageCanvasWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit ImageCanvasWidget(ImageViewWidget *owner)
      : QOpenGLWidget(owner), owner_(owner), fileDragController_(this) {
    setMouseTracking(true);
    setAcceptDrops(true);
    // 画像をクリックしたら操作の対象をビューアへ戻す。ツールバーの入力欄を
    // 触った後でも、画像を触れば矢印キーがページ送りへ戻る。
    setFocusPolicy(Qt::ClickFocus);

    // 画面へ出た間隔を測る。連続して出ていない限り記録しない。止まっている間の
    // 長い間隔が混ざると、なめらかさの目安にならない。
    // 画面の更新に合わせて出す。揃えないと、位置の進み方が出るたびにばらつく。
    connect(&DisplayLink::shared(), &DisplayLink::ticked, this, [this]() {
      if (profilingEnabled()) {
        if (tickTimer_.isValid()) {
          const qint64 intervalNs = tickTimer_.nsecsElapsed();
          if (intervalNs < 500'000'000) {
            tickProfiler().record({intervalNs});
          }
        }
        tickTimer_.restart();
      }
      if (updatePending_) {
        flushPendingUpdate();
      }
    });

    connect(this, &QOpenGLWidget::frameSwapped, this, [this]() {
      if (!profilingEnabled()) {
        return;
      }
      if (updateRequestPending_) {
        latencyProfiler().record({updateRequestTimer_.nsecsElapsed()});
        updateRequestPending_ = false;
      }
      if (presentTimer_.isValid()) {
        const qint64 intervalNs = presentTimer_.nsecsElapsed();
        // 500 ms 以上空いた場合は操作が途切れたとみなす。
        if (intervalNs < 500'000'000) {
          presentProfiler().record({intervalNs});
        }
      }
      presentTimer_.restart();
    });
  }

  ~ImageCanvasWidget() override {
    // GL 資源はコンテキストをカレントにしてから手放す。
    if (context() == nullptr) {
      return;
    }
    makeCurrent();
    renderer_.release();
    doneCurrent();
  }

  QPointF imageCoordinatesForPosition(const QPointF &position) const {
    const auto finalState =
        heiran::domain::computeFinalViewState(owner_->globalState_, owner_->cellState_);
    if (finalState.scale <= 0.0) {
      return QPointF();
    }

    return QPointF(heiran::domain::viewToImageX(finalState, position.x()),
                   heiran::domain::viewToImageY(finalState, position.y()));
  }

  QRect labelRectForCurrentState() const {
    if (owner_->cachedExpandedLabel_.isEmpty()) {
      return {};
    }

    const int availableLabelWidth = std::max(0, width() - 16);
    if (availableLabelWidth <= 0) {
      return {};
    }

    QFontMetrics metrics(font());
    const QString elidedLabelText =
        metrics.elidedText(owner_->cachedExpandedLabel_, Qt::ElideRight, std::max(0, width() - 32));
    const QRect textBounds = metrics.boundingRect(elidedLabelText);
    const int labelWidth = std::min(availableLabelWidth, textBounds.width() + 24);
    const int labelHeight = textBounds.height() + 10;
    return QRect(8, 8, labelWidth, labelHeight);
  }

  // GPU 描画では差分スクロールを使わず、毎回全面を描き直す。
  // QOpenGLWidget は部分更新を持たないため、scroll() では見た目が崩れる。
  void panContentsByPixels(int, int) { requestCoalescedUpdate(); }

  // 画面が出せる数を超えて描き直さない。
  //
  // マウスは毎秒 700 回ほど動きを送ってくる。そのたびに描き直すと、120Hz の画面に
  // 対して 6 倍の枚数を積むことになる。積んだ分は表示されずに捨てられ、後から出す
  // 1 枚を遅らせるだけになる。
  //
  // 毎回全面を描き直す以上、どの経路でも画面の更新間隔より細かく描く意味は無い。
  // そのため描き直しの要求は全てここを通す。
  void requestCoalescedUpdate() {
    // 描き直したいと思った時刻。次に画面へ出るまでを測る。
    if (profilingEnabled() && !updateRequestPending_) {
      updateRequestPending_ = true;
      updateRequestTimer_.restart();
    }

    updatePending_ = true;

    // 揃えることと応答の速さを比べるための逃げ道。揃えると位置の進み方は均一に
    // なるが 1 回分遅れるため、どちらが良く見えるかを比べられるようにしてある。
    if (!displaySyncEnabled()) {
      flushPendingUpdate();
      return;
    }

    // 通常はここでは描かず、画面の更新の合図を待つ。
    //
    // **合図が出ない環境がある。** 遠隔接続や仮想ディスプレイでは vblank を待て
    // ないため `DisplayLink` は刻まない。その場合は時間で刻む。遠隔接続では
    // 1 枚ごとに符号化が走るため、無制限に描くのが最も高くつく。
    //
    // 合図が来る場合は 1.5 倍まで待って合図に譲る。来ない場合は更新間隔ちょうどで
    // 刻む。刻む側が二重にならないよう、閾値だけで切り替える。
    const qint64 displayInterval = displayIntervalNs();
    const qint64 fallbackNs =
        DisplayLink::shared().isTicking() ? displayInterval * 3 / 2 : displayInterval;
    if (!lastUpdateTimer_.isValid() || lastUpdateTimer_.nsecsElapsed() >= fallbackNs) {
      flushPendingUpdate();
      return;
    }

    // ここを通った要求は、合図が来なければ誰も拾わない。**最後の 1 回を落とすと
    // 指を止めた位置が画面に出ない。** 保険は要求時にしか見ないため、時間を置いて
    // もう一度見る手を残す。合図が先に描けば `updatePending_` が下りて何もしない。
    if (fallbackScheduled_) {
      return;
    }
    fallbackScheduled_ = true;
    QTimer::singleShot(static_cast<int>(fallbackNs / 1'000'000) + 1, this, [this]() {
      fallbackScheduled_ = false;
      if (updatePending_) {
        flushPendingUpdate();
      }
    });
  }

  void flushPendingUpdate() {
    updatePending_ = false;
    lastUpdateTimer_.restart();
    update();
  }

  void updateHoverInfo(const QPointF &position) {
    if (!rect().contains(position.toPoint())) {
      Q_EMIT owner_->hoverInfoRequested(owner_->cellIndex_, QPointF(), false);
      return;
    }

    Q_EMIT owner_->hoverInfoRequested(owner_->cellIndex_, imageCoordinatesForPosition(position),
                                      true);
  }

  qint64 gpuTextureBytes() const { return renderer_.textureBytes(); }
  qint64 gpuCachedTextureBytes() const { return renderer_.cachedTextureBytes(); }

  // エクスプローラでファイルを選択した状態で開く。選択まで行う方法は
  void showCurrentFileInFolder() const { FileOperations::showInFolder(owner_->sourcePath_); }

  QString pixelInfoAtImagePosition(const QPointF &imagePosition) const {
    if (!owner_->hasImage()) {
      return buildPixelInfoStatusLine(owner_->imageSetIndex_, owner_->statusInfo_);
    }

    const auto displayPixel =
        heiran::domain::pixelAtImagePosition(imagePosition, owner_->displayImageSize());
    if (!displayPixel) {
      return buildPixelInfoOutOfBoundsLine(owner_->imageSetIndex_,
                                           QPoint(static_cast<int>(std::floor(imagePosition.x())),
                                                  static_cast<int>(std::floor(imagePosition.y()))));
    }

    // 値は復号結果から読み、位置は元ファイルの座標で出す。
    const QPoint sourcePixel = owner_->sourcePixelForDisplayPixel(*displayPixel);
    const auto &decodedImage = owner_->decodedImage_;
    const auto values = selectPixelChannelValues(
        owner_->channelMode_, sourceIsGrayscale(decodedImage, owner_->sourceImage_),
        sourceHasAlpha(decodedImage, owner_->sourceImage_),
        [&](int rgbaChannel) {
          return decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), rgbaChannel);
        },
        [&]() {
          return luminanceOf(decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 0),
                             decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 1),
                             decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 2),
                             owner_->lumaWeights_);
        });

    return buildPixelInfoValueLine(owner_->imageSetIndex_,
                                   owner_->originalFilePixelForDisplayPixel(*displayPixel), values,
                                   owner_->pixelValueFormat_);
  }

protected:
  void initializeGL() override {
    // 合図の元を、窓が乗っている画面に合わせる。画面ごとに更新レートが違うため、
    // 別の画面のものに合わせても揃わない。窓を跨いで動かした場合も追う。
    DisplayLink::shared().setTargetScreen(screen());
    if (QWindow *windowHandle = window()->windowHandle()) {
      // `Qt::UniqueConnection` はラムダには使えない。`initializeGL()` は文脈を
      // 作り直すたびに呼ばれるため、繋ぎ直す形で重複を防ぐ。
      disconnect(screenConnection_);
      screenConnection_ =
          connect(windowHandle, &QWindow::screenChanged, this,
                  [](QScreen *newScreen) { DisplayLink::shared().setTargetScreen(newScreen); });
    }

    initializeOpenGLFunctions();
    if (profilingEnabled()) {
      // 表示の上限を決めるのは vsync の設定。1 なら画面の更新に合わせて待つ。
      const QSurfaceFormat surfaceFormat = format();
      qDebug().noquote() << QStringLiteral(
                                "[HeiranProfile] surface swapInterval=%1 samples=%2 gl=%3.%4 "
                                "refresh=%5 Hz vblank=%6 ms buffers=%7 sync=%8")
                                .arg(surfaceFormat.swapInterval())
                                .arg(surfaceFormat.samples())
                                .arg(surfaceFormat.majorVersion())
                                .arg(surfaceFormat.minorVersion())
                                .arg(screen() != nullptr ? screen()->refreshRate() : 0.0)
                                .arg(displayIntervalNs() / 1'000'000.0)
                                .arg(swapBehaviorName(surfaceFormat.swapBehavior()))
                                .arg(displaySyncEnabled() ? 1 : 0);
    }
    if (!renderer_.initialize()) {
      qWarning().noquote() << "[HeiranGpu] failed to initialize the display renderer";
    }
    // 他のセルの都合で捨てると決められたテクスチャは、次に描くときに消える。
    // 描く予定が無いと VRAM が残るため、再描画を促す。
    renderer_.setEvictionWake([this]() { update(); });
    // コンテキストを作り直した場合はテクスチャも作り直す。
    uploadedImageGeneration_ = 0;
  }

  void paintGL() override {
    QElapsedTimer totalTimer;
    if (profilingEnabled()) {
      totalTimer.start();
    }

    // 背景は GL で塗る。QPainter はこの後のオーバーレイだけに使う。
    glClearColor(56.0F / 255.0F, 60.0F / 255.0F, 66.0F / 255.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    const bool hasImage = owner_->hasImage() && width() > 0 && height() > 0;
    const auto finalState =
        heiran::domain::computeFinalViewState(owner_->globalState_, owner_->cellState_);

    qint64 imageDrawDurationNs = 0;
    if (hasImage) {
      QElapsedTimer imageDrawTimer;
      if (profilingEnabled()) {
        imageDrawTimer.start();
      }

      if (uploadedImageGeneration_ != owner_->imageGeneration_) {
        renderer_.setImage(owner_->decodedImage_);
        uploadedImageGeneration_ = owner_->imageGeneration_;
      }

      const QRectF projectedImageRect(-finalState.offsetX * finalState.scale,
                                      -finalState.offsetY * finalState.scale,
                                      owner_->displayImageSize().width() * finalState.scale,
                                      owner_->displayImageSize().height() * finalState.scale);
      const QRectF visibleImageRect = projectedImageRect.intersected(QRectF(rect()));
      if (!visibleImageRect.isEmpty()) {
        // 縮小はテクスチャのミップマップに任せる。段階縮小バッファは使わない。
        const bool minifying = finalState.scale < 1.0;
        const bool smooth = minifying ? owner_->minifyFilter_ == MinifyFilter::Area
                                      : owner_->magnifyFilter_ == MagnifyFilter::Smooth;
        // 透過画素の下地は画像と同じ四角形の中でシェーダが合成する。
        renderer_.draw(owner_->displayTransformParams(), projectedImageRect, size(),
                       devicePixelRatioF(), smooth, owner_->transparencyBackground_);
      }

      if (profilingEnabled()) {
        imageDrawDurationNs = imageDrawTimer.nsecsElapsed();
      }
    }

    QPainter painter(this);
    if (!owner_->hasImage()) {
      paintCellStatus(&painter, rect(), owner_->statusInfo_);
    }

    if (hasImage) {
      paintCellOverlays(&painter,
                        {.scale = finalState.scale,
                         .offsetX = finalState.offsetX,
                         .offsetY = finalState.offsetY,
                         .canvasSize = size(),
                         .selectionRect = owner_->selectionRect_,
                         .previewSelectionRect = owner_->previewSelectionRect_,
                         .sharedCursorPosition = owner_->sharedCursorPosition_,
                         .cursorStyle = owner_->cursorStyle_,
                         // 色を取るときだけ表示用画像を作る (GPU 描画では普段は作らない)。
                         .sampleImage = owner_->cursorStyle_ == CursorStyle::Contrast &&
                                                owner_->sharedCursorPosition_
                                            ? &owner_->displayImage()
                                            : nullptr,
                         .labelText = owner_->cachedExpandedLabel_,
                         .labelRect = labelRectForCurrentState()});
    }

    if (owner_->bordersVisible_) {
      painter.setPen(QColor(52, 55, 61));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    if (profilingEnabled()) {
      paintProfiler().record({totalTimer.nsecsElapsed(), imageDrawDurationNs});
    }
  }

  void contextMenuEvent(QContextMenuEvent *event) override {
    CellContextMenuState menuState;
    menuState.transparencyPattern = owner_->transparencyBackground_.pattern;
    menuState.syncEnabled = owner_->cellState_.syncEnabled;
    menuState.scale = owner_->cellState_.scale;
    menuState.gamma = owner_->cellState_.gamma;
    menuState.rotationQuarterTurns = owner_->cellState_.rotationQuarterTurns;
    menuState.flipX = owner_->cellState_.flipX;
    menuState.flipY = owner_->cellState_.flipY;
    menuState.invert = owner_->cellState_.invert;
    menuState.channelMode = owner_->channelMode_;
    menuState.hasImageSet = owner_->imageSet_ != nullptr;
    menuState.labelVisible = owner_->imageSet_ != nullptr ? owner_->imageSet_->labelVisible : true;
    menuState.imageSetIndex = owner_->imageSetIndex_;
    menuState.availableImageNames = owner_->availableImageNames_;
    menuState.hasImage = owner_->hasImage();
    menuState.cellLabels = owner_->diffMenuCellLabels_;
    menuState.cellIndex = owner_->cellIndex_;
    menuState.diffActive = owner_->diffMenuHasDiff_;
    menuState.diffIsWipe = owner_->diffMenuIsWipe_;

    const QPoint globalPos = event->globalPos();
    const CellContextMenuResult result = showCellContextMenu(this, globalPos, menuState);
    const int cellIndex = owner_->cellIndex_;
    switch (result.action) {
      case CellContextMenuResult::Action::SetTransparencyPattern:
        Q_EMIT owner_->transparencyPatternRequested(result.transparencyPattern);
        break;
      case CellContextMenuResult::Action::ToggleSync:
        Q_EMIT owner_->syncToggleRequested(cellIndex);
        break;
      case CellContextMenuResult::Action::SetScale:
        Q_EMIT owner_->scaleContextMenuRequested(cellIndex, result.scale);
        break;
      case CellContextMenuResult::Action::SetGamma:
        Q_EMIT owner_->gammaContextMenuRequested(cellIndex, result.gamma);
        break;
      case CellContextMenuResult::Action::SetRotation:
        Q_EMIT owner_->rotationContextMenuRequested(cellIndex, result.rotationQuarterTurns);
        break;
      case CellContextMenuResult::Action::SetFlip:
        Q_EMIT owner_->flipContextMenuRequested(cellIndex, result.flipX, result.flipY);
        break;
      case CellContextMenuResult::Action::SetInvert:
        Q_EMIT owner_->invertContextMenuRequested(cellIndex, result.invert);
        break;
      case CellContextMenuResult::Action::SetLabelVisible:
        Q_EMIT owner_->labelVisibilityRequested(cellIndex, result.labelVisible);
        break;
      case CellContextMenuResult::Action::EditLabel:
        Q_EMIT owner_->editLabelRequested(cellIndex);
        break;
      case CellContextMenuResult::Action::AssignImageIndex:
        Q_EMIT owner_->assignImageIndexRequested(cellIndex, result.imageIndex);
        break;
      case CellContextMenuResult::Action::StartDiff:
        Q_EMIT owner_->diffStartRequested(cellIndex, result.diffBaseCell);
        break;
      case CellContextMenuResult::Action::StartWipe:
        Q_EMIT owner_->wipeStartRequested(cellIndex, result.diffBaseCell);
        break;
      case CellContextMenuResult::Action::StopDiff:
        Q_EMIT owner_->diffStopRequested(cellIndex);
        break;
      case CellContextMenuResult::Action::ResetCellDisplay:
        Q_EMIT owner_->resetCellDisplayRequested(cellIndex);
        break;
      case CellContextMenuResult::Action::SetChannelMode:
        Q_EMIT owner_->channelContextMenuRequested(cellIndex, result.channelMode, globalPos);
        break;
      case CellContextMenuResult::Action::CopyImageToClipboard:
        owner_->copyImageToClipboard();
        break;
      case CellContextMenuResult::Action::ShowInFolder:
        showCurrentFileInFolder();
        break;
      case CellContextMenuResult::Action::None:
        break;
    }

    event->accept();
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && owner_->statusInfo_.status == CellStatus::Failed &&
        cellReloadButtonRect(size(), owner_->statusInfo_.status)
            .contains(event->position().toPoint())) {
      Q_EMIT owner_->reloadRequested(owner_->cellIndex_);
      event->accept();
      return;
    }

    if (event->button() == Qt::LeftButton && owner_->hasImage() &&
        !(event->modifiers() & Qt::ShiftModifier)) {
      dragging_ = true;
      lastMousePosition_ = event->position().toPoint();
      Q_EMIT owner_->hoverInfoRequested(owner_->cellIndex_, QPointF(), false);
      setCursor(Qt::ClosedHandCursor);
      event->accept();
      return;
    }

    if (event->button() == Qt::LeftButton && owner_->hasImage() &&
        (event->modifiers() & Qt::ShiftModifier)) {
      if (rect().contains(event->position().toPoint())) {
        selecting_ = true;
        selectionStartImagePosition_ = imageCoordinatesForPosition(event->position());
        Q_EMIT owner_->selectionRequested(owner_->cellIndex_, selectionStartImagePosition_,
                                          selectionStartImagePosition_, false);
        event->accept();
        return;
      }
    }

    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (dragging_) {
      const QPoint deltaPixels = event->position().toPoint() - lastMousePosition_;
      if (owner_->globalState_.baseScale > 0.0) {
        Q_EMIT owner_->panRequested(owner_->cellIndex_,
                                    -deltaPixels.x() / owner_->globalState_.baseScale,
                                    -deltaPixels.y() / owner_->globalState_.baseScale);
      }
      lastMousePosition_ = event->position().toPoint();
      event->accept();
      return;
    }

    updateHoverInfo(event->position());

    if (selecting_) {
      Q_EMIT owner_->selectionRequested(owner_->cellIndex_, selectionStartImagePosition_,
                                        imageCoordinatesForPosition(event->position()), false);
      event->accept();
      return;
    }

    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && dragging_) {
      dragging_ = false;
      unsetCursor();
      updateHoverInfo(event->position());
      event->accept();
      return;
    }

    if (event->button() == Qt::LeftButton && selecting_) {
      selecting_ = false;
      Q_EMIT owner_->selectionRequested(owner_->cellIndex_, selectionStartImagePosition_,
                                        imageCoordinatesForPosition(event->position()), true);
      event->accept();
      return;
    }

    QWidget::mouseReleaseEvent(event);
  }

  // ダブルクリックでフィットと等倍を切り替える。多くのビューアの定番の操作。
  // 1 回目の押下と離しは通常どおり処理される。動かさなければパンにならない。
  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton || !owner_->hasImage() ||
        !rect().contains(event->position().toPoint())) {
      QWidget::mouseDoubleClickEvent(event);
      return;
    }

    const QPointF imageCoordinates = imageCoordinatesForPosition(event->position());
    const double cellScale = owner_->cellState_.scale > 0.0 ? owner_->cellState_.scale : 1.0;
    Q_EMIT owner_->actualSizeToggleRequested(owner_->cellIndex_, imageCoordinates.x() * cellScale,
                                             imageCoordinates.y() * cellScale);
    event->accept();
  }

  void wheelEvent(QWheelEvent *event) override {
    if (!rect().contains(event->position().toPoint())) {
      pageWheelController_.reset();
      QWidget::wheelEvent(event);
      return;
    }

    if (const auto pages = pageWheelController_.consume(*event)) {
      if (*pages != 0) {
        Q_EMIT owner_->pageScrollRequested(*pages);
      }
      event->accept();
      return;
    }

    if (!owner_->hasImage() || event->angleDelta().y() == 0) {
      QWidget::wheelEvent(event);
      return;
    }

    constexpr double kZoomStep = 1.1;
    const double zoomFactor =
        std::pow(kZoomStep, static_cast<double>(event->angleDelta().y()) / 120.0);
    const QPointF imageCoordinates = imageCoordinatesForPosition(event->position());
    const double cellScale = owner_->cellState_.scale > 0.0 ? owner_->cellState_.scale : 1.0;
    Q_EMIT owner_->zoomRequested(owner_->cellIndex_, zoomFactor, imageCoordinates.x() * cellScale,
                                 imageCoordinates.y() * cellScale);
    event->accept();
  }

  void leaveEvent(QEvent *event) override {
    pageWheelController_.reset();
    Q_EMIT owner_->hoverInfoRequested(owner_->cellIndex_, QPointF(), false);
    QWidget::leaveEvent(event);
  }

  void dragEnterEvent(QDragEnterEvent *event) override {
    if (event->source() == this || !event->mimeData()->hasUrls()) {
      event->ignore();
      return;
    }

    QStringList localPaths;
    for (const auto &url : event->mimeData()->urls()) {
      if (url.isLocalFile()) {
        localPaths.push_back(url.toLocalFile());
      }
    }

    if (localPaths.isEmpty()) {
      event->ignore();
      return;
    }

    event->acceptProposedAction();
  }

  void dragMoveEvent(QDragMoveEvent *event) override {
    if (event->source() != this && event->mimeData()->hasUrls()) {
      event->acceptProposedAction();
      return;
    }

    event->ignore();
  }

  void dropEvent(QDropEvent *event) override {
    if (event->source() == this || !event->mimeData()->hasUrls()) {
      event->ignore();
      return;
    }

    QStringList localPaths;
    for (const auto &url : event->mimeData()->urls()) {
      if (url.isLocalFile()) {
        localPaths.push_back(url.toLocalFile());
      }
    }

    if (localPaths.isEmpty()) {
      event->ignore();
      return;
    }

    Q_EMIT owner_->dropItemsRequested(owner_->cellIndex_, localPaths);
    event->acceptProposedAction();
  }

public:
  void setFileDragSource(const QString &path) { fileDragController_.setSourceFile(path); }

private:
  ImageViewWidget *owner_;
  FileDragController fileDragController_;
  GpuImageRenderer renderer_;
  PageWheelController pageWheelController_;
  // renderer_ へ載せた画像の世代。owner_ 側の世代と違えばテクスチャを作り直す。
  quint64 uploadedImageGeneration_ = 0;
  QElapsedTimer presentTimer_;
  QElapsedTimer lastUpdateTimer_;
  bool updatePending_ = false;
  bool fallbackScheduled_ = false;
  QElapsedTimer updateRequestTimer_;
  bool updateRequestPending_ = false;
  QElapsedTimer tickTimer_;
  QMetaObject::Connection screenConnection_;

  // 画面の更新間隔。取れない場合は 60Hz とみなす。
  qint64 displayIntervalNs() const {
    const QScreen *targetScreen = screen();
    const qreal refreshRate = targetScreen != nullptr ? targetScreen->refreshRate() : 0.0;
    if (refreshRate < 1.0) {
      return 16'666'667;
    }
    return static_cast<qint64>(1'000'000'000.0 / refreshRate);
  }

  bool dragging_ = false;
  bool selecting_ = false;
  QPoint lastMousePosition_;
  QPointF selectionStartImagePosition_;
};

ImageViewWidget::ImageViewWidget(QWidget *parent) : QWidget(parent) {
  setMinimumSize(160, 120);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  headerWidget_ = new CellHeaderWidget(this);
  headerWidget_->setObjectName("cellHeader");
  canvasWidget_ = new ImageCanvasWidget(this);
  // マウス操作を伴うテストが送り先を特定するために使う。キャンバスの型は
  // この翻訳単位の中にしかないため、名前で引けるようにしておく。
  canvasWidget_->setObjectName("imageCanvas");
  // 操作の対象のセルを示す枠。画像の面 (GPU) の上へ重ねる軽い部品で、マウスは下へ通す。
  activeFrameWidget_ = new ActiveFrameWidget(this);
  activeFrameWidget_->hide();
  wipeHandleWidget_ = new WipeHandleWidget(this, true);
  wipeHandleWidget_->hide();
  diffBar_ = new DiffBarWidget(this);
  diffBar_->hide();
  connect(diffBar_, &DiffBarWidget::diffChanged, this,
          [this](const heiran::domain::CellDiff &diff) {
            Q_EMIT diffChangeRequested(cellIndex_, diff);
          });
  connect(diffBar_, &DiffBarWidget::closeRequested, this,
          [this]() { Q_EMIT diffStopRequested(cellIndex_); });
  layout->addWidget(headerWidget_);
  layout->addWidget(canvasWidget_, 1);
}

void ImageViewWidget::setDiffBar(const std::optional<heiran::domain::CellDiff> &diff,
                                 const QStringList &cellLabels, bool ownsImage) {
  diffBarEnabled_ = diff.has_value();
  if (!diff) {
    diffBar_->hide();
    wipeDiff_.reset();
    updateWipeHandle();
    return;
  }
  diffBar_->setDiff(*diff, cellLabels, cellIndex_, ownsImage);
  updateDiffBarVisibility();
  wipeDiff_ = diff->settings.mode == heiran::domain::DiffMode::Wipe ? diff : std::nullopt;
  updateWipeHandle();
}

void ImageViewWidget::setDiffTitle(const QString &title) {
  if (diffTitle_ == title) {
    return;
  }
  diffTitle_ = title;
  rebuildOverlayCache();
  headerWidget_->update();
}

void ImageViewWidget::updateDiffBarVisibility() {
  const bool wanted = diffBarEnabled_ && (underMouse() || diffBar_->isInteracting());
  if (wanted == diffBar_->isVisible()) {
    return;
  }
  diffBar_->setVisible(wanted);
  if (wanted) {
    placeDiffBar();
  }
}

void ImageViewWidget::enterEvent(QEnterEvent *event) {
  QWidget::enterEvent(event);
  updateDiffBarVisibility();
}

void ImageViewWidget::leaveEvent(QEvent *event) {
  QWidget::leaveEvent(event);
  // 一覧を開くと、マウスはこのセルの外へ出る。少し待って、入力中でなければ隠す。
  QTimer::singleShot(150, this, [this]() { updateDiffBarVisibility(); });
}

void ImageViewWidget::placeDiffBar() {
  // 画像の面の下端に、画像へ重ねる。画像の面の大きさは変えない。
  // 上端にはセルのラベルが出るため、下に置く。
  const int barHeight = diffBar_->sizeHint().height();
  diffBar_->setGeometry(0, height() - barHeight, width(), barHeight);
  diffBar_->raise();
}

void ImageViewWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (activeFrameWidget_ != nullptr && activeFrame_) {
    activeFrameWidget_->setGeometry(rect());
  }
  updateWipeHandle();
  if (diffBar_ != nullptr && diffBar_->isVisible()) {
    placeDiffBar();
  }
  Q_EMIT cellResized();
}

void ImageViewWidget::setDiffBarResult(const std::optional<heiran::domain::DiffMetrics> &metrics,
                                       const QString &message) {
  diffBar_->setResult(metrics, message);
}

void ImageViewWidget::setDiffMenuInfo(const QStringList &cellLabels, bool hasDiff,
                                      bool diffIsWipe) {
  diffMenuCellLabels_ = cellLabels;
  diffMenuHasDiff_ = hasDiff;
  diffMenuIsWipe_ = diffIsWipe;
}

void ImageViewWidget::setCellInfo(const heiran::domain::ImageSet *imageSet, int imageSetIndex,
                                  int cellIndex) {
  imageSet_ = imageSet;
  imageSetIndex_ = imageSetIndex;
  cellIndex_ = cellIndex;
  updateColorSpaceOverride();
  updateLumaWeights();
  rebuildDisplayImage();
  rebuildOverlayCache();
  headerWidget_->update();
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setAvailableImageNames(const QStringList &imageNames) {
  availableImageNames_ = imageNames;
}

void ImageViewWidget::setDecodedImage(const heiran::infra::DecodedImage &decodedImage,
                                      const QString &sourcePath, const CellStatusInfo &statusInfo) {
  decodedImage_ = decodedImage;
  updateLumaWeights();
  ++imageGeneration_;
  sourceImage_ = decodedImage.displayImage;
  sourcePath_ = sourcePath;
  statusInfo_ = statusInfo;
  canvasWidget_->setFileDragSource(
      hasImage() && statusInfo.status == CellStatus::Ready ? sourcePath : QString());
  rebuildDisplayImage();
  rebuildOverlayCache();
  headerWidget_->update();
  canvasWidget_->requestCoalescedUpdate();
  updateWipeHandle();
}

void ImageViewWidget::setAnalysisValueSpace(AnalysisValueSpace valueSpace) {
  if (analysisValueSpace_ == valueSpace) {
    return;
  }

  // 表示には効かない。解析の値だけが変わるため、描き直しも要らない。
  analysisValueSpace_ = valueSpace;
}

void ImageViewWidget::setColorLutOptions(const ColorLutOptions &options) {
  if (colorLutOptions_ == options) {
    return;
  }

  colorLutOptions_ = options;
  // 焼き直しが要る。次に引かれたときに作り直す。
  colorLut_.reset();
  rebuildDisplayImage();
  update();
}

void ImageViewWidget::setSessionDirectory(const QString &sessionDirectory) {
  if (sessionDirectory_ == sessionDirectory) {
    return;
  }

  sessionDirectory_ = sessionDirectory;
  updateColorSpaceOverride();
  updateLumaWeights();
  rebuildDisplayImage();
  update();
}

void ImageViewWidget::updateColorSpaceOverride() {
  // `.hrn` の指定は埋め込みプロファイルより強い。撮影機材や書き出しの都合で
  // 埋め込みが誤っている画像を、設定側で正すために要る。
  colorSpaceOverride_ =
      imageSet_ != nullptr ? colorSpaceFromSessionSpec(imageSet_->colorSpaceName,
                                                       imageSet_->iccProfilePath, sessionDirectory_)
                           : QColorSpace();
}

const QColorSpace &ImageViewWidget::effectiveColorSpace() const {
  return colorSpaceOverride_.isValid() ? colorSpaceOverride_ : decodedImage_.colorSpace;
}

void ImageViewWidget::updateLumaWeights() {
  // 重みは原色で決まる。復号結果か指定が変わったときだけ求め直せばよい。
  lumaWeights_ = lumaWeightsFor(effectiveColorSpace());
  rgbToXyz_ = rgbToXyzFor(effectiveColorSpace());
}

void ImageViewWidget::setPixelValueFormat(PixelValueFormat pixelValueFormat) {
  pixelValueFormat_ = pixelValueFormat;
}

void ImageViewWidget::setTransparencyBackground(const TransparencyBackground &background) {
  if (transparencyBackground_ == background) {
    return;
  }
  transparencyBackground_ = background;
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setFilters(MagnifyFilter magnifyFilter, MinifyFilter minifyFilter) {
  if (magnifyFilter_ == magnifyFilter && minifyFilter_ == minifyFilter) {
    return;
  }

  magnifyFilter_ = magnifyFilter;
  minifyFilter_ = minifyFilter;
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setCursorStyle(CursorStyle style) {
  if (cursorStyle_ == style) {
    return;
  }
  cursorStyle_ = style;
  canvasWidget_->requestCoalescedUpdate();
}

// 境目の線を、画像の位置 (拡大と移動)
// に合わせて置く。回転や反転があるときは、線の向きが画像と合わないため、出さない
// (位置は、帯の設定でも変えられる)。
void ImageViewWidget::updateWipeHandle() {
  if (wipeHandleWidget_ == nullptr) {
    return;
  }
  const bool unrotated =
      cellState_.rotationQuarterTurns % 4 == 0 && !cellState_.flipX && !cellState_.flipY;
  const QSize imageSize = displayImageSize();
  if (!wipeDiff_ || !unrotated || imageSize.isEmpty() || !hasImage() || canvasWidget_ == nullptr) {
    wipeHandleWidget_->hide();
    return;
  }
  const auto finalState = heiran::domain::computeFinalViewState(globalState_, cellState_);
  const bool vertical =
      wipeDiff_->settings.wipeOrientation == heiran::domain::WipeOrientation::LeftRight;
  static_cast<WipeHandleWidget *>(wipeHandleWidget_)->setVertical(vertical);
  constexpr int kStrip = 15;
  const QRect canvasRect = canvasWidget_->geometry();
  QRect strip;
  if (vertical) {
    const double x = (wipeDiff_->settings.wipePosition * imageSize.width() - finalState.offsetX) *
                     finalState.scale;
    if (x < 0.0 || x > canvasRect.width()) {
      wipeHandleWidget_->hide();
      return;
    }
    strip = QRect(canvasRect.left() + static_cast<int>(std::lround(x)) - kStrip / 2,
                  canvasRect.top(), kStrip, canvasRect.height());
  } else {
    const double y = (wipeDiff_->settings.wipePosition * imageSize.height() - finalState.offsetY) *
                     finalState.scale;
    if (y < 0.0 || y > canvasRect.height()) {
      wipeHandleWidget_->hide();
      return;
    }
    strip =
        QRect(canvasRect.left(), canvasRect.top() + static_cast<int>(std::lround(y)) - kStrip / 2,
              canvasRect.width(), kStrip);
  }
  wipeHandleWidget_->setGeometry(strip);
  wipeHandleWidget_->show();
  wipeHandleWidget_->raise();
  if (diffBar_ != nullptr && diffBar_->isVisible()) {
    diffBar_->raise();
  }
}

QRect ImageViewWidget::wipeHandleGeometry() const {
  return wipeHandleWidget_ != nullptr && wipeHandleWidget_->isVisible()
             ? wipeHandleWidget_->geometry()
             : QRect();
}

// つかんだ位置から、境目の位置 (画像の幅 / 高さに対する割合) を求め、新しい指定を返す。
void ImageViewWidget::dragWipeTo(const QPoint &positionInThis) {
  if (!wipeDiff_ || displayImageSize().isEmpty()) {
    return;
  }
  const auto finalState = heiran::domain::computeFinalViewState(globalState_, cellState_);
  if (finalState.scale <= 0.0) {
    return;
  }
  const QPoint local = positionInThis - canvasWidget_->geometry().topLeft();
  const bool vertical =
      wipeDiff_->settings.wipeOrientation == heiran::domain::WipeOrientation::LeftRight;
  const double imageCoordinate = vertical ? local.x() / finalState.scale + finalState.offsetX
                                          : local.y() / finalState.scale + finalState.offsetY;
  const double extent = vertical ? displayImageSize().width() : displayImageSize().height();
  const double position = std::clamp(imageCoordinate / extent, 0.0, 1.0);
  if (std::abs(position - wipeDiff_->settings.wipePosition) < 1e-4) {
    return;
  }
  wipeDiff_->settings.wipePosition = position;
  updateWipeHandle();
  Q_EMIT diffChangeRequested(cellIndex_, *wipeDiff_);
}

void ImageViewWidget::setActiveFrame(bool visible) {
  if (activeFrame_ == visible) {
    return;
  }
  activeFrame_ = visible;
  if (visible) {
    activeFrameWidget_->setGeometry(rect());
    activeFrameWidget_->raise();
    // 差分の帯は、枠より前に出す。
    if (diffBar_ != nullptr && diffBar_->isVisible()) {
      diffBar_->raise();
    }
  }
  activeFrameWidget_->setVisible(visible);
}

void ImageViewWidget::setHeaderVisible(bool visible) {
  if (headerWidget_->isHidden() == !visible) {
    return;
  }
  headerWidget_->setVisible(visible);
  layout()->activate();
  updateWipeHandle();
  Q_EMIT cellResized();
}

void ImageViewWidget::setBordersVisible(bool visible) {
  if (bordersVisible_ == visible) {
    return;
  }

  bordersVisible_ = visible;
  headerWidget_->update();
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setDisplayProfile(DisplayProfileMode mode, const QString &profilePath) {
  if (displayProfileMode_ == mode && displayProfilePath_ == profilePath) {
    return;
  }

  displayProfileMode_ = mode;
  displayProfilePath_ = profilePath;
  displayColorSpaceDirty_ = true;
  rebuildDisplayImage();
  update();
}

void ImageViewWidget::setExposureStops(double exposureStops) {
  if (std::abs(exposureStops_ - exposureStops) < 0.0001) {
    return;
  }

  exposureStops_ = exposureStops;
  rebuildDisplayImage();
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setChannelMode(ChannelMode channelMode) {
  if (channelMode_ == channelMode) {
    return;
  }

  channelMode_ = channelMode;
  rebuildDisplayImage();
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setViewState(const heiran::domain::GlobalViewState &globalState,
                                   const heiran::domain::CellViewState &cellState) {
  const auto oldFinalState = heiran::domain::computeFinalViewState(globalState_, cellState_);
  const bool headerChanged = cellState_.syncEnabled != cellState.syncEnabled ||
                             std::abs(cellState_.scale - cellState.scale) > 0.0001;
  const bool rebuildNeeded = std::abs(cellState_.gamma - cellState.gamma) > 0.0001 ||
                             cellState_.rotationQuarterTurns != cellState.rotationQuarterTurns ||
                             cellState_.flipX != cellState.flipX ||
                             cellState_.flipY != cellState.flipY ||
                             cellState_.invert != cellState.invert;
  const bool overlayRebuildNeeded = std::abs(cellState_.scale - cellState.scale) > 0.0001;

  globalState_ = globalState;
  cellState_ = cellState;

  if (rebuildNeeded) {
    rebuildDisplayImage();
  }
  if (overlayRebuildNeeded) {
    rebuildOverlayCache();
  }

  if (headerChanged) {
    headerWidget_->update();
  }
  const auto newFinalState = heiran::domain::computeFinalViewState(globalState_, cellState_);
  const bool purePanUpdate = !rebuildNeeded && !overlayRebuildNeeded &&
                             std::abs(oldFinalState.scale - newFinalState.scale) < 0.0001 &&
                             (std::abs(oldFinalState.offsetX - newFinalState.offsetX) > 0.0001 ||
                              std::abs(oldFinalState.offsetY - newFinalState.offsetY) > 0.0001);
  if (purePanUpdate) {
    const int dx = static_cast<int>(
        std::lround((oldFinalState.offsetX - newFinalState.offsetX) * newFinalState.scale));
    const int dy = static_cast<int>(
        std::lround((oldFinalState.offsetY - newFinalState.offsetY) * newFinalState.scale));
    canvasWidget_->panContentsByPixels(dx, dy);
  } else {
    canvasWidget_->requestCoalescedUpdate();
  }
  updateWipeHandle();
}

void ImageViewWidget::setSelectionRect(const std::optional<QRectF> &selectionRect,
                                       const std::optional<QRectF> &previewSelectionRect) {
  selectionRect_ = selectionRect;
  previewSelectionRect_ = previewSelectionRect;
  canvasWidget_->requestCoalescedUpdate();
}

void ImageViewWidget::setSharedCursorPosition(const std::optional<QPointF> &imagePosition) {
  sharedCursorPosition_ = imagePosition;
  canvasWidget_->requestCoalescedUpdate();
}

QString ImageViewWidget::pixelInfoAtImagePosition(const QPointF &imagePosition) const {
  return canvasWidget_->pixelInfoAtImagePosition(imagePosition);
}

qint64 ImageViewWidget::gpuTextureBytes() const {
  return canvasWidget_->gpuTextureBytes();
}

qint64 ImageViewWidget::gpuCachedTextureBytes() const {
  return canvasWidget_->gpuCachedTextureBytes();
}

QString ImageViewWidget::displayLabel() const {
  return imageSet_ != nullptr ? imageSet_->name : QString("Cell %1").arg(cellIndex_ + 1);
}

const QImage &ImageViewWidget::currentImage() const {
  return displayImage();
}

void ImageViewWidget::copyImageToClipboard() const {
  const QImage &image = displayImage();
  if (!image.isNull()) {
    QGuiApplication::clipboard()->setImage(image);
  }
}

const heiran::infra::DecodedImage &ImageViewWidget::currentDecodedImage() const {
  return decodedImage_;
}

ChannelMode ImageViewWidget::channelMode() const {
  return channelMode_;
}

QSize ImageViewWidget::canvasSize() const {
  return canvasWidget_ != nullptr ? canvasWidget_->size() : QSize();
}

CellStatus ImageViewWidget::cellStatus() const {
  return statusInfo_.status;
}

AnalysisSampler ImageViewWidget::analysisSampler() const {
  // 線形へ戻す曲線は元画像の色空間から取る。表示先は関係しない。
  const auto sourceTransfer =
      makeColorTransformParams(effectiveColorSpace(), effectiveColorSpace());
  return {
      .decodedImage = decodedImage_,
      .orientation = {.rotationQuarterTurns = cellState_.rotationQuarterTurns,
                      .flipX = cellState_.flipX,
                      .flipY = cellState_.flipY},
      .sourceSize = sourceImage_.size(),
      .lumaWeights = lumaWeights_,
      .rgbToXyz = rgbToXyz_,
      .valueSpace = analysisValueSpace_,
      .sourceTransfer = sourceTransfer.sourceTransfer,
      .sourceGamma = sourceTransfer.sourceGamma,
  };
}

QPoint ImageViewWidget::sourcePixelForDisplayPixel(const QPoint &displayPixel) const {
  if (sourceImage_.isNull()) {
    return displayPixel;
  }

  const heiran::domain::ImageOrientation orientation{
      .rotationQuarterTurns = cellState_.rotationQuarterTurns,
      .flipX = cellState_.flipX,
      .flipY = cellState_.flipY,
  };
  return heiran::domain::mapTransformedToSource(displayPixel, sourceImage_.size(), orientation);
}

QPoint ImageViewWidget::originalFilePixelForDisplayPixel(const QPoint &displayPixel) const {
  const QPoint decodedPixel = sourcePixelForDisplayPixel(displayPixel);
  if (decodedImage_.appliedExifOrientation == heiran::infra::ExifOrientation::Normal) {
    return decodedPixel;
  }

  // 復号結果は EXIF を適用した後の並びになっている。元ファイルの座標へ戻すには、
  // 利用者が指定した回転と反転を戻したうえで、EXIF の分をもう一段戻す。
  const auto exifTransform = heiran::infra::toExifTransform(decodedImage_.appliedExifOrientation);
  const heiran::domain::ImageOrientation exifOrientation{
      .rotationQuarterTurns = exifTransform.rotationQuarterTurns,
      .flipX = exifTransform.flipX,
      .flipY = exifTransform.flipY,
  };
  // 元ファイルの大きさは、適用した回転で縦横が入れ替わっている場合は戻したもの。
  const QSize originalSize = heiran::domain::transformedImageSize(
      sourceImage_.size(),
      heiran::domain::normalizeQuarterTurns(-exifTransform.rotationQuarterTurns));
  return heiran::domain::mapTransformedToSource(decodedPixel, originalSize, exifOrientation);
}

std::shared_ptr<const ColorLut> ImageViewWidget::colorLutFor(const QColorSpace &source,
                                                             const QColorSpace &display) const {
  // 表の組み立ては刻みの 3 乗の回数だけ変換を通すため、描画のたびには行えない。
  // 組み合わせが変わったときだけ作り直す。
  if (colorLut_ && colorLutSource_ == source && colorLutDisplay_ == display &&
      colorLutBuiltOptions_ == colorLutOptions_) {
    return colorLut_;
  }

  colorLut_ = buildColorLut(source, display, colorLutOptions_);
  colorLutSource_ = source;
  colorLutDisplay_ = display;
  colorLutBuiltOptions_ = colorLutOptions_;
  return colorLut_;
}

const QColorSpace &ImageViewWidget::resolvedDisplayColorSpace() const {
  // 画面をまたいで動かすとプロファイルが変わる。画面が変わったときも解決し直す。
  const QScreen *currentScreen = screen();
  if (displayColorSpaceDirty_ || displayColorSpaceScreen_ != currentScreen) {
    displayColorSpace_ =
        resolveDisplayColorSpace(displayProfileMode_, displayProfilePath_, currentScreen);
    displayColorSpaceScreen_ = currentScreen;
    displayColorSpaceDirty_ = false;
  }
  return displayColorSpace_;
}

DisplayTransformParams ImageViewWidget::displayTransformParams() const {
  const QColorSpace &displayColorSpace = resolvedDisplayColorSpace();
  const QColorSpace &sourceColorSpace = effectiveColorSpace();
  const bool needsConversion = sourceColorSpace.isValid() && displayColorSpace.isValid() &&
                               sourceColorSpace != displayColorSpace;
  // 行列と曲線で表せる組み合わせはそのまま計算する。表せない場合 (LUT を持つ
  // ICC プロファイルや PQ / HLG) は、変換を焼いた 3 次元の表から引く。
  const bool representable =
      isShaderRepresentable(sourceColorSpace) && isShaderRepresentable(displayColorSpace);

  std::shared_ptr<const ColorLut> colorLut;
  if (needsConversion && !representable) {
    colorLut = colorLutFor(sourceColorSpace, displayColorSpace);
  }
  const bool colorManaged = needsConversion && (representable || colorLut != nullptr);

  return {
      .channelMode = channelMode_,
      .gamma = cellState_.gamma,
      .exposureStops = exposureStops_,
      .invert = cellState_.invert,
      .lumaWeights = lumaWeights_,
      .colorManaged = colorManaged,
      .colorTransform = colorManaged ? makeColorTransformParams(sourceColorSpace, displayColorSpace)
                                     : ColorTransformParams{},
      .colorLut = colorLut,
      .rotationQuarterTurns = cellState_.rotationQuarterTurns,
      .flipX = cellState_.flipX,
      .flipY = cellState_.flipY,
  };
}

bool ImageViewWidget::hasImage() const {
  return !sourceImage_.isNull();
}

QSize ImageViewWidget::displayImageSize() const {
  return heiran::domain::transformedImageSize(
      sourceImage_.size(), heiran::domain::normalizeQuarterTurns(cellState_.rotationQuarterTurns));
}

// 表示用画像は要求されたときだけ作る。
// 画面の描画で使わなくなれば、変換のコストは解析側の要求時だけになる。
const QImage &ImageViewWidget::displayImage() const {
  if (!displayImageDirty_) {
    return image_;
  }

  displayImageDirty_ = false;
  if (sourceImage_.isNull()) {
    image_ = QImage();
    return image_;
  }

  QElapsedTimer totalTimer;
  if (loadProfilingEnabled()) {
    totalTimer.start();
  }

  image_ = renderDisplayImageOnCpu(decodedImage_, displayTransformParams());
  displayImageProfiler().record({loadProfilingEnabled() ? totalTimer.nsecsElapsed() : 0, 0, 0, 0});
  return image_;
}

void ImageViewWidget::rebuildDisplayImage() {
  displayImageDirty_ = true;
  image_ = QImage();
}

void ImageViewWidget::rebuildOverlayCache() {
  // 倍率は百分率で。1.0 が 100%、0.375 が 37.5%。
  cachedScaleText_ = QString("%1%").arg(cellState_.scale * 100.0, 0, 'g', 4);

  if (statusInfo_.status == CellStatus::Ready && !diffTitle_.isEmpty()) {
    // 差分のセル。ファイルではなく、何と何の差かを出す。
    cachedPathText_ = diffTitle_;
    cachedFileBytesPath_.clear();
    cachedInfoText_ = formatImageInfoText(QStringLiteral("diff"), decodedImage_.size(), -1);
  } else if (statusInfo_.status == CellStatus::Ready && !sourcePath_.isEmpty()) {
    cachedPathText_ = sourcePath_;
    if (cachedFileBytesPath_ != sourcePath_) {
      const QFileInfo fileInfo(sourcePath_);
      cachedFileBytes_ = fileInfo.exists() ? fileInfo.size() : -1;
      cachedFileBytesPath_ = sourcePath_;
    }
    cachedInfoText_ =
        formatImageInfoText(pixelFormatText(decodedImage_), decodedImage_.size(), cachedFileBytes_);
  } else if (statusInfo_.reason.isEmpty()) {
    cachedInfoText_.clear();
    cachedFileBytesPath_.clear();
    cachedPathText_ = cellStatusHeadline(statusInfo_);
  } else {
    cachedInfoText_.clear();
    cachedFileBytesPath_.clear();
    cachedPathText_ = QString("%1: %2").arg(cellStatusHeadline(statusInfo_), statusInfo_.reason);
  }

  if (imageSet_ == nullptr || !imageSet_->labelVisible) {
    cachedExpandedLabel_.clear();
    return;
  }

  QString labelTemplate = imageSet_->label;
  if (labelTemplate.isEmpty()) {
    cachedExpandedLabel_.clear();
    return;
  }

  labelTemplate.replace("{width}", sourceImage_.isNull() ? QString("-")
                                                         : QString::number(sourceImage_.width()));
  labelTemplate.replace("{height}", sourceImage_.isNull() ? QString("-")
                                                          : QString::number(sourceImage_.height()));

  const QFileInfo fileInfo(sourcePath_);
  labelTemplate.replace("{filesize}",
                        fileInfo.exists() ? QString::number(fileInfo.size()) : QString("-"));

  static const QRegularExpression fileSizePattern(R"(\{filesize:([^}]+)\})");
  int offset = 0;
  while (true) {
    const auto match = fileSizePattern.match(labelTemplate, offset);
    if (!match.hasMatch()) {
      break;
    }
    const QString replacement =
        fileInfo.exists() ? formatFileSize(fileInfo.size(), match.captured(1)) : QString("-");
    labelTemplate.replace(match.capturedStart(0), match.capturedLength(0), replacement);
    offset = match.capturedStart(0) + replacement.size();
  }

  cachedExpandedLabel_ = labelTemplate.trimmed();
}

}  // namespace heiran::ui
