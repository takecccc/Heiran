#pragma once

#include <QByteArray>
#include <QString>

namespace heiran::infra {

// EXIF の orientation。1 が無変換で、2 から 8 は反転と回転の組み合わせを表す。
// 値の意味は TIFF/EXIF の仕様に従う。
enum class ExifOrientation {
  Normal = 1,
  FlipHorizontal = 2,
  Rotate180 = 3,
  FlipVertical = 4,
  Transpose = 5,
  Rotate90 = 6,
  Transverse = 7,
  Rotate270 = 8,
};

// ファイルから orientation を読む。無い場合や読めない場合は `Normal` を返す。
//
// Qt は復号時に orientation を適用するが、適用した値を教えて
// くれない。`QImageReader::transformation()` は Qt 自身が書いた画像でも常に 0 を
// 返す。画素値表示を元ファイルの座標で出すには適用された値が要るため、自前で読む。
//
// JPEG は APP1 の Exif ブロック、TIFF はファイル先頭の IFD を見る。どちらも
// 中身は TIFF の構造で、タグ 0x0112 が orientation にあたる。
ExifOrientation readExifOrientation(const QString &filePath);

// バイト列から読む。テストで組み立てた画像を渡すために公開する。
ExifOrientation readExifOrientation(const QByteArray &fileContents);

// 回転の 90 度単位と反転の組み合わせへ分解する。表示座標を元ファイルの座標へ
// 戻すときに使う。回転してから反転する順序で表す。
struct ExifTransform {
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
};

ExifTransform toExifTransform(ExifOrientation orientation);

}  // namespace heiran::infra
