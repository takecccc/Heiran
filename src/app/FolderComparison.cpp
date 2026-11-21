#include "heiran/app/FolderComparison.hpp"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMap>
#include <QSet>
#include <cmath>

namespace heiran::app {
namespace {

void enumerateFiles(const QDir &root, bool left, QMap<QString, FolderComparisonEntry> &entries,
                    QStringList &unreadable, const std::function<bool()> &canceled) {
  QStringList pending{root.absolutePath()};
  while (!pending.isEmpty() && !canceled()) {
    const QDir directory(pending.takeLast());
    const QFileInfo info(directory.absolutePath());
    if (!info.isDir() || !info.isReadable()) {
      unreadable.append(directory.absolutePath());
      continue;
    }
    const auto children = directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden |
                                                      QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                                  QDir::NoSort);
    for (const auto &child : children) {
      if (canceled()) {
        return;
      }
      if (child.fileName() == ".git" || child.isJunction()) {
        continue;
      }
      if (child.isDir()) {
        pending.append(child.absoluteFilePath());
      } else {
        const auto relative = root.relativeFilePath(child.absoluteFilePath());
        auto &entry = entries[relative];
        entry.relativePath = relative;
        (left ? entry.leftPath : entry.rightPath) = child.absoluteFilePath();
      }
    }
  }
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

FolderComparisonResult compareFolders(const QString &left, const QString &right,
                                      const std::function<bool()> &isCanceled) {
  const auto canceled = [&]() { return isCanceled && isCanceled(); };
  QMap<QString, FolderComparisonEntry> entries;
  FolderComparisonResult result;
  enumerateFiles(QDir(left), true, entries, result.unreadableDirectories, canceled);
  enumerateFiles(QDir(right), false, entries, result.unreadableDirectories, canceled);
  const auto suffixes = imageSuffixes();
  result.entries.reserve(entries.size());
  for (auto &entry : entries) {
    if (canceled()) {
      return {};
    }
    entry.status = compareFileContents(entry, canceled);
    entry.image = suffixes.contains(QFileInfo(entry.relativePath).suffix().toLower().toLatin1());
    result.entries.append(std::move(entry));
  }
  return canceled() ? FolderComparisonResult{} : result;
}

bool equalDecodedPixels(const infra::DecodedImage &left, const infra::DecodedImage &right,
                        const std::function<bool()> &canceled) {
  if (left.displayImage.isNull() || right.displayImage.isNull() || left.size() != right.size()) {
    return false;
  }
  if (!left.hasAnalysisBuffer() && !right.hasAnalysisBuffer()) {
    return left.displayImage.convertToFormat(QImage::Format_RGBA8888) ==
           right.displayImage.convertToFormat(QImage::Format_RGBA8888);
  }
  for (int y = 0; y < left.size().height(); ++y) {
    if (canceled && canceled()) {
      return false;
    }
    for (int x = 0; x < left.size().width(); ++x) {
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
