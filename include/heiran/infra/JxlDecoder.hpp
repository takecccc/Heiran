#pragma once

#include <QByteArray>
#include <QColorSpace>
#include <QImage>
#include <QString>

namespace heiran::infra {

// JPEG XL の復号結果。画素と、色の情報。
struct JxlResult {
  bool ok = false;
  QString errorMessage;
  // 8bit は RGBA8888 (アルファが無ければ RGBX8888)、それを超える精度は RGBA64 (RGBX64)。
  // 向きの指定は適用済み。
  QImage image;
  // ファイルの精度 (ビット)。
  int bitDepth = 8;
  bool hasAlpha = false;
  // 埋め込みの ICC プロファイル。色空間が enum で指定されている場合は空。
  QByteArray iccProfile;
  // enum で指定された色空間 (sRGB、Display P3、BT.2100 など)。ICC
  // の場合、または対応しない値なら無効。
  QColorSpace declaredColorSpace;
  // アニメーションか。先頭の 1 コマだけを返す。
  bool isAnimation = false;
  // 浮動小数で符号化されていたか。1.0 を超える値は、16bit の整数へ収めるため失われる。
  bool wasFloatingPoint = false;
};

// JPEG XL を読む。
//
// 浮動小数の画像 (線形の HDR など) は、16bit の整数へ収めて読む。1.0 を超える値は 1.0 で切れる。
// アニメーションは、先頭の 1 コマだけ。
JxlResult decodeJxl(const QString &filePath);

}  // namespace heiran::infra
