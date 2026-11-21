#include "heiran/ui/ComparisonDiffController.hpp"

#include <QCoreApplication>
#include <QPromise>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <utility>

namespace heiran::ui {
namespace {

QThreadPool &comparisonPool() {
  static auto *pool = []() {
    auto *threads = new QThreadPool(QCoreApplication::instance());
    threads->setMaxThreadCount(1);
    return threads;
  }();
  return *pool;
}

bool usesSameField(const ComparisonDiffKey &left, const ComparisonDiffKey &right) {
  const auto &a = left.diff.settings;
  const auto &b = right.diff.settings;
  return left.cell == right.cell && left.base == right.base && left.target == right.target &&
         left.baseSerial == right.baseSerial && left.targetSerial == right.targetSerial &&
         a.mode == b.mode && a.mode != heiran::domain::DiffMode::Wipe &&
         a.sizeMatch == b.sizeMatch && a.comparisonBasis == b.comparisonBasis &&
         a.ignoreFullyTransparentRgb == b.ignoreFullyTransparentRgb;
}

}  // namespace

ComparisonDiffController::ComparisonDiffController(QObject *parent) : QObject(parent) {
  connect(&watcher_, &QFutureWatcherBase::finished, this, [this]() {
    const auto key = std::exchange(active_, std::nullopt);
    std::optional<ComparisonDiffResult> result;
    if (key && !invalidated_ && !watcher_.isCanceled() && watcher_.future().resultCount() > 0) {
      result = watcher_.result();
    }
    if (auto work = std::exchange(pending_, std::nullopt)) {
      if (result && usesSameField(*key, work->key)) {
        work->cached = result->field;
      }
      start(std::move(*work));
    }
    if (result) {
      Q_EMIT completed(*key, *result);
    }
  });
}

ComparisonDiffController::~ComparisonDiffController() {
  invalidate();
}

bool ComparisonDiffController::isRequested(const ComparisonDiffKey &key) const {
  return pending_ ? pending_->key == key : active_ == key && !invalidated_;
}

void ComparisonDiffController::request(const ComparisonDiffKey &key, DiffInput base,
                                       DiffInput target, DiffFieldPtr cached) {
  Work work{key, std::move(base), std::move(target), std::move(cached)};
  if (active_) {
    pending_ = std::move(work);
    if (!usesSameField(*active_, key)) {
      watcher_.cancel();
    }
  } else {
    start(std::move(work));
  }
}

void ComparisonDiffController::invalidate() {
  invalidated_ = true;
  pending_.reset();
  watcher_.cancel();
}

void ComparisonDiffController::start(Work work) {
  invalidated_ = false;
  active_ = work.key;
  watcher_.setFuture(QtConcurrent::run(
      &comparisonPool(), [work = std::move(work)](QPromise<ComparisonDiffResult> &promise) {
        if (promise.isCanceled()) {
          return;
        }
        ComparisonDiffResult result{.field = work.cached};
        const auto &settings = work.key.diff.settings;
        if (settings.mode == heiran::domain::DiffMode::Wipe) {
          result.output = renderDiff(work.base, work.target, settings);
        } else {
          if (!result.field) {
            result.field = computeDiffField(work.base, work.target, settings, &result.output.error,
                                            true, [&]() { return promise.isCanceled(); });
          }
          if (result.field && !promise.isCanceled()) {
            result.output = renderDiffFromField(*result.field, work.base, settings, &result.images);
            if (result.output.ok && result.output.metrics.exceededPixelCount > 0 &&
                !promise.isCanceled()) {
              result.regions =
                  findDiffRegions(*result.field, settings.threshold, settings.highlightBlockSize);
            }
          }
        }
        if (!promise.isCanceled()) {
          promise.addResult(std::move(result));
        }
      }));
}

}  // namespace heiran::ui
