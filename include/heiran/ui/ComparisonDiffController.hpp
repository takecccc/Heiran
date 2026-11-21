#pragma once

#include "heiran/domain/SessionModel.hpp"
#include "heiran/ui/DiffRenderer.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <optional>

namespace heiran::ui {

struct ComparisonDiffKey {
  int cell = 0;
  int base = 0;
  int target = 0;
  quint64 baseSerial = 0;
  quint64 targetSerial = 0;
  heiran::domain::CellDiff diff;

  bool operator==(const ComparisonDiffKey &) const = default;
};

struct ComparisonDiffResult {
  DiffFieldPtr field;
  DiffOutput output;
  QVector<QRect> regions;
};

class ComparisonDiffController final : public QObject {
  Q_OBJECT

public:
  explicit ComparisonDiffController(QObject *parent = nullptr);
  ~ComparisonDiffController() override;
  bool isRequested(const ComparisonDiffKey &key) const;
  void request(const ComparisonDiffKey &key, DiffInput base, DiffInput target, DiffFieldPtr cached);
  void invalidate();

Q_SIGNALS:
  void completed(const ComparisonDiffKey &key, const ComparisonDiffResult &result);

private:
  struct Work {
    ComparisonDiffKey key;
    DiffInput base;
    DiffInput target;
    DiffFieldPtr cached;
  };

  void start(Work work);

  QFutureWatcher<ComparisonDiffResult> watcher_;
  std::optional<ComparisonDiffKey> active_;
  std::optional<Work> pending_;
  bool invalidated_ = false;
};

}  // namespace heiran::ui
