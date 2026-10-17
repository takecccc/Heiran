#include "heiran/infra/GifStream.hpp"

#include <QFile>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThreadPool>
#include <algorithm>
#include <zstd.h>

namespace heiran::infra {

namespace {

// 走査の途中で範囲を超えたことを知らせる読み手。
class ByteCursor {
public:
  explicit ByteCursor(const QByteArray &bytes) : bytes_(bytes) {}

  bool atEnd() const { return position_ >= bytes_.size(); }
  bool skip(qsizetype count) {
    if (count < 0 || position_ + count > bytes_.size()) {
      return false;
    }
    position_ += count;
    return true;
  }
  bool readByte(int *value) {
    if (atEnd()) {
      return false;
    }
    *value = static_cast<uchar>(bytes_.at(position_++));
    return true;
  }
  bool readU16(int *value) {
    int low = 0;
    int high = 0;
    if (!readByte(&low) || !readByte(&high)) {
      return false;
    }
    *value = low | (high << 8);
    return true;
  }
  // サブブロックの列を、終わり (長さ 0) まで読み飛ばす。
  bool skipSubBlocks() {
    while (true) {
      int size = 0;
      if (!readByte(&size)) {
        return false;
      }
      if (size == 0) {
        return true;
      }
      if (!skip(size)) {
        return false;
      }
    }
  }

private:
  const QByteArray &bytes_;
  qsizetype position_ = 0;
};

}  // namespace

GifInfo probeGif(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray bytes = file.readAll();
  if (bytes.size() < 13 || !(bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a"))) {
    return {};
  }

  ByteCursor cursor(bytes);
  GifInfo info;
  cursor.skip(6);
  int flags = 0;
  if (!cursor.readU16(&info.width) || !cursor.readU16(&info.height) || !cursor.readByte(&flags) ||
      !cursor.skip(2)) {
    return {};
  }
  if ((flags & 0x80) != 0 && !cursor.skip(3 * (1 << ((flags & 0x07) + 1)))) {
    return {};
  }

  // 次に来る画像へかかる表示時間。画像記述子ごとに使い切る。
  int pendingDelayMs = 0;
  while (true) {
    int introducer = 0;
    if (!cursor.readByte(&introducer) || introducer == 0x3B) {
      break;
    }
    if (introducer == 0x21) {
      int label = 0;
      if (!cursor.readByte(&label)) {
        break;
      }
      if (label == 0xF9) {
        int size = 0;
        int packed = 0;
        int delayCs = 0;
        if (!cursor.readByte(&size) || size < 4 || !cursor.readByte(&packed) ||
            !cursor.readU16(&delayCs) || !cursor.skip(size - 3)) {
          break;
        }
        pendingDelayMs = delayCs * 10;
      }
      if (!cursor.skipSubBlocks()) {
        break;
      }
    } else if (introducer == 0x2C) {
      int descriptorFlags = 0;
      int ignored = 0;
      if (!cursor.skip(8) || !cursor.readByte(&descriptorFlags)) {
        break;
      }
      if ((descriptorFlags & 0x80) != 0 &&
          !cursor.skip(3 * (1 << ((descriptorFlags & 0x07) + 1)))) {
        break;
      }
      // 最小符号長と、圧縮した画素のサブブロック。
      if (!cursor.readByte(&ignored) || !cursor.skipSubBlocks()) {
        break;
      }
      info.delaysMs.push_back(pendingDelayMs);
      pendingDelayMs = 0;
      ++info.frameCount;
    } else {
      break;
    }
  }

  info.valid = info.width > 0 && info.height > 0 && info.frameCount > 0;
  return info;
}

namespace {

QByteArray compressFrame(const QImage &image) {
  const size_t sourceBytes = static_cast<size_t>(image.sizeInBytes());
  QByteArray compressed(static_cast<qsizetype>(ZSTD_compressBound(sourceBytes)), Qt::Uninitialized);
  // レベル 1。実フレームで、LZ4 と同じ速さのまま、より小さくなる。
  const size_t written =
      ZSTD_compress(compressed.data(), compressed.size(), image.constBits(), sourceBytes, 1);
  if (ZSTD_isError(written) != 0) {
    return {};
  }
  compressed.resize(static_cast<qsizetype>(written));
  return compressed;
}

QImage decompressFrame(const QByteArray &compressed, int width, int height) {
  QImage image(width, height, QImage::Format_ARGB32);
  if (image.isNull()) {
    return {};
  }
  const size_t expected = static_cast<size_t>(image.sizeInBytes());
  const size_t written = ZSTD_decompress(image.bits(), expected, compressed.constData(),
                                         static_cast<size_t>(compressed.size()));
  if (ZSTD_isError(written) != 0 || written != expected) {
    return {};
  }
  return image;
}

class GifFrameStream final : public FrameStream,
                             public std::enable_shared_from_this<GifFrameStream> {
public:
  GifFrameStream(QString filePath, GifInfo info, qint64 storeCapBytes)
      : filePath_(std::move(filePath)), info_(std::move(info)), count_(info_.frameCount),
        storeCapBytes_(storeCapBytes) {
    delaysMs_.reserve(count_);
    for (const int delay : std::as_const(info_.delaysMs)) {
      delaysMs_.push_back(normalizedFrameDelayMs(delay));
    }
    store_.resize(count_);
  }

  // 開くときに 1 度だけ、同期で先頭のフレームを作る。
  bool openFirstFrame(QString *errorMessage) {
    QImage first = readNext();
    if (first.isNull()) {
      if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to decode the first frame");
      }
      return false;
    }
    canvasSize_ = first.size();
    // ヘッダーの大きさとフレームの大きさが食い違うなら、フレームの方が正。
    keep(0, first);
    lastImage_ = first;
    const QMutexLocker locker(&mutex_);
    window_.insert(0, first);
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
      auto self = shared_from_this();
      pool().start([self]() { self->runProducer(); });
    }
  }

  qint64 residentBytes() const override {
    // いま抱えている量。貯めるほど増え、上限 (storeCapBytes_) で止まる。
    const QMutexLocker locker(&mutex_);
    return storedBytes_ +
           static_cast<qint64>(window_.size()) * canvasSize_.width() * canvasSize_.height() * 4;
  }

  void setListener(const void *owner, std::function<void()> listener) override {
    const QMutexLocker locker(&listenerMutex_);
    listeners_.insert(owner, std::move(listener));
  }

  void removeListener(const void *owner) override {
    const QMutexLocker locker(&listenerMutex_);
    listeners_.remove(owner);
  }

  int residentFrameCountForTesting() const override {
    const QMutexLocker locker(&mutex_);
    return static_cast<int>(window_.size());
  }

  int storedFrameCount() const {
    const QMutexLocker locker(&mutex_);
    int stored = 0;
    for (const auto &frame : store_) {
      stored += frame.isEmpty() ? 0 : 1;
    }
    return stored;
  }

private:
  static constexpr int kLookahead = 6;
  static constexpr int kKeepBehind = 2;

  static QThreadPool &pool() {
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

  // 読み手の次の 1 コマを復号する。読み手はここだけが触る。
  QImage readNext() {
    if (reader_ == nullptr) {
      reader_ = std::make_unique<QImageReader>(filePath_);
      reader_->setAutoTransform(false);
      readerPosition_ = 0;
    }
    QImage image = reader_->read();
    if (image.isNull()) {
      return {};
    }
    if (image.format() != QImage::Format_ARGB32) {
      image = image.convertToFormat(QImage::Format_ARGB32);
    }
    ++readerPosition_;
    return image;
  }

  // 圧縮して貯める。上限を超えるなら貯めない。
  void keep(int index, const QImage &image) {
    QByteArray compressed = compressFrame(image);
    if (compressed.isEmpty()) {
      return;
    }
    const QMutexLocker locker(&mutex_);
    if (!store_.at(index).isEmpty() || storedBytes_ + compressed.size() > storeCapBytes_) {
      return;
    }
    storedBytes_ += compressed.size();
    store_[index] = std::move(compressed);
  }

  QByteArray storedFrame(int index) const {
    const QMutexLocker locker(&mutex_);
    return store_.at(index);
  }

  // index のフレームを作る。貯めてあれば伸ばし、無ければ読み手を進めて復号する。
  QImage produce(int index, const QSet<int> &needed) {
    if (const QByteArray stored = storedFrame(index); !stored.isEmpty()) {
      return decompressFrame(stored, canvasSize_.width(), canvasSize_.height());
    }

    // 貯めていない。読み手が先にいる (追い越した) なら、先頭から読み直す。
    if (reader_ == nullptr || readerPosition_ > index) {
      reader_.reset();
      readerPosition_ = 0;
    }

    QImage result;
    while (readerPosition_ <= index) {
      const int position = readerPosition_;
      QImage image = readNext();
      if (image.isNull() || image.size() != canvasSize_) {
        // 走査した数より先で読めなくなった。残りは直前のフレームを繰り返す。
        break;
      }
      keep(position, image);
      lastImage_ = image;
      if (position == index) {
        result = image;
      } else if (needed.contains(position)) {
        // 通りがかりに、窓へ入れておく。
        const QMutexLocker locker(&mutex_);
        window_.insert(position, image);
      }
    }
    if (result.isNull()) {
      // 読めなかった。直前に読めたものを繰り返す (無ければ空)。
      result = lastImage_;
      readerPosition_ = count_;
    }
    return result;
  }

  void runProducer() {
    while (true) {
      int target = 0;
      int missing = -1;
      QSet<int> needed;
      {
        const QMutexLocker locker(&mutex_);
        target = wanted_;
        for (int offset = -kKeepBehind; offset < kLookahead; ++offset) {
          needed.insert(wrap(target + offset));
        }
        for (auto it = window_.begin(); it != window_.end();) {
          it = needed.contains(it.key()) ? std::next(it) : window_.erase(it);
        }
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

      const QImage image = produce(missing, needed);
      if (image.isNull()) {
        // 作れなかった。同じところを回り続けないよう、止める。
        const QMutexLocker locker(&mutex_);
        jobRunning_ = false;
        return;
      }
      {
        const QMutexLocker locker(&mutex_);
        window_.insert(missing, image);
      }
      notifyListeners();
    }
  }

  const QString filePath_;
  const GifInfo info_;
  const int count_;
  const qint64 storeCapBytes_;
  QVector<int> delaysMs_;
  QSize canvasSize_;

  mutable QMutex mutex_;
  // mutex_ で守る。貯めた圧縮フレーム (無ければ空) と、作り終えたフレームの窓。
  QVector<QByteArray> store_;
  qint64 storedBytes_ = 0;
  QHash<int, QImage> window_;
  int wanted_ = 0;
  bool jobRunning_ = false;

  // 作る側のスレッドだけが触る (開くときの先頭だけ、その前に同期で触る)。
  std::unique_ptr<QImageReader> reader_;
  int readerPosition_ = 0;
  QImage lastImage_;

  QMutex listenerMutex_;
  QHash<const void *, std::function<void()>> listeners_;
};

}  // namespace

std::shared_ptr<FrameStream> openGifStream(const QString &filePath, QString *errorMessage,
                                           qint64 storeCapBytes) {
  const GifInfo info = probeGif(filePath);
  if (!info.valid || info.frameCount < 2) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Not an animated GIF");
    }
    return nullptr;
  }

  auto stream = std::make_shared<GifFrameStream>(filePath, info, storeCapBytes);
  if (!stream->openFirstFrame(errorMessage)) {
    return nullptr;
  }
  return stream;
}

int gifStoredFrameCountForTesting(const FrameStream &stream) {
  const auto *gif = dynamic_cast<const GifFrameStream *>(&stream);
  return gif != nullptr ? gif->storedFrameCount() : 0;
}

}  // namespace heiran::infra
