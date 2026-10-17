#include "heiran/ui/ThumbnailLoader.hpp"

#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/ColorManagement.hpp"
#include "heiran/ui/DisplayTransform.hpp"

#include <QColorSpace>
#include <QImageReader>

namespace heiran::ui {

namespace {

// `maximumSize` に収まる最大の大きさ。縦横比は保ち、元より大きくはしない。
QSize fittedSize(const QSize &sourceSize, const QSize &maximumSize) {
  if (sourceSize.isEmpty() || maximumSize.isEmpty()) {
    return {};
  }
  if (sourceSize.width() <= maximumSize.width() && sourceSize.height() <= maximumSize.height()) {
    return sourceSize;
  }
  return sourceSize.scaled(maximumSize, Qt::KeepAspectRatio);
}

// 読み込み時の間引きが効く形式かどうか。
//
// JPEG は DCT の段階で 1/2, 1/4, 1/8 に間引ける。それ以外は全画素を読んでから
// 縮小するだけになり、専用のデコーダを通したほうが速い。
bool supportsScaledRead(const QByteArray &format) {
  const QByteArray normalized = format.toLower();
  return normalized == "jpeg" || normalized == "jpg";
}

// 復号結果を表示先の色空間へ写してから縮める。
//
// 線形で持つ EXR をそのまま縮めると暗く出る。一覧では露出を触れないため、
// ここで整えておかないと中身が分からない。
QImage toDisplayImage(const heiran::infra::DecodedImage &decodedImage) {
  const QColorSpace displayColorSpace(QColorSpace::SRgb);
  const QColorSpace &sourceColorSpace = decodedImage.colorSpace;
  const bool needsConversion = sourceColorSpace.isValid() && sourceColorSpace != displayColorSpace;
  if (!needsConversion) {
    return decodedImage.displayImage;
  }

  DisplayTransformParams params;
  params.colorManaged = true;
  params.colorTransform = makeColorTransformParams(sourceColorSpace, displayColorSpace);
  if (!isShaderRepresentable(sourceColorSpace)) {
    params.colorLut = buildColorLut(sourceColorSpace, displayColorSpace);
    if (!params.colorLut) {
      return decodedImage.displayImage;
    }
  }
  const QImage converted = renderDisplayImageOnCpu(decodedImage, params);
  return converted.isNull() ? decodedImage.displayImage : converted;
}

}  // namespace

QImage loadThumbnail(const QString &filePath, const QSize &maximumSize, bool honorExifOrientation) {
  if (filePath.isEmpty() || maximumSize.isEmpty()) {
    return {};
  }

  QImageReader reader(filePath);
  if (supportsScaledRead(reader.format())) {
    const QSize target = fittedSize(reader.size(), maximumSize);
    if (!target.isEmpty()) {
      reader.setAutoTransform(honorExifOrientation);
      reader.setScaledSize(target);
      const QImage scaled = reader.read();
      if (!scaled.isNull()) {
        return scaled;
      }
    }
    // 読めなかった場合は下の経路へ落ちる。壊れた EXIF などで失敗することがある。
  }

  const heiran::infra::ImageDecoder decoder;
  // 動きは要らない。アニメーションは先頭のフレームだけを読む。
  const auto result = decoder.decode(filePath, honorExifOrientation, 0, true);
  if (!result.ok) {
    return {};
  }

  const QImage displayImage = toDisplayImage(result.decodedImage);
  const QSize target = fittedSize(displayImage.size(), maximumSize);
  if (target.isEmpty()) {
    return {};
  }
  if (target == displayImage.size()) {
    return displayImage;
  }
  return displayImage.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

}  // namespace heiran::ui
