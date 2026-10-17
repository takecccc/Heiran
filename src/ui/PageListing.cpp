#include "heiran/ui/PageListing.hpp"

#include "heiran/domain/SessionPages.hpp"

#include <QDir>

namespace heiran::ui {

QString PageListing::pagePath(const heiran::domain::SessionModel &session, int pageIndex) {
  const int visibleCellCount = session.viewer.layout.rows * session.viewer.layout.cols;
  for (int cellIndex = 0; cellIndex < visibleCellCount && cellIndex < session.cells.size();
       ++cellIndex) {
    const auto cellPage = heiran::domain::cellPageAt(session, cellIndex, pageIndex);
    if (cellPage.hasFile()) {
      return cellPage.path;
    }
  }
  return {};
}

QStringList PageListing::pagePaths(const heiran::domain::SessionModel &session, int totalPages) {
  QStringList paths;
  for (int pageIndex = 0; pageIndex < totalPages; ++pageIndex) {
    paths.push_back(pagePath(session, pageIndex));
  }
  return paths;
}

QStringList PageListing::displayTexts(const heiran::domain::SessionModel &session, int totalPages) {
  QStringList items;
  for (int pageIndex = 0; pageIndex < totalPages; ++pageIndex) {
    const QString path = pagePath(session, pageIndex);

    QString text = QString("%1").arg(pageIndex + 1);
    if (!path.isEmpty()) {
      const QString displayPath = session.sessionDirectory.isEmpty()
                                      ? path
                                      : QDir(session.sessionDirectory).relativeFilePath(path);
      text += QString(": %1").arg(displayPath);
    }
    items.push_back(text);
  }
  return items;
}

}  // namespace heiran::ui
