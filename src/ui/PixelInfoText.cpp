#include "heiran/ui/PixelInfoText.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

namespace heiran::ui {

namespace {

QString formatChannelSample(const QString &label, float value, PixelValueFormat format,
                            bool eightBit) {
  return QString("%1 %2").arg(label, -2).arg(formatPixelSample(value, format, eightBit));
}

}  // namespace

float luminanceOf(float red, float green, float blue, const heiran::domain::LumaWeights &weights) {
  return static_cast<float>(weights.red * red + weights.green * green + weights.blue * blue);
}

bool sourceIsGrayscale(const heiran::infra::DecodedImage &decodedImage, const QImage &fallback) {
  switch (decodedImage.sourceColorModel) {
    case heiran::infra::PixelColorModel::Gray:
    case heiran::infra::PixelColorModel::GrayAlpha:
      return true;
    case heiran::infra::PixelColorModel::RGB:
    case heiran::infra::PixelColorModel::RGBA:
      return false;
    default:
      return fallback.isGrayscale();
  }
}

bool sourceHasAlpha(const heiran::infra::DecodedImage &decodedImage, const QImage &fallback) {
  switch (decodedImage.sourceColorModel) {
    case heiran::infra::PixelColorModel::GrayAlpha:
    case heiran::infra::PixelColorModel::RGBA:
      return true;
    case heiran::infra::PixelColorModel::Gray:
    case heiran::infra::PixelColorModel::RGB:
      return false;
    default:
      return fallback.hasAlphaChannel();
  }
}

QVector<PixelChannelValue> selectPixelChannelValues(ChannelMode channelMode, bool grayscale,
                                                    bool hasAlpha,
                                                    const std::function<float(int)> &sample,
                                                    const std::function<float()> &luma) {
  switch (channelMode) {
    case ChannelMode::Color:
      if (grayscale) {
        return {{.label = "Gray", .value = sample(0)}};
      }
      if (hasAlpha) {
        return {{.label = "R", .value = sample(0)},
                {.label = "G", .value = sample(1)},
                {.label = "B", .value = sample(2)},
                {.label = "A", .value = sample(3)}};
      }
      return {{.label = "R", .value = sample(0)},
              {.label = "G", .value = sample(1)},
              {.label = "B", .value = sample(2)}};
    case ChannelMode::Gray:
      return {{.label = "Gray", .value = luma()}};
    case ChannelMode::Red:
      return {{.label = "Red", .value = sample(0)}};
    case ChannelMode::Green:
      return {{.label = "Green", .value = sample(1)}};
    case ChannelMode::Blue:
      return {{.label = "Blue", .value = sample(2)}};
    case ChannelMode::Alpha:
      return {{.label = "Alpha", .value = sample(3)}};
  }
  return {};
}

QString formatPixelPosition(int x, int y) {
  return QString("(%1, %2)").arg(x, 4, 10, QChar(' ')).arg(y, 4, 10, QChar(' '));
}

QString formatImageIndexLabel(int imageSetIndex) {
  return imageSetIndex >= 0 ? QString("[%1]").arg(imageSetIndex + 1, 2, 10, QChar(' '))
                            : QString("[? ]");
}

QString formatPixelChannelValues(const QVector<PixelChannelValue> &values,
                                 PixelValueFormat valueFormat, bool eightBit) {
  if (values.isEmpty()) {
    return {};
  }

  // 1 つだけのときは桁揃えが要らない。揃えると値の前に余分な空白が入る。
  if (values.size() == 1) {
    return QString("%1=%2").arg(values.first().label,
                                formatPixelSample(values.first().value, valueFormat, eightBit));
  }

  QStringList parts;
  for (const auto &value : values) {
    parts.push_back(formatChannelSample(value.label + "=", value.value, valueFormat, eightBit));
  }
  return parts.join(' ');
}

QString buildPixelStatusSummary(int x, int y, const QStringList &infoLines, int maxCells) {
  static const QRegularExpression linePattern(R"(^\[\s*(\d+)\]\s*\([^)]*\):\s*(.+)$)");
  static const QRegularExpression numberPattern(R"(=\s*(-?\d+(?:\.\d+)?))");

  QStringList cells;
  int total = 0;
  for (const QString &line : infoLines) {
    const auto lineMatch = linePattern.match(line);
    if (!lineMatch.hasMatch()) {
      continue;
    }
    ++total;
    if (cells.size() >= maxCells) {
      continue;
    }
    QStringList values;
    auto numbers = numberPattern.globalMatch(lineMatch.captured(2));
    while (numbers.hasNext()) {
      // 桁揃えの 0 (242.000) は、読みにくいため落とす。
      values.push_back(QString::number(numbers.next().captured(1).toDouble(), 'g', 6));
    }
    cells.push_back(QString("[%1] %2").arg(lineMatch.captured(1), values.isEmpty() ? QStringLiteral("-")
                                                                                     : values.join(' ')));
  }

  QString summary = QString("x=%1 y=%2").arg(x).arg(y);
  if (!cells.isEmpty()) {
    summary += "  " + cells.join("  ");
  }
  if (total > cells.size()) {
    summary += QString("  +%1").arg(total - cells.size());
  }
  return summary;
}

QString buildPixelInfoStatusLine(int imageSetIndex, const CellStatusInfo &statusInfo) {
  const QString label = formatImageIndexLabel(imageSetIndex);
  const QString headline = cellStatusHeadline(statusInfo);
  return statusInfo.reason.isEmpty()
             ? QString("%1: %2").arg(label, headline.toLower())
             : QString("%1: %2 (%3)").arg(label, headline.toLower(), statusInfo.reason);
}

QString buildPixelInfoOutOfBoundsLine(int imageSetIndex, const QPoint &position) {
  return QString("%1 %2: %3")
      .arg(formatImageIndexLabel(imageSetIndex))
      .arg(formatPixelPosition(position.x(), position.y()))
      .arg(QCoreApplication::translate("PixelInfoText", "out-of-bounds"));
}

QString buildPixelInfoValueLine(int imageSetIndex, const QPoint &reportedPixel,
                                const QVector<PixelChannelValue> &values,
                                PixelValueFormat valueFormat, bool eightBit) {
  return QString("%1 %2: %3")
      .arg(formatImageIndexLabel(imageSetIndex))
      .arg(formatPixelPosition(reportedPixel.x(), reportedPixel.y()))
      .arg(formatPixelChannelValues(values, valueFormat, eightBit));
}

}  // namespace heiran::ui
