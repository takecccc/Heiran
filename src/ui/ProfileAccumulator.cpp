#include "heiran/ui/ProfileAccumulator.hpp"

#include <QDebug>
#include <algorithm>
#include <limits>

namespace heiran::ui {

ProfileAccumulator::ProfileAccumulator(QString environmentVariable, QString label,
                                       QStringList fieldNames, int reportEvery)
    : environmentVariable_(std::move(environmentVariable)), label_(std::move(label)),
      fieldNames_(std::move(fieldNames)), reportEvery_(std::max(1, reportEvery)),
      totalsNs_(fieldNames_.size(), 0),
      minimumsNs_(fieldNames_.size(), std::numeric_limits<qint64>::max()),
      maximumsNs_(fieldNames_.size(), 0) {}

bool ProfileAccumulator::enabled() const {
  if (!enabled_) {
    enabled_ = qEnvironmentVariableIntValue(environmentVariable_.toUtf8().constData()) != 0;
  }
  return *enabled_;
}

void ProfileAccumulator::record(std::initializer_list<qint64> durationsNs) {
  if (!enabled() || static_cast<int>(durationsNs.size()) != fieldNames_.size()) {
    return;
  }

  int index = 0;
  for (const qint64 durationNs : durationsNs) {
    totalsNs_[index] += durationNs;
    minimumsNs_[index] = std::min(minimumsNs_.at(index), durationNs);
    maximumsNs_[index] = std::max(maximumsNs_.at(index), durationNs);
    ++index;
  }
  ++count_;
  if (count_ < reportEvery_) {
    return;
  }

  QString message = QString("[HeiranProfile] %1").arg(label_);
  for (int fieldIndex = 0; fieldIndex < fieldNames_.size(); ++fieldIndex) {
    const double averageMs =
        static_cast<double>(totalsNs_.at(fieldIndex)) / static_cast<double>(count_) / 1'000'000.0;
    const double minimumMs = static_cast<double>(minimumsNs_.at(fieldIndex)) / 1'000'000.0;
    const double maximumMs = static_cast<double>(maximumsNs_.at(fieldIndex)) / 1'000'000.0;
    message += QString(" avg_%1=%2 ms min_%1=%3 ms max_%1=%4 ms")
                   .arg(fieldNames_.at(fieldIndex))
                   .arg(averageMs, 0, 'f', 3)
                   .arg(minimumMs, 0, 'f', 3)
                   .arg(maximumMs, 0, 'f', 3);
  }
  message += QString(" samples=%1").arg(count_);
  qDebug().noquote() << message;

  totalsNs_.fill(0);
  minimumsNs_.fill(std::numeric_limits<qint64>::max());
  maximumsNs_.fill(0);
  count_ = 0;
}

}  // namespace heiran::ui
