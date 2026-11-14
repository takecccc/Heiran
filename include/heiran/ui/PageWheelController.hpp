#pragma once

#include <QtTypes>
#include <optional>

QT_BEGIN_NAMESPACE
class QWheelEvent;
QT_END_NAMESPACE

namespace heiran::ui {

class PageWheelController {
public:
  std::optional<int> consume(const QWheelEvent &event);
  void reset();

private:
  qint64 angleRemainder_ = 0;
};

}  // namespace heiran::ui
