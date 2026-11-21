#pragma once

#include "heiran/app/ApplicationController.hpp"

#include <QFuture>
#include <QFutureSynchronizer>
#include <QStringList>
#include <memory>
#include <optional>

namespace heiran::ui {

class DecodeScheduler;

struct StartupDiffTitles {
  QString left;
  QString right;
};

struct PreparedImageSession {
  heiran::domain::SessionModel session;
  QStringList sourcePaths;
  int initialPage = 0;
  QString statusMessage;
  bool externalDiff = false;
};

struct StartupPreparationResult {
  heiran::app::ApplicationController::StartupResult startup;
  std::optional<PreparedImageSession> images;
};

class StartupPreparationController {
public:
  StartupPreparationController(std::optional<QString> sessionPath, QStringList imagePaths,
                               QString directory, bool parallel = true, bool decodeAhead = true,
                               std::optional<StartupDiffTitles> diffTitles = std::nullopt);
  StartupPreparationResult takeResult();
  std::shared_ptr<DecodeScheduler> takeDecodeScheduler();
  static PreparedImageSession prepareImages(const QStringList &paths, const QString &directory);
  static PreparedImageSession prepareDiffImages(const QStringList &paths, const QString &directory,
                                                const StartupDiffTitles &titles = {});

private:
  static StartupPreparationResult prepare(const std::optional<QString> &sessionPath,
                                          const QStringList &imagePaths, const QString &directory,
                                          DecodeScheduler *scheduler,
                                          const std::optional<StartupDiffTitles> &diffTitles);

  std::optional<QString> sessionPath_;
  QStringList imagePaths_;
  QString directory_;
  std::optional<StartupDiffTitles> diffTitles_;
  std::shared_ptr<DecodeScheduler> decodeScheduler_;
  QFuture<StartupPreparationResult> future_;
  QFutureSynchronizer<StartupPreparationResult> tasks_;
};

}  // namespace heiran::ui
