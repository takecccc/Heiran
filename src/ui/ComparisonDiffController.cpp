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

}  // namespace

ComparisonDiffController::ComparisonDiffController(QObject *parent) : QObject(parent) {
  connect(&watcher_, &QFutureWatcherBase::finished, this, [this]() {
    const auto key = std::exchange(active_, std::nullopt);
    if (auto work = std::exchange(pending_, std::nullopt)) {
      start(std::move(*work));
    } else if (key && !invalidated_ && !watcher_.isCanceled() &&
               watcher_.future().resultCount() > 0) {
      const auto result = watcher_.result();
      Q_EMIT completed(*key, result);
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
    watcher_.cancel();
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
            result.output = renderDiffFromField(*result.field, work.base, settings);
            if (result.output.ok && result.output.metrics.exceededPixelCount > 0 &&
                !promise.isCanceled()) {
              result.regions = findDiffRegions(*result.field, settings.threshold);
            }
          }
        }
        if (!promise.isCanceled()) {
          promise.addResult(std::move(result));
        }
      }));
}

}  // namespace heiran::ui
