#pragma once

#include "heiran/ui/AnalysisWindowBase.hpp"
#include "heiran/ui/HistogramHighlightController.hpp"
#include "heiran/ui/HistogramSeriesCache.hpp"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace heiran::ui {

class HistogramChartWidget;

class HistogramWindow : public AnalysisWindowBase {
  Q_OBJECT

public:
  explicit HistogramWindow(QWidget *parent = nullptr);
  void invalidateHighlight();

Q_SIGNALS:
  void highlightMasksChanged(const QVector<QImage> &masks);

protected:
  void analysisImagesChanged() override;
  void hideEvent(QHideEvent *event) override;

private:
  // 範囲の入力欄を、グラフの範囲と軸の上限へ合わせる。
  void syncRangeControls();
  void refreshHighlight();
  void clearHighlight();

  HistogramSeriesCache cache_;
  HistogramHighlightController highlightController_;
  std::optional<HistogramValueRange> highlightRange_;
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
  QComboBox *highlightChannel_ = nullptr;
  QLabel *highlightStatus_ = nullptr;
  QPushButton *clearHighlightButton_ = nullptr;
};

}  // namespace heiran::ui
