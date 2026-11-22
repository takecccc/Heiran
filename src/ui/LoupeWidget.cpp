#include "heiran/ui/LoupeWidget.hpp"

#include "heiran/ui/PixelInfoText.hpp"

#include <QCoreApplication>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

LoupeWidget::LoupeWidget(QWidget *parent) : QWidget(parent) {
  setMinimumSize(300, 300);
}

void LoupeWidget::setPixelValueFormat(PixelValueFormat pixelValueFormat) {
  pixelValueFormat_ = pixelValueFormat;
  update();
}

void LoupeWidget::setPreview(const LoupePreview &preview) {
  preview_ = preview;
  update();
}

void LoupeWidget::setTransparencyBackground(const TransparencyBackground &background) {
  transparencyBackground_ = background;
  update();
}

void LoupeWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(24, 26, 30));

  if (!preview_.valid || preview_.image.isNull()) {
    painter.setPen(QColor(220, 223, 228));
    painter.drawText(rect(), Qt::AlignCenter,
                     QCoreApplication::translate("LoupeWidget", "No preview"));
    return;
  }

  constexpr int kSampleRadius = 3;
  constexpr int kGridSize = kSampleRadius * 2 + 1;
  const int cellSize = std::max(16, std::min(width(), height()) / kGridSize);
  const int gridPixelSize = cellSize * kGridSize;
  const int offsetX = (width() - gridPixelSize) / 2;
  const int offsetY = (height() - gridPixelSize) / 2;
  const int centerX = static_cast<int>(std::floor(preview_.imagePosition.x()));
  const int centerY = static_cast<int>(std::floor(preview_.imagePosition.y()));

  QFont font = painter.font();
  font.setFamilies({"Consolas", "Courier New", "monospace"});
  font.setPointSize(7);
  painter.setFont(font);
  const QFontMetrics fontMetrics(font);
  const int lineStep = std::max(8, static_cast<int>(std::round(fontMetrics.height() * 0.72))) + 1;

  const PixelValueFormat valueFormat = pixelValueFormat_;
  const bool eightBit =
      preview_.sampler.valueSpace == AnalysisValueSpace::Source &&
      preview_.sampler.decodedImage.sourceSampleType == heiran::infra::PixelSampleType::UInt8;
  auto formatValue = [valueFormat, eightBit](float value) {
    return formatPixelSample(value, valueFormat, eightBit).trimmed();
  };
  auto formatChannelValue = [&formatValue](const QString &label, float value) {
    return QString("%1 %2").arg(label, -2).arg(formatValue(value));
  };
  // 塗りは表示用画像から、数値は解析用バッファから取る。
  const bool grayscaleSource = preview_.sampler.isGrayscale(preview_.image);
  const bool alphaSource = preview_.sampler.hasAlpha(preview_.image);
  painter.fillRect(QRect(offsetX, offsetY, gridPixelSize, gridPixelSize),
                   transparencyBackgroundBrush(transparencyBackground_));

  for (int gridY = 0; gridY < kGridSize; ++gridY) {
    for (int gridX = 0; gridX < kGridSize; ++gridX) {
      const int sampleX = centerX + gridX - kSampleRadius;
      const int sampleY = centerY + gridY - kSampleRadius;
      const QRect cellRect(offsetX + gridX * cellSize, offsetY + gridY * cellSize, cellSize,
                           cellSize);

      if (sampleX >= 0 && sampleY >= 0 && sampleX < preview_.image.width() &&
          sampleY < preview_.image.height()) {
        const QColor pixelColor = preview_.image.pixelColor(sampleX, sampleY);
        painter.fillRect(cellRect, pixelColor);

        const auto sampleAt = [this, sampleX, sampleY](int rgbaChannel) {
          return preview_.sampler.sampleRgbaAtDisplayPixel(sampleX, sampleY, rgbaChannel);
        };

        // チャネルの選び方は画素値表示と同じにする。ルーペは狭いため、
        // ラベルと値を縦に積む点だけが違う。
        const auto values = selectPixelChannelValues(
            preview_.channelMode, grayscaleSource, alphaSource, sampleAt,
            [this, sampleX, sampleY]() {
              return static_cast<float>(preview_.sampler.lumaAtDisplayPixel(sampleX, sampleY) /
                                        255.0);
            });
        QStringList valueLines;
        for (const auto &value : values) {
          valueLines.push_back(values.size() == 1
                                   ? QString("%1\n%2").arg(value.label, formatValue(value.value))
                                   : formatChannelValue(value.label, value.value));
        }
        const QString valueText = valueLines.join('\n');

        const QColor background = transparencyBackground_.colorAt(cellRect.center());
        const double alpha = pixelColor.alphaF();
        const double brightness =
            ((pixelColor.red() * alpha + background.red() * (1.0 - alpha)) * 299 +
             (pixelColor.green() * alpha + background.green() * (1.0 - alpha)) * 587 +
             (pixelColor.blue() * alpha + background.blue() * (1.0 - alpha)) * 114) /
            1000;
        painter.setPen(brightness < 128 ? QColor(245, 245, 245) : QColor(20, 20, 20));
        const QStringList lines = valueText.split('\n');
        const int totalTextHeight = lineStep * static_cast<int>(lines.size()) - 1;
        int y = cellRect.center().y() - totalTextHeight / 2 + lineStep - 2;
        for (const auto &line : lines) {
          painter.drawText(QRect(cellRect.left() + 2, y - lineStep, cellRect.width() - 4, lineStep),
                           Qt::AlignHCenter | Qt::AlignVCenter, line);
          y += lineStep;
        }
      } else {
        painter.fillRect(cellRect, QColor(48, 52, 58));
      }

      painter.setPen(QColor(70, 74, 80));
      painter.drawRect(cellRect);
    }
  }

  painter.setPen(QPen(QColor(255, 255, 255), 2));
  painter.drawRect(offsetX + kSampleRadius * cellSize, offsetY + kSampleRadius * cellSize, cellSize,
                   cellSize);
}

}  // namespace heiran::ui
