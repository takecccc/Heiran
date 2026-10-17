#pragma once

#include "heiran/ui/AnalysisResultCache.hpp"
#include "heiran/ui/AnalysisWindowBase.hpp"
#include "heiran/ui/ScopeBuilder.hpp"

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QScrollArea;
QT_END_NAMESPACE

namespace heiran::ui {

// 1 つのセルの、波形、またはベクトルスコープを描く部分。絵 (升目から起こした画像) と、目盛りを重ねる。
class ScopePlotWidget : public QWidget {
public:
  enum class Kind { Waveform, Vectorscope };

  ScopePlotWidget(Kind kind, QWidget *parent = nullptr);

  void setTitle(const QString &title);
  void setImage(const QImage &image);
  void setGraticuleVisible(bool visible);
  // ベクトルスコープの目盛り (色の目標) を置くための、輝度の重み。
  void setLumaWeights(const heiran::domain::LumaWeights &weights);
  // 波形の絵が、横に何枚並んでいるか (Parade は 3)。目盛りの縦の線を、その数だけ引く。
  void setWaveformPanels(int panels);

  const QImage &image() const { return image_; }
  QString title() const { return title_; }

protected:
  void paintEvent(QPaintEvent *event) override;
  QSize sizeHint() const override;

private:
  QRect plotRect() const;
  void paintWaveformGraticule(QPainter *painter, const QRect &plot) const;
  void paintVectorscopeGraticule(QPainter *painter, const QRect &plot) const;

  Kind kind_;
  QString title_;
  QImage image_;
  bool graticule_ = true;
  int panels_ = 1;
  heiran::domain::LumaWeights weights_{.red = 0.2126, .green = 0.7152, .blue = 0.0722};
};

// 波形モニタ: 横が画像の位置、縦が値。セルごとに 1 つ。表示は、輝度、RGB の重ね、RGB を並べた (Parade) から選ぶ。
class WaveformWindow : public AnalysisWindowBase {
public:
  explicit WaveformWindow(QWidget *parent = nullptr);

  // 試験のために出している。
  int plotCount() const { return plots_.size(); }
  const ScopePlotWidget *plotAt(int index) const { return plots_.at(index); }
  int cacheComputeCount() const { return cache_.computeCount(); }

protected:
  void analysisImagesChanged() override;

private:
  void renderPlots();

  AnalysisResultCache<WaveformPtr, int> cache_;
  QComboBox *modeCombo_ = nullptr;
  QDoubleSpinBox *gainSpinBox_ = nullptr;
  QCheckBox *graticuleCheckBox_ = nullptr;
  QWidget *plotContainer_ = nullptr;
  QGridLayout *plotLayout_ = nullptr;
  QVector<ScopePlotWidget *> plots_;
  QVector<WaveformPtr> data_;
};

// ベクトルスコープ: 色差 (Cb が横、Cr が縦) の面での、色の分布。セルごとに 1 つ。
class VectorscopeWindow : public AnalysisWindowBase {
public:
  explicit VectorscopeWindow(QWidget *parent = nullptr);

  int plotCount() const { return plots_.size(); }
  const ScopePlotWidget *plotAt(int index) const { return plots_.at(index); }
  int cacheComputeCount() const { return cache_.computeCount(); }

protected:
  void analysisImagesChanged() override;

private:
  void renderPlots();

  AnalysisResultCache<VectorscopePtr, int> cache_;
  QDoubleSpinBox *gainSpinBox_ = nullptr;
  QCheckBox *graticuleCheckBox_ = nullptr;
  QWidget *plotContainer_ = nullptr;
  QGridLayout *plotLayout_ = nullptr;
  QVector<ScopePlotWidget *> plots_;
  QVector<VectorscopePtr> data_;
  QVector<heiran::domain::LumaWeights> weights_;
};

}  // namespace heiran::ui
