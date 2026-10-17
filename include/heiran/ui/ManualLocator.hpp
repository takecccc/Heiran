#pragma once

#include <QString>

namespace heiran::ui {

// ユーザーマニュアルの index.html を探す。見つからなければ空。
//
// 探す場所 (実行ファイルのあるフォルダ `applicationDir` から見て、上から順):
//   1. ../doc/manual/index.html   インストールした形 (bin/heiran.exe の隣に doc/)
//   2. doc/manual/index.html      ZIP を展開して、実行ファイルを直に置いた形
//   3. manual/index.html          ビルドしたフォルダ (scripts/build-manual.ps1 の出力)
QString findManualIndex(const QString &applicationDir);

}  // namespace heiran::ui
