#pragma once

#include "heiran/app/SessionController.hpp"
#include "heiran/domain/SessionModel.hpp"

#include <QStringList>
#include <optional>

namespace heiran::app {

class ApplicationController {
public:
  ApplicationController() = default;

  struct StartupResult {
    heiran::domain::SessionModel session;
    QString startupMessage;
    QVector<heiran::infra::HrnIssue> issues;
    bool issuesTruncated = false;
    bool loadFailed = false;
  };

  struct OpenSessionResult {
    bool ok = false;
    heiran::domain::SessionModel session;
    QString userMessage;
    QVector<heiran::infra::HrnIssue> issues;
    bool issuesTruncated = false;
  };

  struct SaveSessionResult {
    bool ok = false;
    heiran::domain::SessionModel session;
    QString userMessage;
  };

  StartupResult startup(const std::optional<QString> &sessionPath);
  StartupResult prepareStartup(const std::optional<QString> &sessionPath) const;
  void completeStartup(const StartupResult &result);
  OpenSessionResult openSession(const QString &sessionPath);
  SaveSessionResult saveSession(const heiran::domain::SessionModel &session,
                                const QString &sessionPath);
  QStringList recentSessionPaths() const;

private:
  void rememberRecentSession(const QString &sessionPath);

  SessionController sessionController_;
};

}  // namespace heiran::app
