#include "heiran/ui/ImageInfoText.hpp"
#include <QCoreApplication>

namespace heiran::ui {

QString formatFileSizeText(qint64 bytes) {
  if (bytes < 0) {
    return {};
  }
  constexpr double kKilo = 1024.0;
  if (bytes < 1024) {
    return QCoreApplication::translate("ImageInfoText", "%1 B").arg(bytes);
  }
  double value = static_cast<double>(bytes) / kKilo;
  const char *unit = "KB";
  if (value >= kKilo) {
    value /= kKilo;
    unit = "MB";
    if (value >= kKilo) {
      value /= kKilo;
      unit = "GB";
    }
  }
  return QString("%1 %2").arg(value, 0, 'f', 1).arg(unit);
}

QString pixelFormatText(const heiran::infra::DecodedImage &decodedImage) {
  using heiran::infra::PixelColorModel;
  using heiran::infra::PixelSampleType;

  QString bits;
  switch (decodedImage.sourceSampleType) {
    case PixelSampleType::UInt8:
      bits = "8";
      break;
    case PixelSampleType::UInt16:
      bits = "16";
      break;
    case PixelSampleType::Float16:
      bits = "16F";
      break;
    case PixelSampleType::Float32:
      bits = "32F";
      break;
    case PixelSampleType::Unknown:
    default:
      return {};
  }

  PixelColorModel model = decodedImage.sourceColorModel;
  // Qt 経由の読み込みは、グレースケールでも RGB として記録する。表示用の画像が
  // グレースケールの形式なら、グレーと言い直す。
  const QImage::Format format = decodedImage.displayImage.format();
  if (format == QImage::Format_Grayscale8 || format == QImage::Format_Grayscale16) {
    model = PixelColorModel::Gray;
  }

  switch (model) {
    case PixelColorModel::Gray:
      return QCoreApplication::translate("ImageInfoText", "Gray") + bits;
    case PixelColorModel::GrayAlpha:
      return QCoreApplication::translate("ImageInfoText", "GrayA") + bits;
    case PixelColorModel::RGB:
      return "RGB" + bits;
    case PixelColorModel::RGBA:
      return "RGBA" + bits;
    case PixelColorModel::Unknown:
    default:
      return {};
  }
}

QString formatImageInfoText(const QString &format, const QSize &size, qint64 fileBytes) {
  if (size.isEmpty()) {
    return {};
  }
  QString text;
  if (!format.isEmpty()) {
    text = format + QString(" · ");
  }
  text += QString("%1×%2").arg(size.width()).arg(size.height());
  const QString fileSize = formatFileSizeText(fileBytes);
  if (!fileSize.isEmpty()) {
    text += QString(" · %1").arg(fileSize);
  }
  return text;
}

}  // namespace heiran::ui
