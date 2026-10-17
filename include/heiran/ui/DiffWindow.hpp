#pragma once

#include "heiran/domain/DiffSettings.hpp"
#include "heiran/ui/AnalysisWindowBase.hpp"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
QT_END_NAMESPACE

namespace heiran::ui {

class DiffWindow : public AnalysisWindowBase {
public:
  explicit DiffWindow(QWidget *parent = nullptr);

protected:
  void analysisImagesChanged() override;

private:
  void rebuildCellSelectors();
  void refreshDiffImage();
  void setStatusMessage(const QString &message);
  static heiran::domain::DiffMode diffModeFromIndex(int index);
  static heiran::domain::DiffViewMode diffViewModeFromIndex(int index);

  QComboBox *baseCellCombo_ = nullptr;
  QComboBox *targetCellCombo_ = nullptr;
  QComboBox *modeCombo_ = nullptr;
  QComboBox *viewCombo_ = nullptr;
  QDoubleSpinBox *gainSpinBox_ = nullptr;
  QDoubleSpinBox *thresholdSpinBox_ = nullptr;
  QCheckBox *autoRangeCheckBox_ = nullptr;
  QLabel *previewLabel_ = nullptr;
  QLabel *statsLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;
};

}  // namespace heiran::ui
