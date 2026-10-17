#pragma once

#include "heiran/ui/AnalysisWindowBase.hpp"
#include "heiran/ui/HistogramSeriesCache.hpp"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QDoubleSpinBox;
QT_END_NAMESPACE

namespace heiran::ui {

class HistogramChartWidget;

class HistogramWindow : public AnalysisWindowBase {
public:
  explicit HistogramWindow(QWidget *parent = nullptr);

protected:
  void analysisImagesChanged() override;

private:
  // 範囲の入力欄を、グラフの範囲と軸の上限へ合わせる。
  void syncRangeControls();

  HistogramSeriesCache cache_;
  // 横軸の上限。範囲 (割合) と入力欄の値を行き来するのに使う。
  double axisMaximum_ = 1.0;
  QCheckBox *fixedMaxCheckBox_ = nullptr;
  QDoubleSpinBox *fixedMaxSpinBox_ = nullptr;
  QDoubleSpinBox *rangeLowSpinBox_ = nullptr;
  QDoubleSpinBox *rangeHighSpinBox_ = nullptr;
  HistogramChartWidget *chart_ = nullptr;
  QDoubleSpinBox *verticalScaleSpinBox_ = nullptr;
  QCheckBox *logarithmicCheckBox_ = nullptr;
  QCheckBox *statisticsCheckBox_ = nullptr;
};

}  // namespace heiran::ui
