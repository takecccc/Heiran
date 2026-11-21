#include "heiran/domain/DiffSettings.hpp"

namespace heiran::domain {

QString toDiffModeString(DiffMode mode) {
  switch (mode) {
    case DiffMode::Signed:
      return "signed";
    case DiffMode::Binary:
      return "binary";
    case DiffMode::DeltaE:
      return "delta-e";
    case DiffMode::Ssim:
      return "ssim";
    case DiffMode::Wipe:
      return "wipe";
    case DiffMode::RgbMax:
      return "rgb-max";
    case DiffMode::Absolute:
      break;
  }
  return "absolute";
}

DiffMode diffModeFromString(const QString &text) {
  if (text == "signed") {
    return DiffMode::Signed;
  }
  if (text == "binary") {
    return DiffMode::Binary;
  }
  if (text == "delta-e") {
    return DiffMode::DeltaE;
  }
  if (text == "ssim") {
    return DiffMode::Ssim;
  }
  if (text == "wipe") {
    return DiffMode::Wipe;
  }
  if (text == "rgb-max") {
    return DiffMode::RgbMax;
  }
  return DiffMode::Absolute;
}

QString toDiffViewModeString(DiffViewMode view) {
  switch (view) {
    case DiffViewMode::SignedColor:
      return "signed-color";
    case DiffViewMode::Overlay:
      return "overlay";
    case DiffViewMode::Grayscale:
      break;
  }
  return "grayscale";
}

DiffViewMode diffViewModeFromString(const QString &text) {
  if (text == "signed-color") {
    return DiffViewMode::SignedColor;
  }
  if (text == "overlay") {
    return DiffViewMode::Overlay;
  }
  return DiffViewMode::Grayscale;
}

QString toWipeOrientationString(WipeOrientation orientation) {
  return orientation == WipeOrientation::TopBottom ? "top-bottom" : "left-right";
}

WipeOrientation wipeOrientationFromString(const QString &text) {
  return text == "top-bottom" ? WipeOrientation::TopBottom : WipeOrientation::LeftRight;
}

QString toDiffSizeMatchString(DiffSizeMatch match) {
  switch (match) {
    case DiffSizeMatch::ToBase:
      return "base";
    case DiffSizeMatch::ToTarget:
      return "target";
    case DiffSizeMatch::ToLarger:
      return "larger";
    case DiffSizeMatch::Overlap:
      break;
  }
  return "overlap";
}

DiffSizeMatch diffSizeMatchFromString(const QString &text) {
  if (text == "base") {
    return DiffSizeMatch::ToBase;
  }
  if (text == "target") {
    return DiffSizeMatch::ToTarget;
  }
  if (text == "larger") {
    return DiffSizeMatch::ToLarger;
  }
  // "off" は、以前の名前。
  return DiffSizeMatch::Overlap;
}

}  // namespace heiran::domain
