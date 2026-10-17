#include "heiran/ui/ChangeGraphWindow.hpp"

#include "heiran/ui/ColorTheme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

constexpr int kAxisLabelMargin = 44;
constexpr int kBottomLabelMargin = 20;

// セルを重ねて描くときの線の色。順番に割り当てる。
// チャネルの曲線とは別物で、セルどうしを見分けるための色。
const QColor kCellColors[] = {QColor(220, 68, 55),  QColor(50, 160, 80),  QColor(66, 133, 244),
                              QColor(240, 160, 20), QColor(150, 80, 200), QColor(20, 170, 170)};

QString formatValue(double value, double axisMaximum) {
  return QString::number(value, 'f', axisMaximum > 10.0 ? 1 : 2);
}

}  // namespace

// 実際に描く部分。窓の側は操作の受け口と配置だけを持つ。
class ChangeGraphChartWidget final : public QWidget {
public:
  explicit ChangeGraphChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumHeight(240);
  }

  void setData(int pageNumber, const QVector<ChangeGraphSeries> &series,
               ChangeGraphDirection direction, bool selectionActive) {
    pageNumber_ = pageNumber;
    series_ = series;
    direction_ = direction;
    selectionActive_ = selectionActive;
    update();
  }

  // 単一チャネル表示にするかどうか。重ね描きでは 1 セル 1 本にする必要がある。
  void setLumaOnly(bool lumaOnly) {
    lumaOnly_ = lumaOnly;
    update();
  }

  void setOverlayCells(bool overlayCells) {
    overlayCells_ = overlayCells;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    // 配色はここで 1 度求めて、以降の描画で使い回す。
    chartPalette_ = AnalysisChartPalette::fromPalette(palette());
    painter.fillRect(rect(), chartPalette_.background);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect contentRect = rect().adjusted(16, 12, -16, -12);
    painter.setPen(chartPalette_.titleText);
    painter.drawText(contentRect, Qt::AlignLeft | Qt::AlignTop,
                     QCoreApplication::translate("ChangeGraphWindow",
                                                 "Page %1  Scope %2  %3  Value axis 0.0 - %4")
                         .arg(pageNumber_)
                         .arg(selectionActive_
                                  ? QCoreApplication::translate("ChangeGraphWindow", "Selection")
                                  : QCoreApplication::translate("ChangeGraphWindow", "Full"))
                         .arg(direction_ == ChangeGraphDirection::Horizontal
                                  ? QCoreApplication::translate("ChangeGraphWindow",
                                                                "Column means (left to right)")
                                  : QCoreApplication::translate("ChangeGraphWindow",
                                                                "Row means (top to bottom)"))
                         .arg(axisMaximum(), 0, 'f', 2));

    if (series_.isEmpty()) {
      painter.drawText(contentRect, Qt::AlignCenter,
                       QCoreApplication::translate("ChangeGraphWindow", "No analyzable images"));
      return;
    }

    const int titleHeight = 22;
    const QRect plotArea = contentRect.adjusted(0, titleHeight, 0, 0);
    if (overlayCells_) {
      paintOverlay(&painter, plotArea);
      return;
    }

    const int spacing = 10;
    const int seriesCount = static_cast<int>(series_.size());
    const int seriesHeight =
        std::max(120, (plotArea.height() - spacing * (seriesCount - 1)) / seriesCount);
    for (int index = 0; index < seriesCount; ++index) {
      const QRect blockRect(plotArea.left(), plotArea.top() + index * (seriesHeight + spacing),
                            plotArea.width(), seriesHeight);
      // 1 つ分の描画が変えたフォントなどが、次のセルの見出しへ残らないよう、状態を戻す。
      painter.save();
      paintSeries(&painter, blockRect, series_.at(index));
      painter.restore();
    }
  }

private:
  // 縦軸の上限。整数画像では 1.0 で固定になり、ページを送っても軸が動かない。
  // 浮動小数画像は 1.0 を超えるため、実データの最大値まで広げる。
  double axisMaximum() const {
    float maximum = 1.0F;
    for (const auto &series : series_) {
      maximum = std::max(maximum, series.maximumMean());
    }
    return maximum;
  }

  // 横軸の長さ。セルで大きさが違っても同じ尺度で比べられるよう、最も長いものへ
  // そろえる。短いセルは途中で終わる形になる。
  int pointCount() const {
    int count = 0;
    for (const auto &series : series_) {
      count = std::max(count, static_cast<int>(series.lumaMeans.size()));
    }
    return count;
  }

  void drawMeans(QPainter *painter, const QRect &graphRect, const QVector<float> &means,
                 const QColor &color) const {
    const int totalPoints = pointCount();
    if (means.size() < 2 || totalPoints < 2) {
      return;
    }

    const double maximum = axisMaximum();
    QPainterPath path;
    for (int index = 0; index < means.size(); ++index) {
      const double x =
          graphRect.left() + (static_cast<double>(index) / (totalPoints - 1)) * graphRect.width();
      const double ratio = std::clamp(static_cast<double>(means.at(index)) / maximum, 0.0, 1.0);
      const double y = graphRect.bottom() - ratio * graphRect.height();
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }

    painter->setPen(QPen(color, 1.5));
    painter->drawPath(path);
  }

  void drawGrid(QPainter *painter, const QRect &graphRect, int origin) const {
    painter->setPen(chartPalette_.frame);
    painter->drawRect(graphRect);

    QFont labelFont = painter->font();
    labelFont.setPointSize(7);
    painter->setFont(labelFont);

    const int totalPoints = pointCount();

    // 横軸。位置を画像座標で打つ。選択範囲を指定した場合はその左端または上端から。
    constexpr int kPositionTicks = 4;
    for (int tick = 0; tick <= kPositionTicks; ++tick) {
      const double fraction = static_cast<double>(tick) / kPositionTicks;
      const int x = graphRect.left() + static_cast<int>(std::lround(fraction * graphRect.width()));
      if (tick > 0 && tick < kPositionTicks) {
        painter->setPen(chartPalette_.gridLine);
        painter->drawLine(x, graphRect.top() + 1, x, graphRect.bottom() - 1);
      }
      painter->setPen(chartPalette_.axisText);
      const int position =
          origin + static_cast<int>(std::lround(fraction * std::max(0, totalPoints - 1)));
      painter->drawText(QRect(x - 30, graphRect.bottom() + 2, 60, kBottomLabelMargin - 2),
                        Qt::AlignHCenter | Qt::AlignTop, QString::number(position));
    }

    // 縦軸。平均値を打つ。
    const double maximum = axisMaximum();
    constexpr int kValueTicks = 4;
    for (int tick = 0; tick <= kValueTicks; ++tick) {
      const double fraction = static_cast<double>(tick) / kValueTicks;
      const int y =
          graphRect.bottom() - static_cast<int>(std::lround(fraction * graphRect.height()));
      if (tick > 0 && tick < kValueTicks) {
        painter->setPen(chartPalette_.gridLine);
        painter->drawLine(graphRect.left() + 1, y, graphRect.right() - 1, y);
      }
      painter->setPen(chartPalette_.axisText);
      painter->drawText(QRect(graphRect.left() - kAxisLabelMargin, y - 8, kAxisLabelMargin - 4, 16),
                        Qt::AlignRight | Qt::AlignVCenter,
                        formatValue(fraction * maximum, maximum));
    }
  }

  void paintSeries(QPainter *painter, const QRect &blockRect,
                   const ChangeGraphSeries &series) const {
    painter->setPen(chartPalette_.seriesLabel);
    painter->drawText(
        blockRect.adjusted(0, 0, 0, -blockRect.height() + 16), Qt::AlignLeft | Qt::AlignVCenter,
        QCoreApplication::translate("ChangeGraphWindow", "%1%2  %3 samples per point")
            .arg(series.label)
            .arg(series.selectionApplied
                     ? QCoreApplication::translate("ChangeGraphWindow", " [selection]")
                     : "")
            .arg(series.samplesPerPoint));

    const QRect graphRect = blockRect.adjusted(kAxisLabelMargin, 18, 0, -kBottomLabelMargin);
    if (graphRect.height() <= 0) {
      return;
    }

    drawGrid(painter, graphRect, series.origin);

    if (lumaOnly_ || series.grayscale) {
      drawMeans(painter, graphRect, series.lumaMeans, chartPalette_.grayscaleCurve);
      return;
    }

    drawMeans(painter, graphRect, series.redMeans, chartPalette_.redCurve);
    drawMeans(painter, graphRect, series.greenMeans, chartPalette_.greenCurve);
    drawMeans(painter, graphRect, series.blueMeans, chartPalette_.blueCurve);
  }

  // 全セルを 1 つの図へ重ねる。セルごとの差を見るときはこちらが読みやすい。
  // 1 セル 1 本にするため、チャネルは輝度に固定する。
  void paintOverlay(QPainter *painter, const QRect &plotArea) const {
    const int legendHeight = 16 * static_cast<int>(series_.size());
    const QRect graphRect =
        plotArea.adjusted(kAxisLabelMargin, 0, 0, -(kBottomLabelMargin + legendHeight));
    if (graphRect.height() <= 0) {
      return;
    }

    drawGrid(painter, graphRect, series_.isEmpty() ? 0 : series_.first().origin);

    const int colorCount = static_cast<int>(std::size(kCellColors));
    QFont legendFont = painter->font();
    legendFont.setPointSize(7);
    for (int index = 0; index < series_.size(); ++index) {
      // 暗い下地では沈むため明るくする。線の見分けは色相で付ける。
      const bool darkBackground = chartPalette_.background.lightness() < 128;
      const QColor baseColor = kCellColors[index % colorCount];
      const QColor color = darkBackground ? baseColor.lighter(125) : baseColor;
      drawMeans(painter, graphRect, series_.at(index).lumaMeans, color);

      painter->setFont(legendFont);
      painter->setPen(color);
      painter->drawText(QRect(graphRect.left(),
                              graphRect.bottom() + kBottomLabelMargin + index * 16,
                              graphRect.width(), 16),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        QCoreApplication::translate("ChangeGraphWindow", "%1 (luma)")
                            .arg(series_.at(index).label));
    }
  }

  // 描画中に使う配色。`paintEvent` の先頭で求める。
  AnalysisChartPalette chartPalette_;
  int pageNumber_ = 1;
  bool selectionActive_ = false;
  bool lumaOnly_ = false;
  bool overlayCells_ = false;
  ChangeGraphDirection direction_ = ChangeGraphDirection::Horizontal;
  QVector<ChangeGraphSeries> series_;
};

ChangeGraphWindow::ChangeGraphWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("ChangeGraphWindow", "Change Graph"), parent) {
  resize(960, 640);

  auto *rootLayout = new QVBoxLayout(this);
  auto *controlLayout = new QHBoxLayout();

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("ChangeGraphWindow", "Direction"), this));
  directionCombo_ = new QComboBox(this);
  directionCombo_->addItems({"horizontal", "vertical"});
  directionCombo_->setToolTip(QCoreApplication::translate(
      "ChangeGraphWindow",
      "horizontal averages each column and shows the change from left to right."));
  controlLayout->addWidget(directionCombo_);

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("ChangeGraphWindow", "Channel"), this));
  channelCombo_ = new QComboBox(this);
  channelCombo_->addItems({"color", "luma"});
  controlLayout->addWidget(channelCombo_);

  overlayCellsCheckBox_ =
      new QCheckBox(QCoreApplication::translate("ChangeGraphWindow", "Overlay cells"), this);
  overlayCellsCheckBox_->setToolTip(QCoreApplication::translate(
      "ChangeGraphWindow",
      "Draws every cell in one plot. The channel is fixed to luma so each cell is one line."));
  controlLayout->addWidget(overlayCellsCheckBox_);

  controlLayout->addStretch(1);
  rootLayout->addLayout(controlLayout);

  chart_ = new ChangeGraphChartWidget(this);
  rootLayout->addWidget(chart_, 1);

  connect(directionCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) { analysisImagesChanged(); });
  connect(channelCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int index) { chart_->setLumaOnly(index == 1); });
  connect(overlayCellsCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
    // 重ねるときは 1 セル 1 本にする必要があるため、チャネルの選択は効かない。
    channelCombo_->setEnabled(!checked);
    chart_->setOverlayCells(checked);
  });
}

ChangeGraphDirection ChangeGraphWindow::currentDirection() const {
  return directionCombo_->currentIndex() == 1 ? ChangeGraphDirection::Vertical
                                              : ChangeGraphDirection::Horizontal;
}

void ChangeGraphWindow::analysisImagesChanged() {
  const auto &images = analysisImages();
  const ChangeGraphDirection direction = currentDirection();

  QVector<ChangeGraphSeries> series;
  bool selectionActive = false;
  for (const auto &analysisImage : images) {
    if (!analysisImage.image.isNull()) {
      ChangeGraphSeries cellSeries = cache_.value(analysisImage, direction, [&]() {
        return buildChangeGraphSeries(analysisImage, direction);
      });
      // ラベルは集計に影響しないため鍵に入れていない。表示に使う分だけ入れ直す。
      cellSeries.label = analysisImage.label;
      series.push_back(cellSeries);
    }
    if (analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty()) {
      selectionActive = true;
    }
  }

  chart_->setData(pageNumber(), series, direction, selectionActive);
}

}  // namespace heiran::ui
