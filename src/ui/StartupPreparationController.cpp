#include "heiran/ui/StartupPreparationController.hpp"

#include "heiran/domain/SessionPages.hpp"
#include "heiran/ui/DecodeScheduler.hpp"
#include "heiran/ui/SessionLayoutController.hpp"
#include "heiran/ui/SessionMutationController.hpp"

#include <QDir>
#include <QtConcurrentRun>
#include <utility>

namespace heiran::ui {

StartupPreparationController::StartupPreparationController(std::optional<QString> sessionPath,
                                                           QStringList imagePaths,
                                                           QString directory, bool parallel,
                                                           bool decodeAhead)
    : sessionPath_(std::move(sessionPath)), imagePaths_(std::move(imagePaths)),
      directory_(std::move(directory)) {
  if (sessionPath_) {
    sessionPath_ = QDir(directory_).absoluteFilePath(*sessionPath_);
  }
  for (auto &path : imagePaths_) {
    path = QDir(directory_).absoluteFilePath(path);
  }
  if (decodeAhead && (sessionPath_ || !imagePaths_.isEmpty())) {
    decodeScheduler_ = std::make_shared<DecodeScheduler>(nullptr, true);
  }
  if (parallel && (sessionPath_ || !imagePaths_.isEmpty())) {
    future_ = QtConcurrent::run([sessionPath = sessionPath_, imagePaths = imagePaths_,
                                 directory = directory_, scheduler = decodeScheduler_.get()]() {
      return prepare(sessionPath, imagePaths, directory, scheduler);
    });
    tasks_.addFuture(future_);
  }
}

StartupPreparationResult StartupPreparationController::takeResult() {
  if (!future_.isValid()) {
    return prepare(sessionPath_, imagePaths_, directory_, decodeScheduler_.get());
  }
  tasks_.waitForFinished();
  auto result = future_.result();
  tasks_.clearFutures();
  future_ = {};
  return result;
}

std::shared_ptr<DecodeScheduler> StartupPreparationController::takeDecodeScheduler() {
  tasks_.waitForFinished();
  return std::move(decodeScheduler_);
}

StartupPreparationResult StartupPreparationController::prepare(
    const std::optional<QString> &sessionPath, const QStringList &imagePaths,
    const QString &directory, DecodeScheduler *scheduler) {
  StartupPreparationResult result;
  result.startup = heiran::app::ApplicationController().prepareStartup(sessionPath);
  if (!imagePaths.isEmpty()) {
    result.images = prepareImages(imagePaths, directory);
  }
  if (scheduler != nullptr && !result.startup.loadFailed) {
    const auto &session = result.images ? result.images->session : result.startup.session;
    const int page = result.images ? result.images->initialPage : 0;
    const int cellCount = session.viewer.layout.rows * session.viewer.layout.cols;
    for (int cell = 0; cell < cellCount; ++cell) {
      const auto source = heiran::domain::cellPageAt(session, cell, page);
      if (source.hasFile()) {
        scheduler->request(source.path, source.imageSet->honorExifOrientation, false);
      }
    }
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
