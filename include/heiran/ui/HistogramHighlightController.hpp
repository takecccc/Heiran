#pragma once

#include "heiran/ui/AnalysisResultCache.hpp"

#include <QColor>
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <memory>
#include <optional>

namespace heiran::ui {

struct HistogramValueRange {
  double low;
  double high;

  bool operator==(const HistogramValueRange &) const = default;
};

enum class HistogramHighlightChannel { Any, Red, Green, Blue };

class HistogramHighlightController : public QObject {
  Q_OBJECT

public:
  explicit HistogramHighlightController(QObject *parent = nullptr);
  ~HistogramHighlightController() override;

  void request(const QVector<AnalysisImage> &images, HistogramValueRange range,
               HistogramHighlightChannel channel, QColor color);
  void clear();

Q_SIGNALS:
  void masksChanged(const QVector<QImage> &masks);

private:
  struct RequestKey {
    QVector<AnalysisSourceKey> sources;
    HistogramValueRange range;
    HistogramHighlightChannel channel;
    QColor color;

    bool operator==(const RequestKey &) const = default;
  };

  void startPending();

  std::optional<RequestKey> key_;
  std::optional<QVector<AnalysisImage>> pending_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  QThreadPool pool_;
};

}  // namespace heiran::ui
