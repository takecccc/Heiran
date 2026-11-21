#include "heiran/app/FolderComparison.hpp"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMap>
#include <QSet>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace heiran::app {
namespace {

void enumerateFiles(const QDir &root, bool left, QMap<QString, FolderComparisonEntry> &entries,
                    QStringList &unreadable, QSet<QString> &unreadableRelative,
                    const std::function<bool()> &canceled) {
  QStringList pending{root.absolutePath()};
  while (!pending.isEmpty() && !canceled()) {
    const QDir directory(pending.takeLast());
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory.filesystemAbsolutePath(), error), end;
         !error && it != end; it.increment(error)) {
      if (canceled()) {
        return;
      }
      const QFileInfo child(it->path());
      if (child.fileName() == ".git" || child.isSymLink() || child.isJunction()) {
        continue;
      }
      if (child.isDir()) {
        pending.append(child.absoluteFilePath());
      } else if (child.isFile()) {
        const auto relative = root.relativeFilePath(child.absoluteFilePath());
        auto &entry = entries[relative];
        entry.relativePath = relative;
        (left ? entry.leftPath : entry.rightPath) = child.absoluteFilePath();
      }
    }
    if (error) {
      unreadable.append(directory.absolutePath());
      unreadableRelative.insert(root.relativeFilePath(directory.absolutePath()));
    }
  }
}

bool inUnreadableDirectory(const QString &path, const QSet<QString> &unreadable) {
  if (unreadable.contains(".") || unreadable.contains(QString())) {
    return true;
  }
  QStringView parent(path);
  for (auto slash = parent.lastIndexOf(u'/'); slash >= 0; slash = parent.lastIndexOf(u'/')) {
    parent = parent.first(slash);
    if (unreadable.contains(parent.toString())) {
      return true;
    }
  }
  return false;
}

FileComparisonStatus compareFileContents(const FolderComparisonEntry &entry,
                                         const std::function<bool()> &canceled) {
  if (entry.leftPath.isEmpty()) {
    return FileComparisonStatus::RightOnly;
  }
  if (entry.rightPath.isEmpty()) {
    return FileComparisonStatus::LeftOnly;
  }
  QFile left(entry.leftPath);
  QFile right(entry.rightPath);
  if (!left.open(QIODevice::ReadOnly) || !right.open(QIODevice::ReadOnly)) {
    return FileComparisonStatus::Unreadable;
  }
  if (left.size() != right.size()) {
    return FileComparisonStatus::Different;
  }
  constexpr qint64 chunkSize = 1024 * 1024;
  while (!canceled()) {
    const auto a = left.read(chunkSize);
    const auto b = right.read(chunkSize);
    if (left.error() != QFileDevice::NoError || right.error() != QFileDevice::NoError) {
      return FileComparisonStatus::Unreadable;
    }
    if (a != b) {
      return FileComparisonStatus::Different;
    }
    if (a.isEmpty()) {
      return FileComparisonStatus::Identical;
    }
  }
  return FileComparisonStatus::Unreadable;
}

QSet<QByteArray> imageSuffixes() {
  const auto qt = QImageReader::supportedImageFormats();
  QSet<QByteArray> result(qt.cbegin(), qt.cend());
  result.unite({"png", "apng", "jpg", "jpeg", "bmp", "gif", "webp", "avif", "jxl", "tif", "tiff",
                "exr", "hdr", "dpx", "psd", "tga"});
  return result;
}

}  // namespace

bool hasComparisonImageSuffix(const QString &path) {
  static const auto suffixes = imageSuffixes();
  return suffixes.contains(QFileInfo(path).suffix().toLower().toLatin1());
}

FolderComparisonResult compareFolders(const QString &left, const QString &right,
                                      const std::function<bool()> &isCanceled) {
  const auto canceled = [&]() { return isCanceled && isCanceled(); };
  QMap<QString, FolderComparisonEntry> entries;
  FolderComparisonResult result;
  QSet<QString> unreadableLeft;
  QSet<QString> unreadableRight;
  enumerateFiles(QDir(left), true, entries, result.unreadableDirectories, unreadableLeft, canceled);
  enumerateFiles(QDir(right), false, entries, result.unreadableDirectories, unreadableRight,
                 canceled);
  result.entries.reserve(entries.size());
  for (auto &entry : entries) {
    if (canceled()) {
      return {};
    }
    const bool unknownLeft = !unreadableLeft.isEmpty() && entry.leftPath.isEmpty() &&
                             inUnreadableDirectory(entry.relativePath, unreadableLeft);
    const bool unknownRight = !unreadableRight.isEmpty() && entry.rightPath.isEmpty() &&
                              inUnreadableDirectory(entry.relativePath, unreadableRight);
    entry.status = unknownLeft || unknownRight ? FileComparisonStatus::Unreadable
                                               : compareFileContents(entry, canceled);
    entry.image = hasComparisonImageSuffix(entry.relativePath);
    result.entries.append(std::move(entry));
  }
  return canceled() ? FolderComparisonResult{} : result;
}

bool equalDecodedPixels(const infra::DecodedImage &left, const infra::DecodedImage &right,
                        bool ignoreFullyTransparentRgb, const std::function<bool()> &canceled) {
  if (left.displayImage.isNull() || right.displayImage.isNull() || left.size() != right.size()) {
    return false;
  }
  if (!left.hasAnalysisBuffer() && !right.hasAnalysisBuffer()) {
    const auto base = left.displayImage.convertToFormat(QImage::Format_RGBA8888);
    const auto target = right.displayImage.convertToFormat(QImage::Format_RGBA8888);
    if (base.isNull() || target.isNull()) {
      return false;
    }
    if (!ignoreFullyTransparentRgb) {
      return base == target;
    }
    for (int y = 0; y < base.height(); ++y) {
      if (canceled && canceled()) {
        return false;
      }
      const uchar *a = base.constScanLine(y);
      const uchar *b = target.constScanLine(y);
      for (int x = 0; x < base.width(); ++x, a += 4, b += 4) {
        if ((a[3] != 0 || b[3] != 0) && std::memcmp(a, b, 4) != 0) {
          return false;
        }
      }
    }
    return true;
  }
  for (int y = 0; y < left.size().height(); ++y) {
    if (canceled && canceled()) {
      return false;
    }
    for (int x = 0; x < left.size().width(); ++x) {
      if (ignoreFullyTransparentRgb && left.sampleRgba(x, y, 3) == 0.0F &&
          right.sampleRgba(x, y, 3) == 0.0F) {
        continue;
      }
      for (int channel = 0; channel < 4; ++channel) {
        const float a = left.sampleRgba(x, y, channel);
        const float b = right.sampleRgba(x, y, channel);
        if (a != b && !(std::isnan(a) && std::isnan(b))) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace heiran::app
