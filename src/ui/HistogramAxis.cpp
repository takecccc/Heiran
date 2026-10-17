#include "heiran/ui/HistogramAxis.hpp"

#include <algorithm>
#include <cmath>

namespace heiran::ui {

HistogramRange clampHistogramRange(HistogramRange range) {
  const double span = std::clamp(range.high - range.low, kMinHistogramSpan, 1.0);
  range.low = std::clamp(range.low, 0.0, 1.0 - span);
  range.high = range.low + span;
  return range;
}

HistogramRange zoomHistogramRange(HistogramRange range, double anchorFraction, double factor) {
  range = clampHistogramRange(range);
  const double oldSpan = range.span();
  const double newSpan = std::clamp(oldSpan * factor, kMinHistogramSpan, 1.0);
  // 位置が範囲のどのあたりにあったか。ここを動かさない。
  const double anchor = std::clamp(anchorFraction, range.low, range.high);
  const double ratio = (anchor - range.low) / oldSpan;
  HistogramRange zoomed{.low = anchor - ratio * newSpan, .high = anchor + (1.0 - ratio) * newSpan};
  return clampHistogramRange(zoomed);
}

double histogramFractionForX(double x, double left, double width, const HistogramRange &range) {
  if (width <= 0.0) {
    return range.low;
  }
  return range.low + (x - left) / width * range.span();
}

double histogramXForFraction(double fraction, double left, double width,
                             const HistogramRange &range) {
  const double span = range.span();
  if (span <= 0.0) {
    return left;
  }
  return left + (fraction - range.low) / span * width;
}

int histogramBinForFraction(double fraction, int binCount) {
  if (binCount <= 1) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::lround(fraction * (binCount - 1))), 0, binCount - 1);
}

double histogramFractionForBin(int bin, int binCount) {
  if (binCount <= 1) {
    return 0.0;
  }
  return static_cast<double>(std::clamp(bin, 0, binCount - 1)) / (binCount - 1);
}

int histogramMaxCountInRange(const QVector<int> &bins, const HistogramRange &range) {
  int maximum = 0;
  for (int index = 0; index < bins.size(); ++index) {
    const double fraction = histogramFractionForBin(index, static_cast<int>(bins.size()));
    // 範囲の端のビンは、丸めの誤差で外れないよう、わずかに広げて数える。
    if (fraction >= range.low - 1e-9 && fraction <= range.high + 1e-9) {
      maximum = std::max(maximum, bins.at(index));
    }
  }
  return maximum;
}

double histogramFullScaleRatio(double autoRatio, double fixedRatio) {
  return fixedRatio > 0.0 ? fixedRatio : autoRatio;
}

}  // namespace heiran::ui
