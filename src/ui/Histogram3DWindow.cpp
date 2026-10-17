#include "heiran/ui/Histogram3DWindow.hpp"

#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/Histogram3D.hpp"
#include "heiran/ui/HistogramSeriesBuilder.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

// 点の大きさを、画素数の多さからどう決めるか。
enum class PointSizeMode {
  Linear,
  SquareRoot,
  Logarithmic,
};

constexpr double kDefaultYaw = 35.0;
constexpr double kDefaultPitch = 22.0;

// 画素数の割合 (最大を 1 とする) から、点の半径の割合 (0-1) を求める。
// 最大の山に他が埋もれないよう、既定は平方根。
double pointSizeFraction(double ratio, PointSizeMode mode) {
  ratio = std::clamp(ratio, 0.0, 1.0);
  switch (mode) {
    case PointSizeMode::Linear:
      return ratio;
    case PointSizeMode::Logarithmic:
      return std::log10(1.0 + ratio * 99.0) / 2.0;
    case PointSizeMode::SquareRoot:
    default:
      return std::sqrt(ratio);
  }
}

}  // namespace

// 実際に描く部分。ビンを球に見立てて、奥のものから順に円で描く。
class Histogram3DView final : public QWidget {
public:
  explicit Histogram3DView(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumSize(360, 320);
    setMouseTracking(true);
  }

  void setHistogram(Histogram3D histogram) {
    histogram_ = std::move(histogram);
    hoveredIndex_ = -1;
    update();
  }

  void setPointSizeMode(PointSizeMode mode) {
    sizeMode_ = mode;
    update();
  }

  void resetView() {
    yaw_ = kDefaultYaw;
    pitch_ = kDefaultPitch;
    zoom_ = 1.0;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    palette_ = AnalysisChartPalette::fromPalette(palette());
    painter.fillRect(rect(), palette_.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    points_.clear();

    if (histogram_.binsPerAxis <= 0 || histogram_.pixelCount <= 0) {
      painter.setPen(palette_.axisText);
      painter.drawText(rect(), Qt::AlignCenter,
                       QCoreApplication::translate("Histogram3DWindow", "No analyzable image"));
      return;
    }

    paintCube(&painter);
    paintBins(&painter);
    paintHover(&painter);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = true;
      lastDragPosition_ = event->position().toPoint();
      setCursor(Qt::ClosedHandCursor);
    }
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
      unsetCursor();
    }
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    const QPoint position = event->position().toPoint();
    hoverPosition_ = position;
    if (dragging_) {
      // 横のドラッグで水平に回し、縦のドラッグで上下に振る。
      constexpr double kDegreesPerPixel = 0.5;
      const QPoint delta = position - lastDragPosition_;
      yaw_ += delta.x() * kDegreesPerPixel;
      pitch_ = std::clamp(pitch_ + delta.y() * kDegreesPerPixel, -89.0, 89.0);
      lastDragPosition_ = position;
      hoveredIndex_ = -1;
    } else {
      hoveredIndex_ = binUnderCursor(position);
    }
    update();
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      resetView();
    }
  }

  void wheelEvent(QWheelEvent *event) override {
    const double steps = event->angleDelta().y() / 120.0;
    zoom_ = std::clamp(zoom_ * std::pow(1.15, steps), 0.3, 6.0);
    update();
    event->accept();
  }

  void leaveEvent(QEvent *) override {
    hoveredIndex_ = -1;
    update();
  }

private:
  struct DrawnBin {
    QPointF position;
    double radius = 0.0;
    double depth = 0.0;
    int index = 0;
  };

  // 立方体の一辺を画面で何画素にするか。
  double cubeScale() const { return 0.62 * std::min(width(), height()) * zoom_; }

  QPointF toScreen(double red, double green, double blue) const {
    const auto point = projectHistogram3DPoint(red, green, blue, yaw_, pitch_);
    const double scale = cubeScale();
    return {width() / 2.0 + point.x * scale, height() / 2.0 + point.y * scale};
  }

  void paintCube(QPainter *painter) const {
    // 12 本の辺。原点から出る 3 本は、軸の色で太く描いて、向きが分かるようにする。
    painter->setPen(QPen(palette_.frame, 1));
    for (int axis = 0; axis < 3; ++axis) {
      for (int corner = 0; corner < 4; ++corner) {
        // 軸に直交する 2 つの成分の組み合わせ (4 通り) について、軸に沿った辺を引く。
        const double a = corner & 1 ? 1.0 : 0.0;
        const double b = corner & 2 ? 1.0 : 0.0;
        double from[3];
        double to[3];
        const int other1 = (axis + 1) % 3;
        const int other2 = (axis + 2) % 3;
        from[axis] = 0.0;
        to[axis] = 1.0;
        from[other1] = to[other1] = a;
        from[other2] = to[other2] = b;
        if (a == 0.0 && b == 0.0) {
          continue;  // 原点から出る辺は、軸として別に描く。
        }
        painter->drawLine(toScreen(from[0], from[1], from[2]), toScreen(to[0], to[1], to[2]));
      }
    }

    const QColor axisColors[3] = {palette_.redCurve, palette_.greenCurve, palette_.blueCurve};
    const char *axisNames[3] = {"R", "G", "B"};
    const double max = histogram_.axisMaximum;
    for (int axis = 0; axis < 3; ++axis) {
      double end[3] = {0.0, 0.0, 0.0};
      end[axis] = 1.0;
      const QPointF origin = toScreen(0.0, 0.0, 0.0);
      const QPointF tip = toScreen(end[0], end[1], end[2]);
      painter->setPen(QPen(axisColors[axis], 2));
      painter->drawLine(origin, tip);

      // 軸の先の名前と、その軸の上限の値。
      painter->setPen(axisColors[axis]);
      const QPointF direction = tip - origin;
      const double length = std::hypot(direction.x(), direction.y());
      const QPointF unit = length > 0.0 ? direction / length : QPointF(1.0, 0.0);
      const QPointF label = tip + unit * 12.0;
      painter->drawText(QRectF(label.x() - 24, label.y() - 9, 48, 18), Qt::AlignCenter,
                        QString("%1 %2").arg(axisNames[axis]).arg(max, 0, 'f', max > 10.0 ? 1 : 2));
    }
  }

  void paintBins(QPainter *painter) {
    const int bins = histogram_.binsPerAxis;
    // 隣のビンと重なりすぎない大きさを上限にする。
    const double maxRadius = std::max(1.5, cubeScale() / bins * 0.62);

    points_.reserve(histogram_.occupiedBinCount);
    for (int index = 0; index < histogram_.counts.size(); ++index) {
      const int count = histogram_.counts.at(index);
      if (count <= 0) {
        continue;
      }
      int redBin = 0;
      int greenBin = 0;
      int blueBin = 0;
      histogram3DSplit(index, bins, &redBin, &greenBin, &blueBin);
      // ビンの中心。軸の上限に関わらず、立方体の中では 0.0 - 1.0 に並べる。
      const double red = (redBin + 0.5) / bins;
      const double green = (greenBin + 0.5) / bins;
      const double blue = (blueBin + 0.5) / bins;
      const auto projected = projectHistogram3DPoint(red, green, blue, yaw_, pitch_);
      const double scale = cubeScale();
      const double ratio = static_cast<double>(count) / histogram_.maxCount;
      points_.push_back(
          {.position = {width() / 2.0 + projected.x * scale, height() / 2.0 + projected.y * scale},
           .radius = std::max(1.0, maxRadius * pointSizeFraction(ratio, sizeMode_)),
           .depth = projected.depth,
           .index = index});
    }

    // 奥のものから描く。手前のものが奥を隠す。
    std::sort(points_.begin(), points_.end(),
              [](const DrawnBin &left, const DrawnBin &right) { return left.depth < right.depth; });

    painter->setPen(Qt::NoPen);
    for (const auto &bin : std::as_const(points_)) {
      int redBin = 0;
      int greenBin = 0;
      int blueBin = 0;
      histogram3DSplit(bin.index, bins, &redBin, &greenBin, &blueBin);
      QColor color = histogram3DBinColor(redBin, greenBin, blueBin, histogram_.axisMaximum, bins);
      color.setAlpha(215);
      painter->setBrush(color);
      painter->drawEllipse(bin.position, bin.radius, bin.radius);
    }
  }

  // 手前から順に、カーソルが半径の中に入る最初のビン。
  int binUnderCursor(const QPoint &position) const {
    for (auto it = points_.crbegin(); it != points_.crend(); ++it) {
      const double reach = it->radius + 2.0;
      const QPointF delta = it->position - QPointF(position);
      if (delta.x() * delta.x() + delta.y() * delta.y() <= reach * reach) {
        return it->index;
      }
    }
    return -1;
  }

  void paintHover(QPainter *painter) const {
    // 描き直した後で、位置から引き直す (向きを変えると、ビンの画面位置が動く)。
    int index = hoveredIndex_;
    if (index < 0) {
      return;
    }
    const DrawnBin *hovered = nullptr;
    for (const auto &bin : points_) {
      if (bin.index == index) {
        hovered = &bin;
        break;
      }
    }
    if (hovered == nullptr) {
      return;
    }

    const int bins = histogram_.binsPerAxis;
    int redBin = 0;
    int greenBin = 0;
    int blueBin = 0;
    histogram3DSplit(index, bins, &redBin, &greenBin, &blueBin);
    const int count = histogram_.counts.at(index);
    const double ratio = histogram_.pixelCount > 0
                             ? 100.0 * count / static_cast<double>(histogram_.pixelCount)
                             : 0.0;
    const double max = histogram_.axisMaximum;
    const int digits = max > 10.0 ? 2 : 3;
    const auto range = [&](int bin) {
      return QString("%1 - %2")
          .arg(histogram3DBinLow(bin, max, bins), 0, 'f', digits)
          .arg(histogram3DBinHigh(bin, max, bins), 0, 'f', digits);
    };
    QStringList lines;
    lines << QCoreApplication::translate("Histogram3DWindow", "R %1").arg(range(redBin))
          << QCoreApplication::translate("Histogram3DWindow", "G %1").arg(range(greenBin))
          << QCoreApplication::translate("Histogram3DWindow", "B %1").arg(range(blueBin))
          << QCoreApplication::translate("Histogram3DWindow", "%1 px (%2%)")
                 .arg(count)
                 .arg(ratio, 0, 'f', 2);

    painter->save();
    painter->setPen(QPen(palette_.titleText, 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(hovered->position, hovered->radius + 2.0, hovered->radius + 2.0);

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
    box.moveTopLeft(hoverPosition_ + QPoint(16, 16));
    if (box.right() > width() - 4) {
      box.moveRight(hoverPosition_.x() - 16);
    }
    if (box.bottom() > height() - 4) {
      box.moveBottom(hoverPosition_.y() - 16);
    }
    box.moveLeft(std::max(4, box.left()));
    box.moveTop(std::max(4, box.top()));

    QColor fill = palette_.background;
    fill.setAlpha(238);
    painter->setPen(palette_.frame);
    painter->setBrush(fill);
    painter->drawRoundedRect(box, 4, 4);
    painter->setPen(palette_.titleText);
    for (int line = 0; line < lines.size(); ++line) {
      painter->drawText(
          QRect(box.left() + 8, box.top() + 6 + line * lineHeight, box.width() - 16, lineHeight),
          Qt::AlignLeft | Qt::AlignVCenter, lines.at(line));
    }
    painter->restore();
  }

  Histogram3D histogram_;
  PointSizeMode sizeMode_ = PointSizeMode::SquareRoot;
  double yaw_ = kDefaultYaw;
  double pitch_ = kDefaultPitch;
  double zoom_ = 1.0;
  bool dragging_ = false;
  QPoint lastDragPosition_;
  QPoint hoverPosition_;
  int hoveredIndex_ = -1;
  AnalysisChartPalette palette_;
  // 最後に描いたビン (奥から順)。カーソルの位置から、どのビンかを引くのに使う。
  mutable QVector<DrawnBin> points_;
};

Histogram3DWindow::Histogram3DWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("Histogram3DWindow", "Histogram 3D"), parent) {
  resize(760, 640);

  auto *rootLayout = new QVBoxLayout(this);
  auto *controlLayout = new QHBoxLayout();

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("Histogram3DWindow", "Cell"), this));
  cellComboBox_ = new QComboBox(this);
  cellComboBox_->setToolTip(QCoreApplication::translate(
      "Histogram3DWindow", "The cell to show. Only one cell is drawn."));
  controlLayout->addWidget(cellComboBox_);

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("Histogram3DWindow", "Bins per axis"), this));
  binsSpinBox_ = new QSpinBox(this);
  binsSpinBox_->setRange(kMinHistogram3DBins, kMaxHistogram3DBins);
  binsSpinBox_->setValue(16);
  binsSpinBox_->setKeyboardTracking(false);
  binsSpinBox_->setToolTip(QCoreApplication::translate(
      "Histogram3DWindow", "Splits each of R, G and B into this many bins. More bins show finer "
                           "colors but take longer to draw."));
  controlLayout->addWidget(binsSpinBox_);

  controlLayout->addWidget(
      new QLabel(QCoreApplication::translate("Histogram3DWindow", "Point size"), this));
  sizeComboBox_ = new QComboBox(this);
  sizeComboBox_->addItem(QCoreApplication::translate("Histogram3DWindow", "Square root"),
                         static_cast<int>(PointSizeMode::SquareRoot));
  sizeComboBox_->addItem(QCoreApplication::translate("Histogram3DWindow", "Logarithmic"),
                         static_cast<int>(PointSizeMode::Logarithmic));
  sizeComboBox_->addItem(QCoreApplication::translate("Histogram3DWindow", "Linear"),
                         static_cast<int>(PointSizeMode::Linear));
  sizeComboBox_->setToolTip(QCoreApplication::translate(
      "Histogram3DWindow", "How the number of pixels in a bin sets the size of its point. "
                           "Linear lets the biggest bin hide the rest."));
  controlLayout->addWidget(sizeComboBox_);

  auto *resetViewButton =
      new QPushButton(QCoreApplication::translate("Histogram3DWindow", "Reset view"), this);
  resetViewButton->setToolTip(QCoreApplication::translate(
      "Histogram3DWindow", "Drag to rotate, wheel to zoom, double click to reset."));
  controlLayout->addWidget(resetViewButton);
  controlLayout->addStretch(1);
  rootLayout->addLayout(controlLayout);

  view_ = new Histogram3DView(this);
  rootLayout->addWidget(view_, 1);

  summaryLabel_ = new QLabel(this);
  rootLayout->addWidget(summaryLabel_);

  connect(cellComboBox_, &QComboBox::currentIndexChanged, this, [this]() { rebuild(); });
  connect(binsSpinBox_, &QSpinBox::valueChanged, this, [this]() { rebuild(); });
  connect(sizeComboBox_, &QComboBox::currentIndexChanged, this, [this]() {
    view_->setPointSizeMode(static_cast<PointSizeMode>(sizeComboBox_->currentData().toInt()));
  });
  connect(resetViewButton, &QPushButton::clicked, this, [this]() { view_->resetView(); });
}

void Histogram3DWindow::analysisImagesChanged() {
  // 選んでいたセルを覚えておき、並びが変わっても同じ番号を選び直す。
  const int previous = cellComboBox_->currentIndex();
  {
    const QSignalBlocker blocker(cellComboBox_);
    cellComboBox_->clear();
    const auto &images = analysisImages();
    for (int index = 0; index < images.size(); ++index) {
      cellComboBox_->addItem(QCoreApplication::translate("Histogram3DWindow", "Cell %1 - %2")
                                 .arg(index + 1)
                                 .arg(images.at(index).label),
                             index);
    }
    if (!images.isEmpty()) {
      cellComboBox_->setCurrentIndex(std::clamp(previous, 0, static_cast<int>(images.size()) - 1));
    }
  }
  rebuild();
}

void Histogram3DWindow::rebuild() {
  const auto &images = analysisImages();
  const int cell = cellComboBox_->currentIndex();
  if (cell < 0 || cell >= images.size() || images.at(cell).image.isNull()) {
    view_->setHistogram({});
    summaryLabel_->setText(QCoreApplication::translate("Histogram3DWindow", "No analyzable image"));
    return;
  }

  const AnalysisImage &analysisImage = images.at(cell);
  // 軸の上限は、その画像の実データの最大 (整数画像は 1.0)。
  const double axisMaximum = histogramSampleMaximum(analysisImage);
  Histogram3D histogram = buildHistogram3D(analysisImage, binsSpinBox_->value(), axisMaximum);
  summaryLabel_->setText(
      QCoreApplication::translate("Histogram3DWindow",
                                  "%1%2  %3 px  %4 / %5 bins occupied  axis 0 - %6")
          .arg(histogram.label,
               histogram.selectionApplied
                   ? QCoreApplication::translate("Histogram3DWindow", " [selection]")
                   : "")
          .arg(histogram.pixelCount)
          .arg(histogram.occupiedBinCount)
          .arg(static_cast<qint64>(histogram.binsPerAxis) * histogram.binsPerAxis *
               histogram.binsPerAxis)
          .arg(histogram.axisMaximum, 0, 'f', histogram.axisMaximum > 10.0 ? 1 : 2));
  view_->setHistogram(std::move(histogram));
}

}  // namespace heiran::ui
