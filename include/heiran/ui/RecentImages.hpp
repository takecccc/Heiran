#pragma once

#include <QString>
#include <QStringList>

namespace heiran::ui {

// 最近開いた画像の履歴。`.hrn` の履歴とは別に持つ。
// `.hrn` は比較セッションで、普段使いで開くのは画像そのものになる。
class RecentImages {
public:
  static constexpr int kMaxEntries = 10;

  // 履歴の先頭へ `path` を足した一覧を返す。既にあれば先頭へ移し、重複は作らない。
  // 上限を超えた分は古い方から落とす。同じファイルかは、区切りや余分な ./ を整え、
  // Windows では大文字小文字を無視して見る。
  static QStringList pushed(const QStringList &recent, const QString &path,
                            int maxEntries = kMaxEntries);

  // 履歴から `path` を外した一覧を返す。開こうとして無かったファイルを落とすのに使う。
  static QStringList removed(const QStringList &recent, const QString &path);

  // 保存先はアプリ全体設定と同じ場所。
  static QStringList load();
  static void save(const QStringList &recent);
};

}  // namespace heiran::ui
