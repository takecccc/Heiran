#include "heiran/domain/SessionPages.hpp"

#include <QDir>
#include <algorithm>

namespace heiran::domain {

const ImageSet *imageSetForCell(const SessionModel &session, int cellIndex) {
  if (cellIndex < 0 || cellIndex >= session.cells.size()) {
    return nullptr;
  }

  const int imageIndex = session.cells.at(cellIndex).imageIndex;
  if (imageIndex < 0 || imageIndex >= session.images.size()) {
    return nullptr;
  }
  return &session.images.at(imageIndex);
}

QString pagePathInImageSet(const ImageSet &imageSet, int pageIndex) {
  const int localPageIndex = pageIndex - imageSet.pageOffset;
  if (localPageIndex < 0 || localPageIndex >= imageSet.resolvedPaths.size()) {
    return {};
  }
  return imageSet.resolvedPaths.at(localPageIndex);
}

CellPage cellPageAt(const SessionModel &session, int cellIndex, int pageIndex) {
  const ImageSet *imageSet = imageSetForCell(session, cellIndex);
  if (imageSet == nullptr) {
    return {};
  }
  return {.imageSet = imageSet, .path = pagePathInImageSet(*imageSet, pageIndex)};
}

int pageOfFile(const SessionModel &session, int cellIndex, const QString &path) {
  const ImageSet *imageSet = imageSetForCell(session, cellIndex);
  if (imageSet == nullptr || path.isEmpty()) {
    return -1;
  }

#if defined(Q_OS_WIN)
  constexpr Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
  constexpr Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif
  const QString wanted = QDir::cleanPath(path);
  for (int index = 0; index < imageSet->resolvedPaths.size(); ++index) {
    if (QDir::cleanPath(imageSet->resolvedPaths.at(index)).compare(wanted, kCase) == 0) {
      return imageSet->pageOffset + index;
    }
  }
  return -1;
}

int totalPageCount(const SessionModel &session) {
  int maxPages = 0;
  for (int cellIndex = 0; cellIndex < session.cells.size(); ++cellIndex) {
    const ImageSet *imageSet = imageSetForCell(session, cellIndex);
    if (imageSet == nullptr || imageSet->resolvedPaths.isEmpty()) {
      continue;
    }
    maxPages =
        std::max(maxPages, imageSet->pageOffset + static_cast<int>(imageSet->resolvedPaths.size()));
  }
  return maxPages;
}

}  // namespace heiran::domain
