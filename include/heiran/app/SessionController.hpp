#pragma once

#include "heiran/domain/SessionModel.hpp"
#include "heiran/infra/HrnConfigLoader.hpp"
#include "heiran/infra/HrnConfigWriter.hpp"

#include <QString>

namespace heiran::app {

class SessionController {
public:
  SessionController() = default;

  struct SessionLoadResult {
    bool ok = false;
    heiran::domain::SessionModel session;
    QString errorMessage;
    QVector<heiran::infra::HrnIssue> issues;
    bool issuesTruncated = false;
  };

  SessionLoadResult loadSession(const QString &filePath) const;
  heiran::infra::HrnSaveResult saveSession(const heiran::domain::SessionModel &session,
                                           const QString &filePath) const;
  heiran::domain::SessionModel createDefaultSession() const;
};

}  // namespace heiran::app
