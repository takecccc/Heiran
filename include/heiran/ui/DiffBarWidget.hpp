#pragma once

#include "heiran/domain/DiffMetrics.hpp"
#include "heiran/domain/SessionModel.hpp"

#include <QStringList>
#include <QWidget>
#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QFrame;
class QLabel;
class QToolButton;
QT_END_NAMESPACE

namespace heiran::ui {

// 差分を出しているセルの上に置く、小さな帯。帯には、求め方と、差の数値指標だけを出す
// (狭いセルでは、指標を省略記号で切る)。
// 基準と比べる側のセル、見せ方、倍率、閾値、大きさの合わせ方は、歯車のボタンで開くポップアップに置く。
// 変えると `diffChanged` で新しい指定を返す。
class DiffBarWidget : public QWidget {
  Q_OBJECT

public:
  explicit DiffBarWidget(QWidget *parent = nullptr);

  // 表示する指定。`cellLabels` は、選べるセルの名前 (番号の順)。`selfCell` は、このセル自身の番号。
  // `ownsImage` が真なら、このセル自身の画像が比べる側 (比べる側は選ばせない)。
  void setDiff(const heiran::domain::CellDiff &diff, const QStringList &cellLabels, int selfCell,
               bool ownsImage);
  // 求めた指標か、作れなかった理由。
  void setResult(const std::optional<heiran::domain::DiffMetrics> &metrics, const QString &message);
  void setRegionProgress(qsizetype index, qsizetype count);
  void setBlinking(bool enabled);

  heiran::domain::CellDiff currentDiff() const;
  // 指標の全文 (帯が狭くて切れていても、全文を返す)。
  QString resultText() const;
  // 入力の途中 (フォーカスがある、一覧を開いている) か。この間は隠さない。
  bool isInteracting() const;

Q_SIGNALS:
  void diffChanged(const heiran::domain::CellDiff &diff);
  void closeRequested();
  void regionStepRequested(int direction);
  void blinkRequested(bool enabled);

private:
  void emitChange();
  void showSettings();
  // ワイプのときは、見せ方 (View)、倍率、閾値、自動の範囲を隠し、切り方と位置を出す。
  void updateRowVisibility();

  QComboBox *baseCombo_ = nullptr;
  QComboBox *targetCombo_ = nullptr;
  QComboBox *modeCombo_ = nullptr;
  QComboBox *viewCombo_ = nullptr;
  QComboBox *sizeMatchCombo_ = nullptr;
  QDoubleSpinBox *gainSpinBox_ = nullptr;
  QDoubleSpinBox *thresholdSpinBox_ = nullptr;
  QComboBox *wipeDirectionCombo_ = nullptr;
  QDoubleSpinBox *wipePositionSpinBox_ = nullptr;
  QCheckBox *autoRangeCheckBox_ = nullptr;
  QLabel *resultLabel_ = nullptr;
  QLabel *regionLabel_ = nullptr;
  QToolButton *previousRegion_ = nullptr;
  QToolButton *nextRegion_ = nullptr;
  QToolButton *blinkButton_ = nullptr;
  QToolButton *closeButton_ = nullptr;
  QToolButton *settingsButton_ = nullptr;
  QFrame *settingsPopup_ = nullptr;
  QFormLayout *settingsForm_ = nullptr;
  bool ownsImage_ = false;
  bool updating_ = false;
  // 閾値の尺度が、モードで違う。直前のモードを覚えて、閾値が、そのモードの既定のままなら、新しいモードの既定へ替える。
  QString previousMode_ = QStringLiteral("absolute");
};

}  // namespace heiran::ui
