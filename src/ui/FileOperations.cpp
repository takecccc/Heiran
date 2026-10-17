#include "heiran/ui/FileOperations.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace heiran::ui {

namespace {

// Windows でファイル名に使えない文字。他の環境でも同じ名前にしておく方が、
// 別の環境へ持っていったときに困らない。
const QString &invalidCharacters() {
  static const QString characters = QStringLiteral("\\/:*?\"<>|");
  return characters;
}

// Windows の予約名。拡張子が付いていても使えない。
bool isReservedName(const QString &baseName) {
  static const QStringList reserved = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
      "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  return reserved.contains(baseName.toUpper());
}

}  // namespace

QString FileOperations::validateFileName(const QString &name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return QCoreApplication::translate("FileOperations", "The file name is empty.");
  }
  for (const QChar character : trimmed) {
    if (invalidCharacters().contains(character) || character.unicode() < 0x20) {
      return QCoreApplication::translate("FileOperations",
                                         "The file name cannot contain any of these characters: %1")
          .arg(invalidCharacters());
    }
  }
  if (trimmed == "." || trimmed == ".." || trimmed.endsWith('.') || trimmed.endsWith(' ')) {
    return QCoreApplication::translate("FileOperations",
                                       "The file name cannot end with a period or a space.");
  }
  if (isReservedName(trimmed.section('.', 0, 0))) {
    return QCoreApplication::translate("FileOperations",
                                       "The file name is reserved by the system.");
  }
  return {};
}

FileOperationResult FileOperations::rename(const QString &path, const QString &newName) {
  const QFileInfo source(path);
  if (!source.exists() || !source.isFile()) {
    return {.errorMessage =
                QCoreApplication::translate("FileOperations", "The file no longer exists.")};
  }

  const QString trimmed = newName.trimmed();
  if (const QString problem = validateFileName(trimmed); !problem.isEmpty()) {
    return {.errorMessage = problem};
  }
  if (trimmed == source.fileName()) {
    return {.ok = true, .newPath = source.absoluteFilePath()};
  }

  const QString targetPath = QDir(source.absolutePath()).filePath(trimmed);
  const QFileInfo target(targetPath);
  // 大文字小文字だけの変更は、Windows では同じファイルを指すため存在すると見える。
  const bool sameFile = target.exists() && QFileInfo(source.absoluteFilePath()) == target;
  if (target.exists() && !sameFile) {
    return {.errorMessage =
                QCoreApplication::translate("FileOperations", "A file named \"%1\" already exists.")
                    .arg(trimmed)};
  }

  QFile file(source.absoluteFilePath());
  if (sameFile) {
    // 移動先が存在すると QFile::rename は失敗する。一時の名前を経由して大文字小文字を変える。
    const QString temporaryPath =
        QDir(source.absolutePath()).filePath(QString("%1.heiran-rename").arg(source.fileName()));
    if (QFileInfo::exists(temporaryPath) || !file.rename(temporaryPath)) {
      return {.errorMessage =
                  QCoreApplication::translate("FileOperations", "Could not rename the file.")};
    }
    if (!file.rename(targetPath)) {
      // 戻せなければ元の名前へ戻す。一時の名前で残さない。
      const QString reason = file.errorString();
      file.rename(source.absoluteFilePath());
      return {.errorMessage =
                  QCoreApplication::translate("FileOperations", "Could not rename the file: %1")
                      .arg(reason)};
    }
    return {.ok = true, .newPath = QFileInfo(targetPath).absoluteFilePath()};
  }
  if (!file.rename(targetPath)) {
    return {.errorMessage =
                QCoreApplication::translate("FileOperations", "Could not rename the file: %1")
                    .arg(file.errorString())};
  }
  return {.ok = true, .newPath = QFileInfo(targetPath).absoluteFilePath()};
}

FileOperationResult FileOperations::moveToTrash(const QString &path, const TrashFunction &trash) {
  const QFileInfo source(path);
  if (!source.exists() || !source.isFile()) {
    return {.errorMessage =
                QCoreApplication::translate("FileOperations", "The file no longer exists.")};
  }
  if (!trash(source.absoluteFilePath())) {
    return {.errorMessage = QCoreApplication::translate(
                "FileOperations", "Could not move the file to the Recycle Bin.")};
  }
  return {.ok = true};
}

QString FileOperations::pathForClipboard(const QString &path) {
  return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool FileOperations::defaultTrash(const QString &path) {
  return QFile::moveToTrash(path);
}

void FileOperations::showInFolder(const QString &path) {
  if (path.isEmpty()) {
    return;
  }

#if defined(Q_OS_WIN)
  QProcess::startDetached("explorer.exe",
                          {QString("/select,%1").arg(QDir::toNativeSeparators(path))});
#else
  QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

}  // namespace heiran::ui
