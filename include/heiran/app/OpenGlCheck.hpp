#pragma once

#include <QString>

namespace heiran::app {

// Heiran は、画像の描画に OpenGL を使う (3.0 以上。16bit と浮動小数のテクスチャのため)。配布物は、ソフトウェアの OpenGL (opengl32sw.dll) を
// 持たないため、GPU とドライバが、OpenGL に対応している必要がある。対応していない環境で、何も出ない窓だけが開くのを避けるため、
// 起動の前に確かめて、理由を知らせる。
//
// 使えるなら、空。使えない (コンテキストを作れない、または、バージョンが低い) ときは、利用者へ見せる説明を返す。
QString checkOpenGlSupport();

}  // namespace heiran::app
