#include "heiran/ui/HistogramHighlightController.hpp"

#include "heiran/infra/ParallelRows.hpp"
#include "heiran/ui/HistogramSeriesBuilder.hpp"

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <utility>

namespace heiran::ui {

namespace {

QImage buildMask(const AnalysisImage &image, HistogramValueRange range,
                 HistogramHighlightChannel channel, QColor color,
                 const std::atomic_bool &cancelled) {
  const QRect region = histogramAnalysisRect(image);
  if (region.isEmpty() || cancelled.load(std::memory_order_relaxed)) {
    return {};
  }

  QImage mask(region.size(), QImage::Format_MonoLSB);
  if (mask.isNull()) {
    return {};
  }
  mask.setColorTable({qRgba(0, 0, 0, 0), color.rgba()});
  mask.setOffset(region.topLeft());
  mask.fill(0);
  auto *bits = mask.bits();
  const qsizetype stride = mask.bytesPerLine();
  const bool grayscale = image.sampler.isGrayscale(image.image);
  const int first =
      grayscale || channel == HistogramHighlightChannel::Any ? 0 : static_cast<int>(channel) - 1;
  const int end = !grayscale && channel == HistogramHighlightChannel::Any ? 3 : first + 1;
  for (int index = first; index < end; ++index) {
    if (cancelled.load(std::memory_order_relaxed)) {
      return {};
    }
    const auto plane = image.sampler.buildChannelPlane(region, index);
    if (plane.size() != qsizetype(region.width()) * region.height()) {
      return {};
    }
    heiran::infra::forEachRowBand(region.height(), [&](int firstRow, int lastRow) {
      for (int y = firstRow; y < lastRow; ++y) {
        if (cancelled.load(std::memory_order_relaxed)) {
          return;
        }
        auto *row = bits + y * stride;
        const auto *samples = plane.constData() + qsizetype(y) * region.width();
        for (int x = 0; x < region.width(); ++x) {
          const double sample = samples[x];
          if (sample >= range.low && sample <= range.high) {
            row[x / 8] |= static_cast<uchar>(1U << (x % 8));
          }
        }
      }
    });
  }
  return mask;
}

}  // namespace

HistogramHighlightController::HistogramHighlightController(QObject *parent) : QObject(parent) {
  pool_.setMaxThreadCount(1);
}

HistogramHighlightController::~HistogramHighlightController() {
  if (cancelled_) {
    cancelled_->store(true, std::memory_order_relaxed);
  }
}

void HistogramHighlightController::request(const QVector<AnalysisImage> &images,
                                           HistogramValueRange range,
                                           HistogramHighlightChannel channel, QColor color) {
  QVector<AnalysisSourceKey> sources;
  sources.reserve(images.size());
  for (const auto &image : images) {
    sources.push_back(makeAnalysisSourceKey(image));
  }
  RequestKey key{std::move(sources), range, channel, color};
  if (key_ == key) {
    return;
  }
  clear();
  key_ = std::move(key);
  pending_ = images;
  startPending();
}

void HistogramHighlightController::clear() {
  if (cancelled_) {
    cancelled_->store(true, std::memory_order_relaxed);
  }
  pending_.reset();
  if (key_) {
    key_.reset();
    Q_EMIT masksChanged({});
  }
}

void HistogramHighlightController::startPending() {
  if (cancelled_ || !pending_) {
    return;
  }
  auto images = std::move(*pending_);
  pending_.reset();
  cancelled_ = std::make_shared<std::atomic_bool>(false);
  auto *watcher = new QFutureWatcher<QVector<QImage>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
    const bool current = !cancelled_->load(std::memory_order_relaxed);
    cancelled_.reset();
    if (current) {
      Q_EMIT masksChanged(watcher->result());
    }
    watcher->deleteLater();
    startPending();
  });
  watcher->setFuture(QtConcurrent::run(&pool_, [images = std::move(images), range = key_->range,
                                                channel = key_->channel, color = key_->color,
                                                cancelled = cancelled_]() {
    QVector<QImage> masks;
    masks.reserve(images.size());
    for (const auto &image : images) {
      if (cancelled->load(std::memory_order_relaxed)) {
        return QVector<QImage>{};
      }
      masks.push_back(buildMask(image, range, channel, color, *cancelled));
    }
    return masks;
  }));
}

}  // namespace heiran::ui
