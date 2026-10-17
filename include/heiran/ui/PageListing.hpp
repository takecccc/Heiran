#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>
#include <QStringList>

namespace heiran::ui {

// ページ送りの一覧に出す情報を、セッションから組み立てる。
class PageListing {
public:
  // ページを代表するファイル。表示中のセルのうち、最初にそのページを持つものを使う。
  // どのセルも持たなければ空を返す。
  static QString pagePath(const heiran::domain::SessionModel &session, int pageIndex);

  // 0 から totalPages 未満までの代表ファイル。
  static QStringList pagePaths(const heiran::domain::SessionModel &session, int totalPages);

  // "3: sub/a.png" 形式の表示文字列。セッションの場所からの相対パスで出す。
  // ファイルが無いページは番号だけになる。
  static QStringList displayTexts(const heiran::domain::SessionModel &session, int totalPages);
};

}  // namespace heiran::ui
