#pragma once

#include "heiran/ui/AnalysisResultCache.hpp"
#include "heiran/ui/AnalysisWindowBase.hpp"
#include "heiran/ui/ChangeGraphSeriesBuilder.hpp"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
QT_END_NAMESPACE

namespace heiran::ui {

class ChangeGraphChartWidget;

// 変化グラフ。縦に平均して横方向の変化、または横に平均して縦方向の変化を描く。
class ChangeGraphWindow : public AnalysisWindowBase {
public:
  explicit ChangeGraphWindow(QWidget *parent = nullptr);

protected:
  void analysisImagesChanged() override;

private:
  ChangeGraphDirection currentDirection() const;

  AnalysisResultCache<ChangeGraphSeries, ChangeGraphDirection> cache_;
  ChangeGraphChartWidget *chart_ = nullptr;
  QComboBox *directionCombo_ = nullptr;
  QComboBox *channelCombo_ = nullptr;
  QCheckBox *overlayCellsCheckBox_ = nullptr;
};

}  // namespace heiran::ui
