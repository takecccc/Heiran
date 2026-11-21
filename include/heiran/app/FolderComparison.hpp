#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QStringList>
#include <functional>

namespace heiran::app {

enum class FileComparisonStatus { Identical, Different, LeftOnly, RightOnly, Unreadable };

struct FolderComparisonEntry {
  QString relativePath;
  QString leftPath;
  QString rightPath;
  FileComparisonStatus status = FileComparisonStatus::Unreadable;
  bool image = false;
};

struct FolderComparisonResult {
  QVector<FolderComparisonEntry> entries;
  QStringList unreadableDirectories;
};

FolderComparisonResult compareFolders(const QString &left, const QString &right,
                                      const std::function<bool()> &canceled = {});
bool hasComparisonImageSuffix(const QString &path);
bool equalDecodedPixels(const infra::DecodedImage &left, const infra::DecodedImage &right,
                        bool ignoreFullyTransparentRgb = false,
                        const std::function<bool()> &canceled = {});

}  // namespace heiran::app
