#include "heiran/ui/DecodeScheduler.hpp"

#include "heiran/ui/DecodeCache.hpp"

#include <QElapsedTimer>
#include <QThread>
#include <algorithm>

namespace heiran::ui {

DecodeScheduler::DecodeScheduler(QObject *parent, bool bufferCompletions)
    : QObject(parent), bufferCompletions_(bufferCompletions) {
  qRegisterMetaType<heiran::ui::DecodeCompletion>();
  startWorkers();
}

DecodeScheduler::~DecodeScheduler() {
  stopWorkers();
}

void DecodeScheduler::reset() {
  std::lock_guard lock(mutex_);
  queue_.clear();
  pending_.clear();
  completed_.clear();
  ++generation_;
}

void DecodeScheduler::request(const QString &filePath, bool honorExifOrientation,
                              bool prefetchRequest, int subImageIndex) {
  if (filePath.isEmpty()) {
    return;
  }

  const QString cacheKey = DecodeCache::makeKey(filePath, honorExifOrientation, subImageIndex);
  bool notifyWorker = false;
  {
    std::lock_guard lock(mutex_);
    if (completed_.contains(cacheKey)) {
      return;
    }
    auto pendingIt = pending_.find(cacheKey);
    if (pendingIt != pending_.end()) {
      if (!prefetchRequest && pendingIt->prefetchOnly) {
        pendingIt->prefetchOnly = false;
        for (auto queueIt = queue_.begin(); queueIt != queue_.end(); ++queueIt) {
          if (queueIt->cacheKey == cacheKey) {
            DecodeJob promotedJob = *queueIt;
            promotedJob.prefetchOnly = false;
            queue_.erase(queueIt);
            queue_.push_front(std::move(promotedJob));
            break;
          }
        }
      }
      return;
    }

    pending_.insert(cacheKey, PendingDecodeState{.queued = true,
                                                 .running = false,
                                                 .prefetchOnly = prefetchRequest,
                                                 .generation = generation_});
    DecodeJob job{.filePath = filePath,
                  .cacheKey = cacheKey,
                  .honorExifOrientation = honorExifOrientation,
                  .subImageIndex = subImageIndex,
                  .prefetchOnly = prefetchRequest,
                  .generation = generation_};
    if (prefetchRequest) {
      queue_.push_back(std::move(job));
    } else {
      queue_.push_front(std::move(job));
    }
    notifyWorker = true;
  }

  if (notifyWorker) {
    cv_.notify_one();
  }
}

QVector<DecodeCompletion> DecodeScheduler::activate() {
  std::lock_guard lock(mutex_);
  bufferCompletions_ = false;
  const auto completed = completed_.values();
  completed_.clear();
  return completed;
}

std::optional<DecodeCompletion> DecodeScheduler::takeCompleted(const QString &cacheKey) {
  std::lock_guard lock(mutex_);
  const auto found = completed_.find(cacheKey);
  if (found == completed_.end()) {
    return std::nullopt;
  }
  auto completion = found.value();
  completed_.erase(found);
  return completion;
}

void DecodeScheduler::startWorkers() {
  const int workerCount = std::max(1, QThread::idealThreadCount());
  workers_.reserve(workerCount);
  for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
    workers_.push_back(std::thread([this]() { workerLoop(); }));
  }
}

void DecodeScheduler::stopWorkers() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    queue_.clear();
    pending_.clear();
  }
  cv_.notify_all();
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

void DecodeScheduler::workerLoop() {
  while (true) {
    DecodeJob job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_) {
        return;
      }

      auto selectedIt = queue_.end();
      for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (!it->prefetchOnly) {
          selectedIt = it;
          break;
        }
      }
      if (selectedIt == queue_.end()) {
        selectedIt = queue_.begin();
      }

      job = *selectedIt;
      queue_.erase(selectedIt);

      auto pendingIt = pending_.find(job.cacheKey);
      if (pendingIt == pending_.end() || pendingIt->generation != job.generation) {
        continue;
      }
      pendingIt->queued = false;
      pendingIt->running = true;
    }

    QElapsedTimer timer;
    timer.start();
    heiran::infra::ImageDecoder decoder;
    DecodeCompletion completion{
        .filePath = job.filePath,
        .cacheKey = job.cacheKey,
        .honorExifOrientation = job.honorExifOrientation,
        .subImageIndex = job.subImageIndex,
        .prefetchOnly = job.prefetchOnly,
        .result = decoder.decode(job.filePath, job.honorExifOrientation, job.subImageIndex),
        .decodeDurationNs = timer.nsecsElapsed()};

    bool stale = false;
    bool buffered = false;
    {
      std::lock_guard lock(mutex_);
      auto pendingIt = pending_.find(job.cacheKey);
      if (pendingIt != pending_.end() && pendingIt->generation == job.generation) {
        pending_.erase(pendingIt);
      } else {
        stale = true;
      }

      if (job.generation != generation_) {
        stale = true;
      }
      if (!stale) {
        completed_.insert(job.cacheKey, completion);
        buffered = bufferCompletions_;
      }
    }

    if (buffered) {
      Q_EMIT completionBuffered();
    } else if (!stale) {
      // 受け側が別スレッドにいるため queued connection になる。
      Q_EMIT decodeCompleted(completion);
    }
  }
}

}  // namespace heiran::ui
