#pragma once

#include "heiran/ui/AnalysisWindowBase.hpp"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QSpinBox;
QT_END_NAMESPACE

namespace heiran::ui {

class Histogram3DView;

// 指定した 1 つのセルの、RGB の 3 次元ヒストグラム。全セルは並べず、見るセルを選ぶ。
// ドラッグで回し、ホイールで拡大縮小し、ダブルクリックで向きを戻す。
class Histogram3DWindow : public AnalysisWindowBase {
public:
  explicit Histogram3DWindow(QWidget *parent = nullptr);

protected:
  void analysisImagesChanged() override;

private:
  // 選んだセルと分割数で、ヒストグラムを作り直す。
  void rebuild();

  Histogram3DView *view_ = nullptr;
  QComboBox *cellComboBox_ = nullptr;
  QSpinBox *binsSpinBox_ = nullptr;
  QComboBox *sizeComboBox_ = nullptr;
  QLabel *summaryLabel_ = nullptr;
};

}  // namespace heiran::ui
