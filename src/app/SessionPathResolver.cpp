#include "heiran/app/SessionPathResolver.hpp"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace heiran::app {

QVector<QString> SessionPathResolver::resolveGlobbedPaths(const QString &sessionDirectory,
                                                          const QString &globPath) {
  QVector<QString> resolvedPaths;
  QFileInfo patternInfo(globPath);
  const QString directoryPath = patternInfo.isAbsolute()
                                    ? patternInfo.absolutePath()
                                    : QDir(sessionDirectory).filePath(patternInfo.path());
  const QString filePattern = patternInfo.fileName();

  QDir directory(directoryPath);
  if (!directory.exists() || filePattern.isEmpty()) {
    return resolvedPaths;
  }

  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);

  QFileInfoList fileInfos =
      directory.entryInfoList({filePattern}, QDir::Files | QDir::Readable, QDir::Name);
  std::sort(fileInfos.begin(), fileInfos.end(),
            [&collator](const QFileInfo &left, const QFileInfo &right) {
              return collator.compare(left.fileName(), right.fileName()) < 0;
            });

  resolvedPaths.reserve(fileInfos.size());
  for (const auto &fileInfo : fileInfos) {
    resolvedPaths.push_back(fileInfo.absoluteFilePath());
  }
  return resolvedPaths;
}

void SessionPathResolver::resolveImagePaths(heiran::domain::SessionModel *session) {
  for (auto &imageSet : session->images) {
    imageSet.resolvedPaths = resolveGlobbedPaths(session->sessionDirectory, imageSet.path);
  }
}

}  // namespace heiran::app
