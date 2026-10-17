#pragma once

#include <QByteArray>
#include <QColorSpace>
#include <QString>

namespace heiran::infra {

// ファイルへ埋め込まれている色情報。
//
// ICC プロファイルがあればそれが最も強い申告になる。無い場合でも、形式ごとの
// 補助的な申告 (PNG の sRGB / cHRM / gAMA など) から色空間が分かることがある。
struct EmbeddedColorProfile {
  // 埋め込み ICC プロファイルの生データ。無ければ空。
  QByteArray iccProfile;
  // ICC が無い場合に形式が申告している色空間。申告が無ければ無効。
  QColorSpace declaredColorSpace;

  bool isEmpty() const { return iccProfile.isEmpty() && !declaredColorSpace.isValid(); }

  // 使うべき色空間を返す。ICC を優先する。どちらも無ければ無効を返す。
  QColorSpace resolve() const;
};

// ファイルの先頭を読んで埋め込みの色情報を取り出す。復号は行わない。
//
// EXIF orientation と同じ理由で、backend ごとではなく 1 か所で読む。backend ごとに
// 書くと退避経路で漏れる。
//
// 対応形式は PNG (iCCP / sRGB / cHRM / gAMA)、JPEG (APP2 の ICC_PROFILE)、
// TIFF (タグ 34675)、WebP (RIFF の ICCP)。
EmbeddedColorProfile readEmbeddedColorProfile(const QString &filePath);

// バイト列から読む。テストで組み立てた画像を渡すために公開する。
EmbeddedColorProfile readEmbeddedColorProfile(const QByteArray &fileContents);

}  // namespace heiran::infra
