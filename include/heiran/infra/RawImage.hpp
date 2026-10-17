#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QByteArray>
#include <QString>

namespace heiran::infra {

// 形式ごとの自前の復号器 (HDR、TGA、DPX、PSD) が返す、画素の並び。
//
// 画素はすき間なく並べる (行ごとの余白は無い)。1 画素あたり `channels` 個の値を、チャンネルの
// 順に (グレー、グレー + アルファ、RGB、RGBA) 持つ。値の型は `sampleType` で、ホストのバイト順。
// 上の行から順に並べる (向きの指定は復号器の側で適用済み)。
struct RawImage {
  bool ok = false;
  QString errorMessage;
  int width = 0;
  int height = 0;
  int channels = 0;
  PixelSampleType sampleType = PixelSampleType::Unknown;
  QByteArray pixels;
  // 埋め込みの ICC プロファイル。無ければ空。
  QByteArray iccProfile;

  qsizetype bytesPerRow() const {
    return static_cast<qsizetype>(width) * channels * bytesPerSample(sampleType);
  }
};

// 失敗の結果を作る。
inline RawImage rawImageFailure(const QString &message) {
  RawImage image;
  image.errorMessage = message;
  return image;
}

// 大きさの上限。これを超える申告は、壊れたファイルとして断る (確保で落ちないため)。
inline constexpr qint64 kMaxRawImagePixels = 1LL << 28;  // 約 2.7 億画素

}  // namespace heiran::infra
