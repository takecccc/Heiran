#include "heiran/ui/ImageInfo.hpp"

#include "heiran/ui/ImageInfoText.hpp"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <algorithm>

namespace heiran::ui {

namespace {

constexpr const char *kMissingValue = "-";

QString primariesName(QColorSpace::Primaries primaries) {
  switch (primaries) {
    case QColorSpace::Primaries::SRgb:
      return QCoreApplication::translate("ImageInfo", "sRGB");
    case QColorSpace::Primaries::AdobeRgb:
      return QCoreApplication::translate("ImageInfo", "Adobe RGB");
    case QColorSpace::Primaries::DciP3D65:
      return QCoreApplication::translate("ImageInfo", "Display P3");
    case QColorSpace::Primaries::ProPhotoRgb:
      return QCoreApplication::translate("ImageInfo", "ProPhoto RGB");
    case QColorSpace::Primaries::Bt2020:
      return "BT.2020";
    case QColorSpace::Primaries::Custom:
    default:
      return QCoreApplication::translate("ImageInfo", "custom primaries");
  }
}

QString transferName(QColorSpace::TransferFunction transfer, float gamma) {
  switch (transfer) {
    case QColorSpace::TransferFunction::Linear:
      return "linear";
    case QColorSpace::TransferFunction::Gamma:
      return QCoreApplication::translate("ImageInfo", "gamma %1").arg(gamma, 0, 'f', 2);
    case QColorSpace::TransferFunction::SRgb:
      return QCoreApplication::translate("ImageInfo", "sRGB curve");
    case QColorSpace::TransferFunction::ProPhotoRgb:
      return QCoreApplication::translate("ImageInfo", "ProPhoto curve");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    case QColorSpace::TransferFunction::Bt2020:
      return QCoreApplication::translate("ImageInfo", "BT.2020 curve");
    case QColorSpace::TransferFunction::Hlg:
      return "HLG";
    case QColorSpace::TransferFunction::St2084:
      return "PQ";
#endif
    case QColorSpace::TransferFunction::Custom:
    default:
      return QCoreApplication::translate("ImageInfo", "custom curve");
  }
}

}  // namespace

QString describeColorSpace(const QColorSpace &colorSpace) {
  if (!colorSpace.isValid()) {
    return {};
  }
  return QString("%1 / %2").arg(primariesName(colorSpace.primaries()),
                                transferName(colorSpace.transferFunction(), colorSpace.gamma()));
}

QVector<ImageInfoRow> buildImageInfoRows(const QVector<ImageInfoCell> &cells) {
  // (分類, 名前) の組が初めて現れた順に行を作る。
  QVector<ImageInfoRow> rows;
  QHash<QString, int> rowIndexByKey;
  const auto keyOf = [](const QString &group, const QString &key) {
    return group + QChar(0x1F) + key;
  };

  for (int cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
    for (const auto &entry : cells.at(cellIndex).entries) {
      const QString key = keyOf(entry.group, entry.key);
      auto found = rowIndexByKey.constFind(key);
      int rowIndex = 0;
      if (found == rowIndexByKey.constEnd()) {
        rowIndex = static_cast<int>(rows.size());
        rowIndexByKey.insert(key, rowIndex);
        ImageInfoRow row;
        row.group = entry.group;
        row.key = entry.key;
        row.values = QStringList(cells.size(), kMissingValue);
        rows.push_back(std::move(row));
      } else {
        rowIndex = found.value();
      }
      rows[rowIndex].values[cellIndex] = entry.value;
    }
  }

  for (auto &row : rows) {
    for (int index = 1; index < row.values.size(); ++index) {
      if (row.values.at(index) != row.values.at(0)) {
        row.differs = true;
        break;
      }
    }
  }

  // 分類ごとにまとめる。分類の順は初めて現れた順、分類の中の順も同じ。
  QStringList groupOrder;
  for (const auto &row : rows) {
    if (!groupOrder.contains(row.group)) {
      groupOrder.push_back(row.group);
    }
  }
  std::stable_sort(rows.begin(), rows.end(),
                   [&groupOrder](const ImageInfoRow &a, const ImageInfoRow &b) {
                     return groupOrder.indexOf(a.group) < groupOrder.indexOf(b.group);
                   });
  return rows;
}

ImageInfoCell describeImage(const QString &label, const QString &filePath,
                            const heiran::infra::DecodedImage &decodedImage,
                            const QVector<heiran::infra::MetadataEntry> &fileMetadata) {
  using heiran::infra::MetadataEntry;

  ImageInfoCell cell;
  cell.label = label;
  const auto add = [&cell](const QString &group, const QString &key, const QString &value) {
    if (!value.isEmpty()) {
      cell.entries.push_back({group, key, value});
    }
  };

  const QFileInfo fileInfo(filePath);
  add(QCoreApplication::translate("ImageInfo", "File"),
      QCoreApplication::translate("ImageInfo", "Name"), fileInfo.fileName());
  add(QCoreApplication::translate("ImageInfo", "File"),
      QCoreApplication::translate("ImageInfo", "Folder"),
      QDir::toNativeSeparators(fileInfo.absolutePath()));
  if (fileInfo.exists()) {
    add(QCoreApplication::translate("ImageInfo", "File"),
        QCoreApplication::translate("ImageInfo", "Size"),
        QCoreApplication::translate("ImageInfo", "%1 (%2 bytes)")
            .arg(formatFileSizeText(fileInfo.size()),
                 QLocale(QLocale::English).toString(fileInfo.size())));
    add(QCoreApplication::translate("ImageInfo", "File"),
        QCoreApplication::translate("ImageInfo", "Modified"),
        fileInfo.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
  }

  const QSize size = decodedImage.size();
  if (!size.isEmpty()) {
    add(QCoreApplication::translate("ImageInfo", "Image"),
        QCoreApplication::translate("ImageInfo", "Dimensions"),
        QString("%1 × %2").arg(size.width()).arg(size.height()));
  }
  add(QCoreApplication::translate("ImageInfo", "Image"),
      QCoreApplication::translate("ImageInfo", "Pixel format"), pixelFormatText(decodedImage));
  add(QCoreApplication::translate("ImageInfo", "Image"),
      QCoreApplication::translate("ImageInfo", "Decoder"), decodedImage.decoderBackend);
  switch (decodedImage.subImageKind) {
    case heiran::infra::SubImageKind::Frames:
      add(QCoreApplication::translate("ImageInfo", "Image"),
          QCoreApplication::translate("ImageInfo", "Frames"),
          QString::number(decodedImage.subImageCount));
      break;
    case heiran::infra::SubImageKind::Pages:
      add(QCoreApplication::translate("ImageInfo", "Image"),
          QCoreApplication::translate("ImageInfo", "Pages"),
          QString::number(decodedImage.subImageCount));
      break;
    case heiran::infra::SubImageKind::None:
    default:
      break;
  }
  if (decodedImage.appliedExifOrientation != heiran::infra::ExifOrientation::Normal) {
    add(QCoreApplication::translate("ImageInfo", "Image"),
        QCoreApplication::translate("ImageInfo", "Orientation applied"),
        QString::number(static_cast<int>(decodedImage.appliedExifOrientation)));
  }

  add(QCoreApplication::translate("ImageInfo", "Color"),
      QCoreApplication::translate("ImageInfo", "Color space"),
      describeColorSpace(decodedImage.colorSpace));
  const auto &profile = decodedImage.embeddedColorProfile;
  if (!profile.iccProfile.isEmpty()) {
    add(QCoreApplication::translate("ImageInfo", "Color"),
        QCoreApplication::translate("ImageInfo", "Embedded profile"),
        QCoreApplication::translate("ImageInfo", "ICC, %1 bytes").arg(profile.iccProfile.size()));
  } else if (profile.declaredColorSpace.isValid()) {
    add(QCoreApplication::translate("ImageInfo", "Color"),
        QCoreApplication::translate("ImageInfo", "Embedded profile"),
        QCoreApplication::translate("ImageInfo", "declared (%1)")
            .arg(describeColorSpace(profile.declaredColorSpace)));
  } else {
    add(QCoreApplication::translate("ImageInfo", "Color"),
        QCoreApplication::translate("ImageInfo", "Embedded profile"), "none");
  }

  for (const auto &entry : fileMetadata) {
    cell.entries.push_back(entry);
  }
  return cell;
}

}  // namespace heiran::ui
