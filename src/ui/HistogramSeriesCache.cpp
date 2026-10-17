#include "heiran/ui/HistogramSeriesCache.hpp"

namespace heiran::ui {

HistogramSeriesCache::HistogramSeriesCache(int capacity) : maximums_(capacity), series_(capacity) {}

double HistogramSeriesCache::sampleMaximum(const AnalysisImage &analysisImage) {
  return maximums_.value(analysisImage, std::monostate{},
                         [&analysisImage]() { return histogramSampleMaximum(analysisImage); });
}

HistogramSeries HistogramSeriesCache::series(const AnalysisImage &analysisImage,
                                             double axisMaximum) {
  HistogramSeries series = series_.value(analysisImage, axisMaximum, [&]() {
    return buildHistogramSeries(analysisImage, axisMaximum);
  });
  // ラベルは集計に影響しないため鍵に入れていない。表示に使う分だけ入れ直す。
  series.label = analysisImage.label;
  return series;
}

void HistogramSeriesCache::clear() {
  maximums_.clear();
  series_.clear();
}

int HistogramSeriesCache::entryCount() const {
  return maximums_.entryCount() + series_.entryCount();
}

int HistogramSeriesCache::computeCount() const {
  return maximums_.computeCount() + series_.computeCount();
}

}  // namespace heiran::ui
