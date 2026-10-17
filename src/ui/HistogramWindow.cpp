#include "heiran/ui/HistogramWindow.hpp"

#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/HistogramAxis.hpp"
#include "heiran/ui/HistogramSeriesCache.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

namespace heiran::ui {

namespace {

constexpr int kAxisLabelMargin = 44;
constexpr int kBottomLabelMargin = 20;
constexpr int kStatisticsHeight = 44;

QString formatAxisValue(double value, double axisMaximum) {
  // 0.0-1.0 に収まる画像は小数 2 桁、1.0 を超える画像は桁数を抑える。
  return QString::number(value, 'f', axisMaximum > 10.0 ? 1 : 2);
}

QString formatStatistics(const QString &channelName, const HistogramChannelStatistics &statistics) {
  return QCoreApplication::translate("HistogramWindow",
                                     "%1 mean %2  sd %3  min %4  max %5  peak %6 (%7%)")
      .arg(channelName)
      .arg(statistics.mean, 0, 'f', 4)
      .arg(statistics.standardDeviation, 0, 'f', 4)
      .arg(statistics.minimum, 0, 'f', 4)
      .arg(statistics.maximum, 0, 'f', 4)
      .arg(statistics.peakValue, 0, 'f', 4)
      .arg(statistics.peakRatio * 100.0, 0, 'f', 2);
}

}  // namespace

// 実際に描く部分。窓の側は操作の受け口と配置だけを持つ。
class HistogramChartWidget final : public QWidget {
public:
  explicit HistogramChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumHeight(240);
    // ボタンを押していなくても、カーソルの動きを受けて、下の値を出す。
    setMouseTracking(true);
  }

  void setData(int pageNumber, const QVector<HistogramSeries> &series, double axisMaximum,
               bool selectionActive) {
    pageNumber_ = pageNumber;
    series_ = series;
    axisMaximum_ = axisMaximum > 0.0 ? axisMaximum : 1.0;
    selectionActive_ = selectionActive;
    update();
  }

  // 横軸の表示範囲。軸全体に対する割合。
  void setRange(HistogramRange range) {
    range_ = clampHistogramRange(range);
    update();
  }

  HistogramRange range() const { return range_; }

  // 縦軸の上限 (割合、0-1)。0 なら、セルごとに自動 (そのセルの最大)。正なら全セルで同じ上限に
  // 固定する。セル同士を同じ縦の尺度で見比べたいときに使う。
  void setFixedMaxRatio(double ratio) {
    fixedMaxRatio_ = ratio > 0.0 ? ratio : 0.0;
    update();
  }

  // いまの範囲で、どのセルの最大も収まる縦軸の上限 (割合)。固定の上限の初期値に使う。
  double autoMaxRatio() const {
    double maximum = 0.0;
    for (const auto &series : series_) {
      if (series.pixelCount <= 0) {
        continue;
      }
      for (const auto &bins : {series.redBins, series.greenBins, series.blueBins}) {
        maximum = std::max(maximum, static_cast<double>(histogramMaxCountInRange(bins, range_)) /
                                        static_cast<double>(series.pixelCount));
      }
    }
    return maximum;
  }

  // Ctrl + ホイールで縦の倍率が変わったときに、倍率へ掛ける値を渡して呼ばれる。
  std::function<void(double)> onVerticalZoom;

  // 範囲がグラフ側の操作 (ホイール、ダブルクリック) で変わったときに呼ばれる。
  std::function<void()> onRangeChanged;

  void setVerticalScale(double verticalScale) {
    verticalScale_ = verticalScale > 0.0 ? verticalScale : 1.0;
    update();
  }

  void setLogarithmic(bool logarithmic) {
    logarithmic_ = logarithmic;
    update();
  }

  void setStatisticsVisible(bool visible) {
    statisticsVisible_ = visible;
    update();
  }

protected:
  // ホイールで、カーソルの下を中心に横軸を拡大縮小する。
  void wheelEvent(QWheelEvent *event) override {
    // Ctrl を押していれば、縦軸の拡大縮小。
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
      constexpr double kVerticalStep = 1.25;
      if (onVerticalZoom) {
        onVerticalZoom(event->angleDelta().y() > 0 ? kVerticalStep : 1.0 / kVerticalStep);
      }
      event->accept();
      return;
    }
    if (graphRects_.isEmpty() || graphRects_.first().isNull()) {
      event->ignore();
      return;
    }
    const QRect graphRect = graphRects_.first();
    const double anchor =
        histogramFractionForX(event->position().x(), graphRect.left(), graphRect.width(), range_);
    constexpr double kZoomStep = 0.8;
    range_ = zoomHistogramRange(range_, anchor,
                                event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep);
    update();
    if (onRangeChanged) {
      onRangeChanged();
    }
    event->accept();
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    // ダブルクリックで全体へ戻す。
    range_ = HistogramRange{};
    update();
    if (onRangeChanged) {
      onRangeChanged();
    }
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    hoverPosition_ = event->position().toPoint();
    hovering_ = true;
    update();
  }

  void leaveEvent(QEvent *) override {
    hovering_ = false;
    update();
  }

  void paintEvent(QPaintEvent *) override {
    graphRects_.clear();
    QPainter painter(this);
    // 配色はここで 1 度求めて、以降の描画で使い回す。
    chartPalette_ = AnalysisChartPalette::fromPalette(palette());
    painter.fillRect(rect(), chartPalette_.background);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect contentRect = rect().adjusted(16, 12, -16, -12);
    painter.setPen(chartPalette_.titleText);
    painter.drawText(
        contentRect, Qt::AlignLeft | Qt::AlignTop,
        QCoreApplication::translate("HistogramWindow", "Page %1  Scope %2  Value axis 0.0 - %3")
            .arg(pageNumber_)
            .arg(selectionActive_ ? QCoreApplication::translate("HistogramWindow", "Selection")
                                  : QCoreApplication::translate("HistogramWindow", "Full"))
            .arg(axisMaximum_, 0, 'f', axisMaximum_ > 1.0 ? 2 : 1));

    if (series_.isEmpty()) {
      painter.drawText(contentRect, Qt::AlignCenter,
                       QCoreApplication::translate("HistogramWindow", "No analyzable images"));
      return;
    }

    const int titleHeight = 22;
    const int spacing = 10;
    const int blockExtra = statisticsVisible_ ? kStatisticsHeight : 0;
    const int seriesCount = static_cast<int>(series_.size());
    const int seriesHeight =
        std::max(120 + blockExtra,
                 (contentRect.height() - titleHeight - spacing * (seriesCount - 1)) / seriesCount);

    for (int seriesIndex = 0; seriesIndex < seriesCount; ++seriesIndex) {
      const QRect blockRect(contentRect.left(),
                            contentRect.top() + titleHeight +
                                seriesIndex * (seriesHeight + spacing),
                            contentRect.width(), seriesHeight);
      // 1 つ分の描画が変えたフォントなどが、次のセルの見出しへ残らないよう、状態を戻す。
      painter.save();
      paintSeries(&painter, blockRect, series_.at(seriesIndex));
      painter.restore();
    }

    paintHover(&painter);
  }

private:
  // 割合を縦位置へ写す。0.0 が下端、fullScaleRatio_ 以上が上端になる。
  double plotRatio(double ratio, double fullScaleRatio) const {
    if (fullScaleRatio <= 0.0) {
      return 0.0;
    }

    const double scaled = ratio / fullScaleRatio * verticalScale_;
    if (!logarithmic_) {
      return std::clamp(scaled, 0.0, 1.0);
    }

    // 対数軸。1 つの山が突出して他が潰れる場合に使う。
    // 0 を扱えるよう 1 を足してから対数を取る。
    constexpr double kLogGain = 99.0;
    return std::clamp(std::log10(1.0 + std::clamp(scaled, 0.0, 1.0) * kLogGain) / 2.0, 0.0, 1.0);
  }

  void drawHistogramPath(QPainter *painter, const QRect &graphRect, const QVector<int> &bins,
                         const QColor &color, qint64 pixelCount, double fullScaleRatio) const {
    if (bins.size() < 2 || pixelCount <= 0) {
      return;
    }

    QPainterPath path;
    for (int index = 0; index < bins.size(); ++index) {
      const double x = histogramXForFraction(histogramFractionForBin(index, bins.size()),
                                             graphRect.left(), graphRect.width(), range_);
      const double ratio = static_cast<double>(bins.at(index)) / static_cast<double>(pixelCount);
      const double y = graphRect.bottom() - plotRatio(ratio, fullScaleRatio) * graphRect.height();
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }

    // 表示範囲の外へはみ出す分は、枠で切る。
    painter->save();
    painter->setClipRect(graphRect.adjusted(1, 1, 0, 0));
    painter->setPen(QPen(color, 1.5));
    painter->drawPath(path);
    painter->restore();
  }

  void drawGrid(QPainter *painter, const QRect &graphRect, double fullScaleRatio) const {
    painter->setPen(chartPalette_.frame);
    painter->drawRect(graphRect);

    QFont labelFont = painter->font();
    labelFont.setPointSize(7);
    painter->setFont(labelFont);

    // 横軸。画素値を 4 分割して目盛りを打つ。
    constexpr int kValueTicks = 4;
    for (int tick = 0; tick <= kValueTicks; ++tick) {
      const double fraction = static_cast<double>(tick) / kValueTicks;
      const int x = graphRect.left() + static_cast<int>(std::lround(fraction * graphRect.width()));
      if (tick > 0 && tick < kValueTicks) {
        painter->setPen(chartPalette_.gridLine);
        painter->drawLine(x, graphRect.top() + 1, x, graphRect.bottom() - 1);
      }
      painter->setPen(chartPalette_.axisText);
      painter->drawText(
          QRect(x - 30, graphRect.bottom() + 2, 60, kBottomLabelMargin - 2),
          Qt::AlignHCenter | Qt::AlignTop,
          formatAxisValue((range_.low + fraction * range_.span()) * axisMaximum_, axisMaximum_));
    }

    // 縦軸。全体に占める割合を打つ。対数軸では目盛りの間隔が変わる。
    constexpr int kRatioTicks = 4;
    for (int tick = 0; tick <= kRatioTicks; ++tick) {
      const double fraction = static_cast<double>(tick) / kRatioTicks;
      const int y =
          graphRect.bottom() - static_cast<int>(std::lround(fraction * graphRect.height()));
      if (tick > 0 && tick < kRatioTicks) {
        painter->setPen(chartPalette_.gridLine);
        painter->drawLine(graphRect.left() + 1, y, graphRect.right() - 1, y);
      }

      // 目盛りの位置に対応する割合を逆算する。
      double ratio = 0.0;
      if (fullScaleRatio > 0.0) {
        const double scaled =
            logarithmic_ ? (std::pow(10.0, fraction * 2.0) - 1.0) / 99.0 : fraction;
        ratio = scaled * fullScaleRatio / verticalScale_;
      }
      painter->setPen(chartPalette_.axisText);
      painter->drawText(QRect(graphRect.left() - kAxisLabelMargin, y - 8, kAxisLabelMargin - 4, 16),
                        Qt::AlignRight | Qt::AlignVCenter,
                        QString("%1%").arg(ratio * 100.0, 0, 'f', ratio < 0.01 ? 2 : 1));
    }
  }

  void paintSeries(QPainter *painter, const QRect &blockRect, const HistogramSeries &series) const {
    painter->setPen(chartPalette_.seriesLabel);
    painter->drawText(blockRect.adjusted(0, 0, 0, -blockRect.height() + 16),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      series.selectionApplied
                          ? QCoreApplication::translate("HistogramWindow", "%1 [selection]  %2 px")
                                .arg(series.label)
                                .arg(series.pixelCount)
                          : QCoreApplication::translate("HistogramWindow", "%1  %2 px")
                                .arg(series.label)
                                .arg(series.pixelCount));

    const int statisticsHeight = statisticsVisible_ ? kStatisticsHeight : 0;
    const QRect graphRect =
        blockRect.adjusted(kAxisLabelMargin, 18, 0, -(kBottomLabelMargin + statisticsHeight));
    graphRects_.push_back(graphRect.height() > 0 ? graphRect : QRect());
    if (graphRect.height() <= 0) {
      return;
    }

    // 縦軸の上限は、表示範囲の中で最も多いビンの割合
    // (自動)。固定を指定していれば、全セル共通でそれ。
    int maximumCount = 0;
    for (const auto &bins : {series.redBins, series.greenBins, series.blueBins}) {
      maximumCount = std::max(maximumCount, histogramMaxCountInRange(bins, range_));
    }
    const double autoRatio =
        series.pixelCount > 0 ? static_cast<double>(maximumCount) / series.pixelCount : 0.0;
    const double fullScaleRatio = histogramFullScaleRatio(autoRatio, fixedMaxRatio_);

    drawGrid(painter, graphRect, fullScaleRatio);

    if (series.grayscale) {
      drawHistogramPath(painter, graphRect, series.redBins, chartPalette_.grayscaleCurve,
                        series.pixelCount, fullScaleRatio);
    } else {
      drawHistogramPath(painter, graphRect, series.redBins, chartPalette_.redCurve,
                        series.pixelCount, fullScaleRatio);
      drawHistogramPath(painter, graphRect, series.greenBins, chartPalette_.greenCurve,
                        series.pixelCount, fullScaleRatio);
      drawHistogramPath(painter, graphRect, series.blueBins, chartPalette_.blueCurve,
                        series.pixelCount, fullScaleRatio);
    }

    if (!statisticsVisible_) {
      return;
    }

    QFont statisticsFont = painter->font();
    statisticsFont.setPointSize(7);
    statisticsFont.setFamilies({"Consolas", "Courier New", "monospace"});
    painter->setFont(statisticsFont);
    painter->setPen(chartPalette_.statisticsText);

    const int statisticsTop = graphRect.bottom() + kBottomLabelMargin;
    if (series.grayscale) {
      painter->drawText(QRect(graphRect.left(), statisticsTop, graphRect.width(), 14),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        formatStatistics(QCoreApplication::translate("HistogramWindow", "Gray"),
                                         series.redStatistics));
      return;
    }

    const HistogramChannelStatistics channels[3] = {series.redStatistics, series.greenStatistics,
                                                    series.blueStatistics};
    const char *names[3] = {"R", "G", "B"};
    for (int channel = 0; channel < 3; ++channel) {
      painter->drawText(
          QRect(graphRect.left(), statisticsTop + channel * 14, graphRect.width(), 14),
          Qt::AlignLeft | Qt::AlignVCenter, formatStatistics(names[channel], channels[channel]));
    }
  }

  // カーソルの下のビンの値を、縦の線と小さな枠で出す。
  void paintHover(QPainter *painter) const {
    if (!hovering_) {
      return;
    }
    for (int seriesIndex = 0; seriesIndex < graphRects_.size() && seriesIndex < series_.size();
         ++seriesIndex) {
      const QRect graphRect = graphRects_.at(seriesIndex);
      if (graphRect.isNull() || !graphRect.contains(hoverPosition_)) {
        continue;
      }

      const HistogramSeries &series = series_.at(seriesIndex);
      const int binCount = static_cast<int>(series.redBins.size());
      if (binCount < 2) {
        return;
      }
      const double fraction =
          histogramFractionForX(hoverPosition_.x(), graphRect.left(), graphRect.width(), range_);
      const int bin = histogramBinForFraction(fraction, binCount);
      const double snapped = histogramFractionForBin(bin, binCount);
      const double x = histogramXForFraction(snapped, graphRect.left(), graphRect.width(), range_);
      if (x < graphRect.left() || x > graphRect.right()) {
        return;
      }

      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, false);
      painter->setPen(QPen(chartPalette_.axisText, 1, Qt::DashLine));
      painter->drawLine(QPointF(x, graphRect.top()), QPointF(x, graphRect.bottom()));

      const auto describe = [&series](const QString &name, const QVector<int> &bins, int index) {
        const int count = bins.at(index);
        const double ratio =
            series.pixelCount > 0 ? 100.0 * static_cast<double>(count) / series.pixelCount : 0.0;
        return QString("%1 %2 (%3%)").arg(name, -2).arg(count, 8).arg(ratio, 0, 'f', 2);
      };
      QStringList lines;
      lines << QCoreApplication::translate("HistogramWindow", "value %1  bin %2")
                   .arg(snapped * axisMaximum_, 0, 'f', axisMaximum_ > 10.0 ? 2 : 4)
                   .arg(bin);
      if (series.grayscale) {
        lines << describe(QCoreApplication::translate("HistogramWindow", "Gray"), series.redBins,
                          bin);
      } else {
        lines << describe("R", series.redBins, bin) << describe("G", series.greenBins, bin)
              << describe("B", series.blueBins, bin);
      }

      QFont font = painter->font();
      font.setPointSize(8);
      font.setFamilies({"Consolas", "Courier New", "monospace"});
      painter->setFont(font);
      const QFontMetrics metrics(font);
      int textWidth = 0;
      for (const auto &line : lines) {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
      }
      const int lineHeight = metrics.height();
      QRect box(0, 0, textWidth + 16, lineHeight * static_cast<int>(lines.size()) + 12);
      box.moveTopLeft(hoverPosition_ + QPoint(14, 14));
      if (box.right() > width() - 4) {
        box.moveRight(hoverPosition_.x() - 14);
      }
      if (box.bottom() > height() - 4) {
        box.moveBottom(hoverPosition_.y() - 14);
      }
      box.moveLeft(std::max(4, box.left()));
      box.moveTop(std::max(4, box.top()));

      QColor fill = chartPalette_.background;
      fill.setAlpha(238);
      painter->setPen(chartPalette_.frame);
      painter->setBrush(fill);
      painter->drawRoundedRect(box, 4, 4);
      painter->setPen(chartPalette_.titleText);
      for (int line = 0; line < lines.size(); ++line) {
        painter->drawText(
            QRect(box.left() + 8, box.top() + 6 + line * lineHeight, box.width() - 16, lineHeight),
            Qt::AlignLeft | Qt::AlignVCenter, lines.at(line));
      }
      painter->restore();
      return;
    }
  }

  // 描画中に使う配色。`paintEvent` の先頭で求める。
  AnalysisChartPalette chartPalette_;
  int pageNumber_ = 1;
  bool selectionActive_ = false;
  bool logarithmic_ = false;
  bool statisticsVisible_ = true;
  double verticalScale_ = 1.0;
  double axisMaximum_ = 1.0;
  HistogramRange range_;
  double fixedMaxRatio_ = 0.0;
  QVector<HistogramSeries> series_;
  // 描いたグラフの枠 (系列と同じ順)。カーソルの位置から、どの系列のどのビンかを引くのに使う。
  mutable QVector<QRect> graphRects_;
  QPoint hoverPosition_;
  bool hovering_ = false;
};

HistogramWindow::HistogramWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("HistogramWindow", "Histogram"), parent) {
  resize(960, 640);

  auto *rootLayout = new QVBoxLayout(this);
  auto *controlLayout = new QHBoxLayout();

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("HistogramWindow", "Vertical scale"), this));
  verticalScaleSpinBox_ = new QDoubleSpinBox(this);
  verticalScaleSpinBox_->setRange(0.1, 100.0);
  verticalScaleSpinBox_->setSingleStep(0.5);
  verticalScaleSpinBox_->setValue(1.0);
  verticalScaleSpinBox_->setToolTip(QCoreApplication::translate(
      "HistogramWindow", "Multiplies the drawn height. Raise it to see small populations."));
  controlLayout->addWidget(verticalScaleSpinBox_);

  // 横軸の表示範囲。明部だけ、暗部だけを拡大して見る。グラフ上のホイールでも変えられる。
  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("HistogramWindow", "Range"), this));
  rangeLowSpinBox_ = new QDoubleSpinBox(this);
  rangeHighSpinBox_ = new QDoubleSpinBox(this);
  for (auto *spinBox : {rangeLowSpinBox_, rangeHighSpinBox_}) {
    spinBox->setDecimals(3);
    spinBox->setRange(0.0, 1.0);
    spinBox->setKeyboardTracking(false);
    spinBox->setToolTip(QCoreApplication::translate(
        "HistogramWindow", "Horizontal range to show. The mouse wheel over the graph zooms around "
                           "the cursor, and a double click resets it."));
  }
  rangeHighSpinBox_->setValue(1.0);
  controlLayout->addWidget(rangeLowSpinBox_);
  controlLayout->addWidget(new QLabel("-", this));
  controlLayout->addWidget(rangeHighSpinBox_);
  auto *resetRangeButton =
      new QPushButton(QCoreApplication::translate("HistogramWindow", "Reset"), this);
  resetRangeButton->setToolTip(
      QCoreApplication::translate("HistogramWindow", "Show the whole range."));
  controlLayout->addWidget(resetRangeButton);

  // 縦軸の上限を固定する。既定は、セルごとにそのセルの最大へ合わせる (自動)。固定すると、
  // 全セルが同じ縦の尺度になり、山の高さをセル同士で比べられる。
  fixedMaxCheckBox_ =
      new QCheckBox(QCoreApplication::translate("HistogramWindow", "Fixed max"), this);
  fixedMaxCheckBox_->setToolTip(QCoreApplication::translate(
      "HistogramWindow", "Use the same vertical maximum for every cell, so the heights "
                         "can be compared. Ctrl + wheel over the graph zooms vertically."));
  controlLayout->addWidget(fixedMaxCheckBox_);
  fixedMaxSpinBox_ = new QDoubleSpinBox(this);
  fixedMaxSpinBox_->setRange(0.01, 100.0);
  fixedMaxSpinBox_->setDecimals(2);
  fixedMaxSpinBox_->setSuffix(" %");
  fixedMaxSpinBox_->setValue(10.0);
  fixedMaxSpinBox_->setKeyboardTracking(false);
  fixedMaxSpinBox_->setEnabled(false);
  fixedMaxSpinBox_->setToolTip(QCoreApplication::translate(
      "HistogramWindow", "Share of all pixels at the top of the vertical axis."));
  controlLayout->addWidget(fixedMaxSpinBox_);

  logarithmicCheckBox_ =
      new QCheckBox(QCoreApplication::translate("HistogramWindow", "Logarithmic"), this);
  logarithmicCheckBox_->setToolTip(QCoreApplication::translate(
      "HistogramWindow", "Use a logarithmic vertical axis when one peak dominates."));
  controlLayout->addWidget(logarithmicCheckBox_);

  statisticsCheckBox_ =
      new QCheckBox(QCoreApplication::translate("HistogramWindow", "Statistics"), this);
  statisticsCheckBox_->setChecked(true);
  controlLayout->addWidget(statisticsCheckBox_);

  controlLayout->addStretch(1);
  rootLayout->addLayout(controlLayout);

  chart_ = new HistogramChartWidget(this);
  rootLayout->addWidget(chart_, 1);

  chart_->onRangeChanged = [this]() { syncRangeControls(); };
  connect(rangeLowSpinBox_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    const HistogramRange current = chart_->range();
    chart_->setRange({value / axisMaximum_, current.high});
    syncRangeControls();
  });
  connect(rangeHighSpinBox_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    const HistogramRange current = chart_->range();
    chart_->setRange({current.low, value / axisMaximum_});
    syncRangeControls();
  });
  connect(resetRangeButton, &QPushButton::clicked, this, [this]() {
    chart_->setRange(HistogramRange{});
    syncRangeControls();
  });
  connect(fixedMaxCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
    fixedMaxSpinBox_->setEnabled(checked);
    if (checked) {
      // いまの見え方から始める。急に尺度が変わらないよう、全セルの最大を切り上げて入れる。
      const double autoPercent = chart_->autoMaxRatio() * 100.0;
      if (autoPercent > 0.0) {
        const QSignalBlocker blocker(fixedMaxSpinBox_);
        fixedMaxSpinBox_->setValue(std::clamp(std::ceil(autoPercent), 0.01, 100.0));
      }
    }
    chart_->setFixedMaxRatio(checked ? fixedMaxSpinBox_->value() / 100.0 : 0.0);
  });
  connect(fixedMaxSpinBox_, &QDoubleSpinBox::valueChanged, this, [this](double percent) {
    if (fixedMaxCheckBox_->isChecked()) {
      chart_->setFixedMaxRatio(percent / 100.0);
    }
  });
  chart_->onVerticalZoom = [this](double factor) {
    verticalScaleSpinBox_->setValue(verticalScaleSpinBox_->value() * factor);
  };
  connect(verticalScaleSpinBox_, &QDoubleSpinBox::valueChanged, this,
          [this](double value) { chart_->setVerticalScale(value); });
  connect(logarithmicCheckBox_, &QCheckBox::toggled, this,
          [this](bool checked) { chart_->setLogarithmic(checked); });
  connect(statisticsCheckBox_, &QCheckBox::toggled, this,
          [this](bool checked) { chart_->setStatisticsVisible(checked); });
}

// 範囲の入力欄を、グラフの範囲と軸の上限へ合わせる。入力欄の変更を、グラフへ送り返さない。
void HistogramWindow::syncRangeControls() {
  const HistogramRange range = chart_->range();
  const double step = axisMaximum_ / 100.0;
  for (auto *spinBox : {rangeLowSpinBox_, rangeHighSpinBox_}) {
    const QSignalBlocker blocker(spinBox);
    spinBox->setRange(0.0, axisMaximum_);
    spinBox->setSingleStep(step);
  }
  {
    const QSignalBlocker blocker(rangeLowSpinBox_);
    rangeLowSpinBox_->setValue(range.low * axisMaximum_);
  }
  {
    const QSignalBlocker blocker(rangeHighSpinBox_);
    rangeHighSpinBox_->setValue(range.high * axisMaximum_);
  }
}

void HistogramWindow::analysisImagesChanged() {
  const auto &images = analysisImages();

  // 横軸は全セルで共通にする。セルごとに軸が変わると比較できないため。
  double axisMaximum = 1.0;
  for (const auto &analysisImage : images) {
    if (!analysisImage.image.isNull()) {
      axisMaximum = std::max(axisMaximum, cache_.sampleMaximum(analysisImage));
    }
  }

  QVector<HistogramSeries> series;
  bool selectionActive = false;
  for (const auto &analysisImage : images) {
    if (!analysisImage.image.isNull()) {
      series.push_back(cache_.series(analysisImage, axisMaximum));
    }
    if (analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty()) {
      selectionActive = true;
    }
  }

  chart_->setData(pageNumber(), series, axisMaximum, selectionActive);
  axisMaximum_ = axisMaximum;
  syncRangeControls();
}

}  // namespace heiran::ui
