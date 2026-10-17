#include "heiran/ui/PrefetchPlanner.hpp"

#include <algorithm>

namespace heiran::ui {

int prefetchRadiusForBudget(int entryCount, qint64 usedBytes, qint64 budgetBytes,
                            int entriesPerPage, int maxRadius) {
  if (maxRadius <= 0) {
    return 0;
  }
  if (entryCount <= 0 || usedBytes <= 0 || budgetBytes <= 0) {
    return maxRadius;
  }

  // 1 ページあたりの大きさは、いま抱えているものの平均で見積もる。
  const qint64 averageEntryBytes = usedBytes / entryCount;
  const qint64 bytesPerPage = averageEntryBytes * std::max(1, entriesPerPage);
  if (bytesPerPage <= 0) {
    return maxRadius;
  }

  const qint64 pagesThatFit = budgetBytes / bytesPerPage;
  // 現在ページの分を除いて前後へ振り分ける。
  const qint64 radius = (pagesThatFit - 1) / 2;
  // 1 ページ分も入らない場合でも隣までは読む。ページ送りが全て待ちになるより、
  // 入れ替わりを承知で 1 枚先を持つ方がよい。
  return static_cast<int>(std::clamp<qint64>(radius, 1, maxRadius));
}

QVector<int> prefetchPageOrder(int currentPage, int totalPages, int radius) {
  QVector<int> pages;
  if (totalPages <= 0 || radius <= 0) {
    return pages;
  }

  pages.reserve(radius * 2);
  for (int distance = 1; distance <= radius; ++distance) {
    for (const int targetPage : {currentPage - distance, currentPage + distance}) {
      if (targetPage >= 0 && targetPage < totalPages) {
        pages.push_back(targetPage);
      }
    }
  }
  return pages;
}

}  // namespace heiran::ui
