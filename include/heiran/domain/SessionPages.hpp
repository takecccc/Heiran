#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>

namespace heiran::domain {

// セルに割り当てた画像セット。割り当て無しや範囲外なら nullptr。
const ImageSet *imageSetForCell(const SessionModel &session, int cellIndex);

// 画像セットの中で、全体のページ番号 (0 起点) が指すファイル。
// `pageOffset` より前や、末尾より後ろのページは持たない。その場合は空を返す。
QString pagePathInImageSet(const ImageSet &imageSet, int pageIndex);

// あるセルが、指定したページで見せるファイル。
struct CellPage {
  const ImageSet *imageSet = nullptr;
  QString path;

  // 見せるファイルがあるか。割り当て無しと、そのページを持たない場合は偽。
  bool hasFile() const { return !path.isEmpty(); }
};

CellPage cellPageAt(const SessionModel &session, int cellIndex, int pageIndex);

// セルが見せる画像セットの中で、指定したファイルが何ページ目か。無ければ -1。
// ファイルの名前を変えると並びが変わるため、変えた後に同じファイルを表示し続けるのに使う。
int pageOfFile(const SessionModel &session, int cellIndex, const QString &path);

// 全体のページ数。最も後ろまで続く画像セットで決まる。
// ファイルを 1 つも解決できなかった画像セットは数えない。
int totalPageCount(const SessionModel &session);

}  // namespace heiran::domain
