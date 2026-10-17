#pragma once

#include <QByteArray>

namespace heiran::infra {

// CMYK の画素を、sRGB の RGB へ変換する。PSD と TIFF の CMYK が使う。
//
// `cmyk` は、画素ごとに C M Y K の順に詰めた標本 (すき間なし)。標本は 1 バイト、または 2 バイト
// (ホストのバイト順)。画素に C M Y K の後ろの標本 (アルファなど) があれば、`sourceChannels` へ
// 4 より大きい数を渡す。後ろの標本は、読み飛ばす。`inkReversed` が真なら、255 (65535) がインク無し
// (Photoshop の持ち方)。 偽なら、0 がインク無し (TIFF の持ち方)。 `rgb` へ、画素ごとに R G B
// の順に、同じ標本の大きさで書く (`width * height * 3` 標本)。
//
// `iccProfile` が CMYK 用の有効なプロファイルなら、Little CMS で色を管理して変換する
// (相対的な色度を保ち、黒点を補正する。Photoshop の既定と同じ)。無い、読めない、CMYK 用でない、
// または Little CMS が使えないときは、プロファイルを使わず、インクの量から素朴に求める
// (R = (1 - C) * (1 - K))。返り値は、プロファイルを使えたら真。
bool convertCmykToSrgb(const QByteArray &iccProfile, const uchar *cmyk, int width, int height,
                       int sampleBytes, int sourceChannels, bool inkReversed, uchar *rgb);

}  // namespace heiran::infra
