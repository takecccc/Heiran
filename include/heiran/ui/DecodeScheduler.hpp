#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace heiran::ui {

struct DecodeCompletion {
  QString filePath;
  QString cacheKey;
  bool honorExifOrientation = false;
  // 読んだページ (TIFF など)。ページを持たない形式では 0。
  int subImageIndex = 0;
  bool prefetchOnly = true;
  heiran::infra::DecodeResult result;
  qint64 decodeDurationNs = 0;
};

// 完了通知は signal/slot で行う。受け側を context object にして接続すれば、
// 受け側の破棄時に Qt が接続を切るため、ワーカースレッドから破棄済みの
// オブジェクトを触る経路が残らない。
class DecodeScheduler : public QObject {
  Q_OBJECT

public:
  explicit DecodeScheduler(QObject *parent = nullptr);
  ~DecodeScheduler() override;

  void reset();
  void request(const QString &filePath, bool honorExifOrientation, bool prefetchRequest,
               int subImageIndex = 0);

Q_SIGNALS:
  void decodeCompleted(const heiran::ui::DecodeCompletion &completion);

private:
  struct DecodeJob {
    QString filePath;
    QString cacheKey;
    bool honorExifOrientation = false;
    int subImageIndex = 0;
    bool prefetchOnly = true;
    quint64 generation = 0;
  };

  struct PendingDecodeState {
    bool queued = false;
    bool running = false;
    bool prefetchOnly = true;
    quint64 generation = 0;
  };

  void startWorkers();
  void stopWorkers();
  void workerLoop();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<DecodeJob> queue_;
  QHash<QString, PendingDecodeState> pending_;
  std::vector<std::thread> workers_;
  bool stop_ = false;
  quint64 generation_ = 0;
};

}  // namespace heiran::ui

Q_DECLARE_METATYPE(heiran::ui::DecodeCompletion)
