#pragma once

#include "heiran/infra/ImageMetadata.hpp"
#include "heiran/infra/RawImage.hpp"

#include <QColorSpace>
#include <QString>

namespace heiran::infra {

// OpenEXR を直接使う EXR の復号結果。
struct ExrResult {
  RawImage raw;
  // ファイルが持つ画像の数 (パート)。複数なら、ページとして扱う。
  int partCount = 1;
  // `chromaticities` 属性が申告する色空間 (線形)。無ければ無効。
  QColorSpace declaredColorSpace;
};

// EXR の `partIndex` 番目のパートを、表示用の RGBA (または、グレー、グレー + アルファ) として読む。
//
// - 画像はデータウィンドウの範囲。表示ウィンドウとの食い違いは扱わない。
// - 精度は、すべてのチャンネルが half なら half (16bit 浮動小数)、それ以外は 32bit 浮動小数。
//   32bit 整数のチャンネルは float へ寄せる。
// - チャンネルは、R / G / B (+ A)、なければ Y (+ A)、なければ `層名.R` などの層、なければ先頭の
//   4 つまで。色差を間引いた形式 (Y / RY / BY) と、deep の画像は対応しない (失敗を返す)。
// - 圧縮は OpenEXR が扱えるもの全部。スキャンラインとタイルの両方。タイルのミップマップは先頭の段。
//
// 失敗しても例外は出さない (`ok` が偽)。
ExrResult decodeExr(const QString &filePath, int partIndex);

// EXR の属性 (ヘッダー) を読む。画素は復号しない。先頭のパートの属性と、チャンネルの一覧、
// パートの数 (2 つ以上のとき)。分類は `EXR`。EXR でなければ、読めなければ、空を返す。
QVector<MetadataEntry> readExrMetadata(const QString &filePath);

// ファイルの先頭 (マジックナンバー) が EXR か。
bool looksLikeExr(const QByteArray &firstBytes);

}  // namespace heiran::infra
