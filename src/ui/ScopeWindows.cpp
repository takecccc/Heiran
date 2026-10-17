#include "heiran/ui/ScopeWindows.hpp"

#include "heiran/domain/Scopes.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

// 計測用の面は、配色に関わらず、暗い中立な下地にする
// (画像を見比べる面と同じ。明るい下地では、光の点が見えにくい)。
const QColor kScopeBackground(14, 15, 18);
const QColor kScopeFrame(70, 74, 82);
const QColor kScopeGrid(255, 255, 255, 38);
const QColor kScopeText(150, 155, 164);
const QColor kScopeTitle(214, 217, 223);

constexpr int kLeftMargin = 40;
constexpr int kTopMargin = 26;
constexpr int kRightMargin = 10;
constexpr int kBottomMargin = 10;

}  // namespace

ScopePlotWidget::ScopePlotWidget(Kind kind, QWidget *parent) : QWidget(parent), kind_(kind) {
  setObjectName("scopePlot");
  setMinimumSize(kind == Kind::Waveform ? QSize(320, 200) : QSize(240, 240));
}

void ScopePlotWidget::setTitle(const QString &title) {
  title_ = title;
  update();
}

void ScopePlotWidget::setImage(const QImage &image) {
  image_ = image;
  update();
}

void ScopePlotWidget::setGraticuleVisible(bool visible) {
  graticule_ = visible;
  update();
}

void ScopePlotWidget::setLumaWeights(const heiran::domain::LumaWeights &weights) {
  weights_ = weights;
  update();
}

void ScopePlotWidget::setWaveformPanels(int panels) {
  panels_ = std::max(1, panels);
  update();
}

QSize ScopePlotWidget::sizeHint() const {
  return kind_ == Kind::Waveform ? QSize(560, 300) : QSize(320, 320);
}

QRect ScopePlotWidget::plotRect() const {
  const QRect area = rect().adjusted(kLeftMargin, kTopMargin, -kRightMargin, -kBottomMargin);
  if (kind_ == Kind::Vectorscope) {
    // 色差の面は、正方形。
    const int side = std::min(area.width(), area.height());
    return QRect(area.left() + (area.width() - side) / 2, area.top() + (area.height() - side) / 2,
                 side, side);
  }
  return area;
}

void ScopePlotWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), kScopeBackground);
  painter.setPen(kScopeTitle);
  painter.drawText(QRect(8, 4, width() - 16, kTopMargin - 4), Qt::AlignLeft | Qt::AlignVCenter,
                   title_);

  const QRect plot = plotRect();
  if (plot.width() < 8 || plot.height() < 8) {
    return;
  }
  if (image_.isNull()) {
    painter.setPen(kScopeText);
    painter.drawText(plot, Qt::AlignCenter,
                     QCoreApplication::translate("ScopePlotWidget", "No analyzable image"));
    return;
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform, kind_ == Kind::Waveform);
  painter.drawImage(plot, image_);
  if (graticule_) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (kind_ == Kind::Waveform) {
      paintWaveformGraticule(&painter, plot);
    } else {
      paintVectorscopeGraticule(&painter, plot);
    }
  }
  painter.setPen(kScopeFrame);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(plot.adjusted(0, 0, -1, -1));
}

void ScopePlotWidget::paintWaveformGraticule(QPainter *painter, const QRect &plot) const {
  // 値の目盛り: 0、0.25、0.5、0.75、1.0。
  const QFontMetrics metrics = painter->fontMetrics();
  for (int step = 0; step <= 4; ++step) {
    const double value = step / 4.0;
    const int y = plot.bottom() - static_cast<int>(std::lround(value * (plot.height() - 1)));
    painter->setPen(QPen(kScopeGrid, 1));
    painter->drawLine(plot.left(), y, plot.right(), y);
    painter->setPen(kScopeText);
    const QString label = value == 0.0 || value == 1.0 ? QString::number(value, 'f', 1)
                                                       : QString::number(value, 'f', 2);
    painter->drawText(QRect(0, y - metrics.height() / 2, kLeftMargin - 4, metrics.height()),
                      Qt::AlignRight | Qt::AlignVCenter, label);
  }
  // 並べた絵の境目 (Parade)。
  for (int panel = 1; panel < panels_; ++panel) {
    const int x = plot.left() + plot.width() * panel / panels_;
    painter->setPen(QPen(kScopeFrame, 1));
    painter->drawLine(x, plot.top(), x, plot.bottom());
  }
}

void ScopePlotWidget::paintVectorscopeGraticule(QPainter *painter, const QRect &plot) const {
  const QPointF center = QRectF(plot).center();
  const double radius = plot.width() / 2.0;  // 色差の範囲 (-0.5 から 0.5) が、面の端まで
  painter->setPen(QPen(kScopeGrid, 1));
  // 軸と、輪 (色差の大きさ 0.25 と 0.5)。
  painter->drawLine(QPointF(plot.left(), center.y()), QPointF(plot.right(), center.y()));
  painter->drawLine(QPointF(center.x(), plot.top()), QPointF(center.x(), plot.bottom()));
  painter->drawEllipse(center, radius * 0.5, radius * 0.5);
  painter->drawEllipse(center, radius, radius);

  // 肌の色の線 (I 軸。+Cb の軸から、反時計回りに約 123 度)。
  constexpr double kSkinAngle = 123.0 * 3.14159265358979 / 180.0;
  painter->setPen(QPen(QColor(230, 190, 150, 90), 1, Qt::DashLine));
  painter->drawLine(center, QPointF(center.x() + radius * std::cos(kSkinAngle),
                                    center.y() - radius * std::sin(kSkinAngle)));

  // 色の目標 (75% の R、Y、G、C、B、M)。
  struct Target {
    const char *label;
    double red, green, blue;
    QColor color;
  };
  const Target targets[] = {
      {"R", 0.75, 0.0, 0.0, QColor(240, 90, 80)},   {"Y", 0.75, 0.75, 0.0, QColor(235, 215, 80)},
      {"G", 0.0, 0.75, 0.0, QColor(90, 210, 110)},  {"C", 0.0, 0.75, 0.75, QColor(90, 210, 210)},
      {"B", 0.0, 0.0, 0.75, QColor(110, 150, 250)}, {"M", 0.75, 0.0, 0.75, QColor(220, 110, 220)}};
  QFont small = painter->font();
  small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
  painter->setFont(small);
  for (const auto &target : targets) {
    const auto chroma = heiran::domain::chromaOf(target.red, target.green, target.blue,
                                                 weights_.red, weights_.green, weights_.blue);
    const QPointF point(center.x() + chroma.cb * 2.0 * radius,
                        center.y() - chroma.cr * 2.0 * radius);
    painter->setPen(QPen(target.color, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(point.x() - 5, point.y() - 5, 10, 10));
    painter->drawText(QPointF(point.x() + 7, point.y() - 6), target.label);
  }
}

namespace {

// 窓の共通の骨格。上に操作、下に、セルごとの絵を並べる面。
QWidget *buildScopeLayout(QWidget *window, QHBoxLayout *controls, QWidget **containerOut,
                          QGridLayout **gridOut) {
  auto *root = new QVBoxLayout(window);
  root->addLayout(controls);
  auto *scroll = new QScrollArea(window);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *container = new QWidget(scroll);
  auto *grid = new QGridLayout(container);
  grid->setSpacing(8);
  grid->setContentsMargins(0, 0, 0, 0);
  scroll->setWidget(container);
  root->addWidget(scroll, 1);
  *containerOut = container;
  *gridOut = grid;
  return scroll;
}

// セルごとの絵を、必要な数へそろえて、格子へ並べる。
void ensurePlots(QVector<ScopePlotWidget *> *plots, QWidget *container, QGridLayout *grid,
                 ScopePlotWidget::Kind kind, int count, int columnsPerRow) {
  while (plots->size() > count) {
    delete plots->takeLast();
  }
  while (plots->size() < count) {
    auto *plot = new ScopePlotWidget(kind, container);
    plots->push_back(plot);
  }
  for (int index = 0; index < plots->size(); ++index) {
    grid->addWidget(plots->at(index), index / columnsPerRow, index % columnsPerRow);
    plots->at(index)->show();
  }
}

QString plotTitle(const AnalysisImage &analysisImage, const QString &detail) {
  const bool selection = analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty();
  QString title = analysisImage.label;
  if (selection) {
    title += QCoreApplication::translate("ScopeWindows", "  (selection %1 x %2)")
                 .arg(analysisImage.selectionRect->width())
                 .arg(analysisImage.selectionRect->height());
  }
  if (!detail.isEmpty()) {
    title += "  " + detail;
  }
  return title;
}

}  // namespace

// ---- 波形モニタ ----

WaveformWindow::WaveformWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("ScopeWindows", "Waveform"), parent) {
  resize(900, 560);
  auto *controls = new QHBoxLayout;
  controls->addWidget(new QLabel(QCoreApplication::translate("WaveformWindow", "Mode"), this));
  modeCombo_ = new QComboBox(this);
  modeCombo_->addItems({QCoreApplication::translate("WaveformWindow", "Luma"), "RGB",
                        QCoreApplication::translate("WaveformWindow", "Parade")});
  modeCombo_->setToolTip(QCoreApplication::translate(
      "WaveformWindow",
      "Luma: the brightness. RGB: red, green and blue drawn over each other. Parade: red, green "
      "and blue side by side."));
  controls->addWidget(modeCombo_);
  controls->addWidget(
      new QLabel(QCoreApplication::translate("WaveformWindow", "Brightness"), this));
  gainSpinBox_ = new QDoubleSpinBox(this);
  gainSpinBox_->setRange(0.1, 20.0);
  gainSpinBox_->setValue(1.0);
  gainSpinBox_->setSingleStep(0.25);
  gainSpinBox_->setPrefix("×");
  gainSpinBox_->setToolTip(QCoreApplication::translate(
      "WaveformWindow", "How bright the traces are drawn (a faint trace becomes visible)"));
  controls->addWidget(gainSpinBox_);
  graticuleCheckBox_ =
      new QCheckBox(QCoreApplication::translate("WaveformWindow", "Graticule"), this);
  graticuleCheckBox_->setChecked(true);
  controls->addWidget(graticuleCheckBox_);
  controls->addStretch(1);
  buildScopeLayout(this, controls, &plotContainer_, &plotLayout_);

  connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this]() { renderPlots(); });
  connect(gainSpinBox_, &QDoubleSpinBox::valueChanged, this, [this]() { renderPlots(); });
  connect(graticuleCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
    for (auto *plot : std::as_const(plots_)) {
      plot->setGraticuleVisible(checked);
    }
  });
}

void WaveformWindow::analysisImagesChanged() {
  const auto &images = analysisImages();
  data_.clear();
  QVector<const AnalysisImage *> used;
  for (const auto &analysisImage : images) {
    if (analysisImage.image.isNull()) {
      continue;
    }
    // 結果は、画像と選択範囲の同一性で、取っておく (ページを行き来しても、数え直さない)。
    WaveformPtr counts =
        cache_.value(analysisImage, 0, [&]() { return buildWaveform(analysisImage); });
    data_.push_back(counts);
    used.push_back(&analysisImage);
  }
  ensurePlots(&plots_, plotContainer_, plotLayout_, ScopePlotWidget::Kind::Waveform,
              static_cast<int>(used.size()), 1);
  for (int index = 0; index < used.size(); ++index) {
    const auto &analysisImage = *used.at(index);
    plots_.at(index)->setTitle(
        plotTitle(analysisImage, QCoreApplication::translate("WaveformWindow", "%1 columns")
                                     .arg(data_.at(index) ? data_.at(index)->columns : 0)));
    plots_.at(index)->setGraticuleVisible(graticuleCheckBox_->isChecked());
  }
  renderPlots();
}

void WaveformWindow::renderPlots() {
  const auto mode = static_cast<WaveformMode>(modeCombo_->currentIndex());
  for (int index = 0; index < plots_.size() && index < data_.size(); ++index) {
    const auto &counts = data_.at(index);
    plots_.at(index)->setWaveformPanels(mode == WaveformMode::Parade ? 3 : 1);
    plots_.at(index)->setImage(counts ? renderWaveformImage(*counts, mode, gainSpinBox_->value())
                                      : QImage());
  }
}

// ---- ベクトルスコープ ----

VectorscopeWindow::VectorscopeWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("ScopeWindows", "Vectorscope"), parent) {
  resize(900, 560);
  auto *controls = new QHBoxLayout;
  controls->addWidget(
      new QLabel(QCoreApplication::translate("VectorscopeWindow", "Brightness"), this));
  gainSpinBox_ = new QDoubleSpinBox(this);
  gainSpinBox_->setRange(0.1, 20.0);
  gainSpinBox_->setValue(1.0);
  gainSpinBox_->setSingleStep(0.25);
  gainSpinBox_->setPrefix("×");
  gainSpinBox_->setToolTip(
      QCoreApplication::translate("VectorscopeWindow", "How bright the points are drawn"));
  controls->addWidget(gainSpinBox_);
  graticuleCheckBox_ =
      new QCheckBox(QCoreApplication::translate("VectorscopeWindow", "Graticule"), this);
  graticuleCheckBox_->setChecked(true);
  graticuleCheckBox_->setToolTip(QCoreApplication::translate(
      "VectorscopeWindow",
      "Axes, circles, the skin tone line (dashed), and the 75% targets for R, Y, G, C, B and M"));
  controls->addWidget(graticuleCheckBox_);
  controls->addStretch(1);
  buildScopeLayout(this, controls, &plotContainer_, &plotLayout_);

  connect(gainSpinBox_, &QDoubleSpinBox::valueChanged, this, [this]() { renderPlots(); });
  connect(graticuleCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
    for (auto *plot : std::as_const(plots_)) {
      plot->setGraticuleVisible(checked);
    }
  });
}

void VectorscopeWindow::analysisImagesChanged() {
  const auto &images = analysisImages();
  data_.clear();
  weights_.clear();
  QVector<const AnalysisImage *> used;
  for (const auto &analysisImage : images) {
    if (analysisImage.image.isNull()) {
      continue;
    }
    VectorscopePtr counts =
        cache_.value(analysisImage, 0, [&]() { return buildVectorscope(analysisImage); });
    data_.push_back(counts);
    weights_.push_back(analysisImage.sampler.lumaWeights);
    used.push_back(&analysisImage);
  }
  const int columnsPerRow = std::max(1, std::min<int>(3, static_cast<int>(used.size())));
  ensurePlots(&plots_, plotContainer_, plotLayout_, ScopePlotWidget::Kind::Vectorscope,
              static_cast<int>(used.size()), columnsPerRow);
  for (int index = 0; index < used.size(); ++index) {
    plots_.at(index)->setTitle(plotTitle(*used.at(index), QString()));
    plots_.at(index)->setLumaWeights(weights_.at(index));
    plots_.at(index)->setGraticuleVisible(graticuleCheckBox_->isChecked());
  }
  renderPlots();
}

void VectorscopeWindow::renderPlots() {
  for (int index = 0; index < plots_.size() && index < data_.size(); ++index) {
    const auto &counts = data_.at(index);
    plots_.at(index)->setImage(
        counts ? renderVectorscopeImage(*counts, weights_.at(index), gainSpinBox_->value())
               : QImage());
  }
}

}  // namespace heiran::ui
