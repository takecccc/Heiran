#pragma once

#include "heiran/app/ApplicationController.hpp"

#include <QFuture>
#include <QFutureSynchronizer>
#include <QStringList>
#include <optional>

namespace heiran::ui {

struct PreparedImageSession {
  heiran::domain::SessionModel session;
  QStringList sourcePaths;
  int initialPage = 0;
  QString statusMessage;
};

struct StartupPreparationResult {
  heiran::app::ApplicationController::StartupResult startup;
  std::optional<PreparedImageSession> images;
};

class StartupPreparationController {
public:
  StartupPreparationController(std::optional<QString> sessionPath, QStringList imagePaths,
                               QString directory, bool parallel = true);
  StartupPreparationResult takeResult();
  static PreparedImageSession prepareImages(const QStringList &paths, const QString &directory);

private:
  static StartupPreparationResult prepare(const std::optional<QString> &sessionPath,
                                          const QStringList &imagePaths, const QString &directory);

  std::optional<QString> sessionPath_;
  QStringList imagePaths_;
  QString directory_;
  QFuture<StartupPreparationResult> future_;
  QFutureSynchronizer<StartupPreparationResult> tasks_;
};

}  // namespace heiran::ui
