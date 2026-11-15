#include "heiran/ui/StartupPreparationController.hpp"

#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/SessionMutationController.hpp"

#include <QDir>
#include <QtConcurrentRun>
#include <utility>

namespace heiran::ui {

StartupPreparationController::StartupPreparationController(std::optional<QString> sessionPath,
                                                           QStringList imagePaths,
                                                           QString directory, bool parallel)
    : sessionPath_(std::move(sessionPath)), imagePaths_(std::move(imagePaths)),
      directory_(std::move(directory)) {
  if (sessionPath_) {
    sessionPath_ = QDir(directory_).absoluteFilePath(*sessionPath_);
  }
  for (auto &path : imagePaths_) {
    path = QDir(directory_).absoluteFilePath(path);
  }
  if (parallel && (sessionPath_ || !imagePaths_.isEmpty())) {
    future_ = QtConcurrent::run(
        [sessionPath = sessionPath_, imagePaths = imagePaths_, directory = directory_]() {
          return prepare(sessionPath, imagePaths, directory);
        });
    tasks_.addFuture(future_);
  }
}

StartupPreparationResult StartupPreparationController::takeResult() {
  if (!future_.isValid()) {
    return prepare(sessionPath_, imagePaths_, directory_);
  }
  tasks_.waitForFinished();
  auto result = future_.result();
  tasks_.clearFutures();
  future_ = {};
  return result;
}

StartupPreparationResult StartupPreparationController::prepare(
    const std::optional<QString> &sessionPath, const QStringList &imagePaths,
    const QString &directory) {
  StartupPreparationResult result;
  result.startup = heiran::app::ApplicationController().prepareStartup(sessionPath);
  if (!imagePaths.isEmpty()) {
    result.images = prepareImages(imagePaths, directory);
  }
  return result;
}

PreparedImageSession StartupPreparationController::prepareImages(const QStringList &paths,
                                                                 const QString &directory) {
  PreparedImageSession prepared;
  for (const auto &path : paths) {
    prepared.sourcePaths.push_back(QDir(directory).absoluteFilePath(path));
  }
  prepared.session.sessionDirectory = directory;
  prepared.session.viewer.layout =
      SessionLayoutController::layoutForImageCount(static_cast<int>(paths.size()));
  const auto &layout = prepared.session.viewer.layout;
  prepared.session.cells.resize(layout.rows * layout.cols);
  auto cells = prepared.session.cells;
  const auto mutation = SessionMutationController::applyDroppedItems(&prepared.session, &cells, 0,
                                                                     prepared.sourcePaths, 0);
  prepared.initialPage = mutation.targetPage;
  prepared.statusMessage = mutation.statusMessage;
  return prepared;
}

}  // namespace heiran::ui
