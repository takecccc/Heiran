#pragma once

#include <QBrush>
#include <QColor>
#include <QPoint>

namespace heiran::ui {

enum class TransparencyPattern { Solid, Checkerboard };

struct TransparencyBackground {
  TransparencyPattern pattern = TransparencyPattern::Solid;
  QColor solidColor{245, 245, 245};
  QColor checkerColor1{192, 192, 192};
  QColor checkerColor2{245, 245, 245};

  bool operator==(const TransparencyBackground &) const = default;
  QColor colorAt(const QPoint &position) const;
};

inline constexpr int kTransparencyCheckerSize = 8;

QBrush transparencyBackgroundBrush(const TransparencyBackground &background);

}  // namespace heiran::ui
