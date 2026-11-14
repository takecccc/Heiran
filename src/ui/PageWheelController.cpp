#include "heiran/ui/PageWheelController.hpp"

#include <QWheelEvent>

namespace heiran::ui {

std::optional<int> PageWheelController::consume(const QWheelEvent &event) {
  if (event.modifiers() != Qt::AltModifier) {
    reset();
    return std::nullopt;
  }
  if (event.phase() == Qt::ScrollBegin) {
    reset();
  }
  const int delta = event.angleDelta().y();
  if ((delta > 0 && angleRemainder_ < 0) || (delta < 0 && angleRemainder_ > 0)) {
    reset();
  }
  angleRemainder_ += delta;
  const int steps = static_cast<int>(angleRemainder_ / 120);
  angleRemainder_ %= 120;
  if (event.phase() == Qt::ScrollEnd) {
    reset();
  }
  return -steps;
}

void PageWheelController::reset() {
  angleRemainder_ = 0;
}

}  // namespace heiran::ui
