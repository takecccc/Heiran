#include "heiran/app/SessionController.hpp"
#include "heiran/app/SessionPathResolver.hpp"

#include <QDir>
#include <QFileInfo>

namespace heiran::app {

namespace {

heiran::domain::SessionModel runtimeSessionFromDocument(
    const heiran::domain::SessionDocument &document, const QString &sessionFilePath,
    const QString &sessionDirectory) {
  heiran::domain::SessionModel session;
  session.formatVersion = document.formatVersion;
  session.sessionFilePath = sessionFilePath;
  session.sessionDirectory = sessionDirectory;
  session.viewer = document.viewer;
  session.cells = document.cells;
  session.images.reserve(document.images.size());
  for (const auto &imageDocument : document.images) {
    session.images.push_back({.name = imageDocument.name,
                              .label = imageDocument.label,
                              .path = imageDocument.path,
                              .scale = imageDocument.scale,
                              .pageOffset = imageDocument.pageOffset,
                              .labelVisible = imageDocument.labelVisible,
                              .honorExifOrientation = imageDocument.honorExifOrientation,
                              .colorSpaceName = imageDocument.colorSpaceName,
                              .iccProfilePath = imageDocument.iccProfilePath});
  }
  return session;
}

heiran::domain::SessionDocument documentFromRuntimeSession(
    const heiran::domain::SessionModel &session) {
  heiran::domain::SessionDocument document;
  document.formatVersion = session.formatVersion;
  document.viewer = session.viewer;
  document.cells = session.cells;
  document.images.reserve(session.images.size());
  for (const auto &imageSet : session.images) {
    document.images.push_back({.name = imageSet.name,
                               .label = imageSet.label,
                               .path = imageSet.path,
                               .scale = imageSet.scale,
                               .pageOffset = imageSet.pageOffset,
                               .labelVisible = imageSet.labelVisible,
                               .honorExifOrientation = imageSet.honorExifOrientation,
                               .colorSpaceName = imageSet.colorSpaceName,
                               .iccProfilePath = imageSet.iccProfilePath});
  }
  return document;
}

}  // namespace

SessionController::SessionLoadResult SessionController::loadSession(const QString &filePath) const {
  heiran::infra::HrnConfigLoader loader;
  auto result = loader.loadFromFile(filePath);
  if (!result.ok) {
    return {.ok = false,
            .errorMessage = result.errorMessage,
            .issues = result.issues,
            .issuesTruncated = result.issuesTruncated};
  }

  QFileInfo sessionFileInfo(filePath);
  auto session = runtimeSessionFromDocument(result.document, sessionFileInfo.absoluteFilePath(),
                                            sessionFileInfo.absolutePath());
  SessionPathResolver::resolveImagePaths(&session);
  return {.ok = true,
          .session = std::move(session),
          .errorMessage = result.errorMessage,
          .issues = result.issues,
          .issuesTruncated = result.issuesTruncated};
}

heiran::infra::HrnSaveResult SessionController::saveSession(
    const heiran::domain::SessionModel &session, const QString &filePath) const {
  heiran::domain::SessionModel writableSession = session;
  QFileInfo sessionFileInfo(filePath);
  writableSession.sessionFilePath = sessionFileInfo.absoluteFilePath();
  writableSession.sessionDirectory = sessionFileInfo.absolutePath();
  const auto document = documentFromRuntimeSession(writableSession);

  heiran::infra::HrnConfigWriter writer;
  return writer.saveToFile(document, filePath);
}

heiran::domain::SessionModel SessionController::createDefaultSession() const {
  // 空の 1 セルから始める。以前は images/a/* などを指す 2x2 を返していたが、
  // パスはカレントディレクトリ基準のため、スタートメニューやエクスプローラから
  // 起動すると 4 セルすべてが読込失敗の表示になっていた。
  heiran::domain::SessionModel session;
  session.sessionDirectory = QDir::currentPath();
  session.viewer.layout.rows = 1;
  session.viewer.layout.cols = 1;
  session.cells = {{.imageIndex = -1, .sync = true}};
  return session;
}

}  // namespace heiran::app
