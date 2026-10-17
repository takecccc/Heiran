#pragma once

#include <QByteArray>
#include <QColorSpace>
#include <QImage>
#include <QString>

namespace heiran::infra {

// AVIF の復号結果。画素と、色の情報。
struct AvifResult {
  bool ok = false;
  QString errorMessage;
  // 8bit は RGBA8888 (アルファが無ければ RGBX8888)、それを超える精度は RGBA64 (RGBX64)。
  // 向きの指定 (irot / imir) は適用済み。
  QImage image;
  // ファイルの精度 (ビット)。
  int bitDepth = 8;
  bool hasAlpha = false;
  // 埋め込みの ICC プロファイル。無ければ空。
  QByteArray iccProfile;
  // ICC が無い場合に、CICP (nclx) が申告している色空間。申告が無い、または対応しない値なら無効。
  QColorSpace declaredColorSpace;
  // ファイルの画像の数。2 以上ならアニメーション (先頭だけを読む)。
  int imageCount = 1;
};

// AVIF を読む。AV1 の復号は dav1d。HEIC は読まない (HEVC は、特許の扱いが重いため対象外)。
//
// 向きは、EXIF ではなく AVIF の変換指定 (回転と反転) に従い、ここで適用する。
// アニメーション AVIF は、先頭の 1 コマだけを返す。
AvifResult decodeAvif(const QString &filePath);

// CICP の値 (色の原色、伝達特性) から、Qt の色空間を求める。対応しない組み合わせは無効。
QColorSpace colorSpaceFromCicp(int colorPrimaries, int transferCharacteristics);

}  // namespace heiran::infra
