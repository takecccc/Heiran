#include "heiran/infra/AnimationComposer.hpp"

#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <cstring>

namespace heiran::infra {

namespace {

// 元の上へ src を重ねる。どちらもアルファを掛けていない 8bit の RGBA。
inline void blendOver(uchar *destination, const uchar *source) {
  const int sourceAlpha = source[3];
  if (sourceAlpha == 255) {
    std::memcpy(destination, source, 4);
    return;
  }
  if (sourceAlpha == 0) {
    return;
  }

  const int destinationAlpha = destination[3];
  // 出力のアルファを 255 倍した値。0 割りは起きない (sourceAlpha > 0)。
  const int scaledAlpha = sourceAlpha * 255 + destinationAlpha * (255 - sourceAlpha);
  for (int channel = 0; channel < 3; ++channel) {
    destination[channel] = static_cast<uchar>(
        (source[channel] * sourceAlpha * 255 +
         destination[channel] * destinationAlpha * (255 - sourceAlpha) + scaledAlpha / 2) /
        scaledAlpha);
  }
  destination[3] = static_cast<uchar>((scaledAlpha + 127) / 255);
}

// フレームをキャンバスへ描く。
void drawFrame(QImage *canvas, const QImage &frameImage, const AnimationFrameControl &control) {
  const int width = static_cast<int>(control.width);
  const int height = static_cast<int>(control.height);
  for (int row = 0; row < height; ++row) {
    const uchar *source = frameImage.constScanLine(row);
    uchar *destination = canvas->scanLine(static_cast<int>(control.yOffset) + row) +
                         static_cast<qsizetype>(control.xOffset) * 4;
    if (control.blendOp == kAnimationBlendSource) {
      std::memcpy(destination, source, static_cast<size_t>(width) * 4);
      continue;
    }
    for (int column = 0; column < width; ++column) {
      blendOver(destination + column * 4, source + column * 4);
    }
  }
}

void clearRegion(QImage *canvas, const AnimationFrameControl &control) {
  for (quint32 row = 0; row < control.height; ++row) {
    std::memset(canvas->scanLine(static_cast<int>(control.yOffset + row)) +
                    static_cast<qsizetype>(control.xOffset) * 4,
                0, static_cast<size_t>(control.width) * 4);
  }
}

bool frameFitsInCanvas(const AnimationFrameControl &control, quint32 canvasWidth,
                       quint32 canvasHeight) {
  return control.width > 0 && control.height > 0 &&
         static_cast<quint64>(control.xOffset) + control.width <= canvasWidth &&
         static_cast<quint64>(control.yOffset) + control.height <= canvasHeight;
}

// キャンバスの状態を、フレーム 1 枚分進める。描いた後の絵を返す (`wantOutput` が偽なら空)。
// 描いた後は、フレームの指定に従って背景や前の状態へ戻す。
QImage composeStep(QImage *canvas, int index, const AnimationFrameControl &control,
                   const QImage &raw, bool wantOutput) {
  const bool usable =
      !raw.isNull() && frameFitsInCanvas(control, static_cast<quint32>(canvas->width()),
                                         static_cast<quint32>(canvas->height()));

  // 最初のフレームの「前の状態へ戻す」は、戻す先が無い。背景へ戻す扱いにする (仕様)。
  quint8 disposeOp = control.disposeOp;
  if (index == 0 && disposeOp == kAnimationDisposePrevious) {
    disposeOp = kAnimationDisposeBackground;
  }

  QImage savedForPrevious;
  if (usable && disposeOp == kAnimationDisposePrevious) {
    savedForPrevious = canvas->copy();
  }
  if (usable) {
    drawFrame(canvas, raw, control);
  }
  // 壊れたフレームは、キャンバスを変えないまま前のコマを繰り返す。
  const QImage output = wantOutput ? canvas->copy() : QImage();
  if (usable) {
    if (disposeOp == kAnimationDisposeBackground) {
      clearRegion(canvas, control);
    } else if (disposeOp == kAnimationDisposePrevious) {
      *canvas = savedForPrevious;
    }
  }
  return output;
}

}  // namespace

AnimationResult composeAllFrames(const AnimationSource &source, qint64 maxBytes,
                                 bool firstFrameOnly) {
  AnimationResult result;
  const qint64 canvasBytes = static_cast<qint64>(source.canvasWidth()) * source.canvasHeight() * 4;

  // 読むフレームの数を、メモリの上限と 1 枚だけの指定から決める。
  qsizetype frameCount = source.frameCount();
  if (firstFrameOnly) {
    frameCount = 1;
  } else if (canvasBytes > 0 && canvasBytes * frameCount > maxBytes) {
    frameCount = std::max<qint64>(1, maxBytes / canvasBytes);
    result.truncated = frameCount < source.frameCount();
  }

  // 復号はフレームごとに独立している。並列に走らせる。
  QList<int> indices;
  indices.reserve(frameCount);
  for (int index = 0; index < frameCount; ++index) {
    indices.push_back(index);
  }
  const QList<QImage> decoded = QtConcurrent::blockingMapped<QList<QImage>>(
      indices, [&source](int index) { return source.decodeFrame(index); });

  // 合成は前のフレームの状態に依るため、順に行う。
  QImage canvas(source.canvasWidth(), source.canvasHeight(), QImage::Format_RGBA8888);
  if (canvas.isNull()) {
    return {.errorMessage = QStringLiteral("Failed to allocate the canvas")};
  }
  canvas.fill(Qt::transparent);

  for (int index = 0; index < frameCount; ++index) {
    const AnimationFrameControl &control = source.control(index);
    const QImage &frameImage = decoded.at(index);
    if (frameImage.isNull() ||
        !frameFitsInCanvas(control, static_cast<quint32>(source.canvasWidth()),
                           static_cast<quint32>(source.canvasHeight()))) {
      if (result.frames.isEmpty()) {
        return {.errorMessage = QStringLiteral("Failed to decode an animation frame")};
      }
      // 途中のフレームが壊れていたら、そこまでを使う。
      result.truncated = true;
      break;
    }

    const QImage output = composeStep(&canvas, index, control, frameImage, true);
    result.frames.push_back({.image = output, .delayMs = control.delayMs});
  }

  result.ok = !result.frames.isEmpty();
  if (!result.ok) {
    result.errorMessage = QStringLiteral("The animation has no decodable frames");
  }
  return result;
}

namespace {

// 圧縮したまま持ち、要るフレームだけを背景で作るアニメーション。
class ComposedFrameStream final : public FrameStream,
                                  public std::enable_shared_from_this<ComposedFrameStream> {
public:
  ComposedFrameStream(std::shared_ptr<const AnimationSource> source, int snapshotInterval)
      : source_(std::move(source)), count_(source_->frameCount()),
        snapshotInterval_(snapshotInterval > 0 ? snapshotInterval
                                               : std::max(30, (count_ + 15) / 16)) {
    delaysMs_.reserve(count_);
    for (int index = 0; index < count_; ++index) {
      delaysMs_.push_back(normalizedFrameDelayMs(source_->control(index).delayMs));
    }
  }

  // 開くときに 1 度だけ、同期で先頭のフレームを作る。
  bool renderFirstFrame(QString *errorMessage) {
    canvas_ = QImage(source_->canvasWidth(), source_->canvasHeight(), QImage::Format_RGBA8888);
    if (canvas_.isNull()) {
      setError(errorMessage, QStringLiteral("Failed to allocate the canvas"));
      return false;
    }
    canvas_.fill(Qt::transparent);
    snapshots_.insert(0, canvas_);

    const QImage raw = source_->decodeFrame(0);
    if (raw.isNull()) {
      setError(errorMessage, QStringLiteral("Failed to decode the first frame"));
      return false;
    }
    window_.insert(0, composeStep(&canvas_, 0, source_->control(0), raw, true));
    stateIndex_ = 1;
    return true;
  }

  int frameCount() const override { return count_; }

  int delayMs(int index) const override { return delaysMs_.at(wrap(index)); }

  QImage frameIfReady(int index) override {
    const QMutexLocker locker(&mutex_);
    return window_.value(wrap(index));
  }

  void request(int index) override {
    bool start = false;
    {
      const QMutexLocker locker(&mutex_);
      wanted_ = wrap(index);
      if (!jobRunning_) {
        jobRunning_ = true;
        start = true;
      }
    }
    if (start) {
      // 実行中は、この共有ポインタがストリームを生かす。
      auto self = shared_from_this();
      pool().start([self]() { self->runProducer(); });
    }
  }

  qint64 residentBytes() const override {
    // 抱え得る上限。圧縮したデータ、窓のフレーム、キャンバスの写し。
    const qint64 canvasBytes =
        static_cast<qint64>(source_->canvasWidth()) * source_->canvasHeight() * 4;
    const int maxSnapshots = count_ / snapshotInterval_ + 1;
    return source_->compressedBytes() + canvasBytes * (kLookahead + kKeepBehind + maxSnapshots + 2);
  }

  void setListener(const void *owner, std::function<void()> listener) override {
    const QMutexLocker locker(&listenerMutex_);
    listeners_.insert(owner, std::move(listener));
  }

  void removeListener(const void *owner) override {
    // 呼び出しの最中は待つ。戻った後は、もう呼ばれない。
    const QMutexLocker locker(&listenerMutex_);
    listeners_.remove(owner);
  }

  int residentFrameCountForTesting() const override {
    const QMutexLocker locker(&mutex_);
    return static_cast<int>(window_.size());
  }

private:
  // 先に作っておくフレームの数と、見終えた後ろへ残す数。
  static constexpr int kLookahead = 6;
  static constexpr int kKeepBehind = 2;
  // 並列に復号する数。
  static constexpr int kBatch = 4;

  static void setError(QString *errorMessage, const QString &text) {
    if (errorMessage != nullptr) {
      *errorMessage = text;
    }
  }

  static QThreadPool &pool() {
    // 作る側は 1 つだけ走らせる。復号の並列は、その中で全体のプールを使う。
    static QThreadPool instance;
    instance.setMaxThreadCount(1);
    return instance;
  }

  int wrap(int index) const { return ((index % count_) + count_) % count_; }

  void notifyListeners() {
    const QMutexLocker locker(&listenerMutex_);
    for (const auto &listener : std::as_const(listeners_)) {
      listener();
    }
  }

  // `index` の頭の状態まで戻すか、進める。いまの状態が使えて、写しより近ければそのまま。
  void prepareState(int index) {
    const QMutexLocker locker(&mutex_);
    int bestSnapshot = 0;
    for (auto it = snapshots_.constBegin(); it != snapshots_.constEnd(); ++it) {
      if (it.key() <= index) {
        bestSnapshot = std::max(bestSnapshot, it.key());
      }
    }
    const bool currentUsable = stateIndex_ <= index;
    if (!currentUsable || bestSnapshot > stateIndex_) {
      canvas_ = snapshots_.value(bestSnapshot);
      stateIndex_ = bestSnapshot;
    }
  }

  // 要るフレームの集合。先の数フレームと、見終えた後ろの数フレーム (巻き戻りを含む)。
  QSet<int> neededFrames(int target) const {
    QSet<int> needed;
    for (int offset = -kKeepBehind; offset < kLookahead; ++offset) {
      needed.insert(wrap(target + offset));
    }
    return needed;
  }

  void runProducer() {
    while (true) {
      int target = 0;
      int missing = -1;
      QSet<int> needed;
      {
        const QMutexLocker locker(&mutex_);
        target = wanted_;
        needed = neededFrames(target);
        // 窓の外を捨てる。
        for (auto it = window_.begin(); it != window_.end();) {
          it = needed.contains(it.key()) ? std::next(it) : window_.erase(it);
        }
        // 先に要る順で、最初の作っていないもの。
        for (int offset = 0; offset < kLookahead; ++offset) {
          const int index = wrap(target + offset);
          if (!window_.contains(index)) {
            missing = index;
            break;
          }
        }
        if (missing < 0) {
          jobRunning_ = false;
          return;
        }
      }

      prepareState(missing);
      // 状態から missing まで、組にして復号しながら進める。
      bool stored = false;
      while (stateIndex_ <= missing) {
        const int batchEnd = std::min(missing + 1, stateIndex_ + kBatch);
        QList<int> indices;
        for (int index = stateIndex_; index < batchEnd; ++index) {
          indices.push_back(index);
        }
        const QList<QImage> raws = QtConcurrent::blockingMapped<QList<QImage>>(
            indices, [this](int index) { return source_->decodeFrame(index); });

        for (int position = 0; position < indices.size(); ++position) {
          const int index = indices.at(position);
          {
            // 一定間隔で、この頭の状態の写しを取っておく。
            const QMutexLocker locker(&mutex_);
            if (index % snapshotInterval_ == 0 && !snapshots_.contains(index)) {
              snapshots_.insert(index, canvas_);
            }
          }
          const bool wanted = needed.contains(index);
          const QImage output =
              composeStep(&canvas_, index, source_->control(index), raws.at(position), wanted);
          stateIndex_ = index + 1;
          if (wanted) {
            const QMutexLocker locker(&mutex_);
            window_.insert(index, output);
            stored = true;
          }
        }

        // 見せる側が飛んだ (別の場所を求めた) なら、この続きは無駄になる。作り直す。
        const QMutexLocker locker(&mutex_);
        if (wanted_ != target) {
          break;
        }
      }
      if (stored) {
        notifyListeners();
      }
    }
  }

  const std::shared_ptr<const AnimationSource> source_;
  const int count_;
  const int snapshotInterval_;
  QVector<int> delaysMs_;

  mutable QMutex mutex_;
  // mutex_ で守る。作り終えたフレームと、キャンバスの写し (フレームの頭の状態)。
  QHash<int, QImage> window_;
  QMap<int, QImage> snapshots_;
  int wanted_ = 0;
  bool jobRunning_ = false;

  // 作る側のスレッドだけが触る。canvas_ は stateIndex_ 番目のフレームの頭の状態。
  QImage canvas_;
  int stateIndex_ = 0;

  QMutex listenerMutex_;
  QHash<const void *, std::function<void()>> listeners_;
};

}  // namespace

std::shared_ptr<FrameStream> openAnimationStream(std::shared_ptr<const AnimationSource> source,
                                                 QString *errorMessage, int snapshotInterval) {
  if (source == nullptr || source->frameCount() <= 0) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("The animation has no frames");
    }
    return nullptr;
  }

  auto stream = std::make_shared<ComposedFrameStream>(std::move(source), snapshotInterval);
  if (!stream->renderFirstFrame(errorMessage)) {
    return nullptr;
  }
  return stream;
}

}  // namespace heiran::infra
