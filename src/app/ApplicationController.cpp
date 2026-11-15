#include "heiran/app/ApplicationController.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

namespace heiran::app {

namespace {

constexpr int kMaxRecentSessions = 10;
constexpr auto kRecentSessionsKey = "recent_sessions";

QString normalizeSessionPath(const QString &sessionPath) {
  return QFileInfo(sessionPath).absoluteFilePath();
}

}  // namespace

ApplicationController::StartupResult ApplicationController::startup(
    const std::optional<QString> &sessionPath) {
  auto result = prepareStartup(sessionPath);
  completeStartup(result);
  return result;
}

void ApplicationController::completeStartup(const StartupResult &result) {
  if (!result.loadFailed && !result.session.sessionFilePath.isEmpty()) {
    rememberRecentSession(result.session.sessionFilePath);
  }
}

ApplicationController::StartupResult ApplicationController::prepareStartup(
    const std::optional<QString> &sessionPath) const {
  if (!sessionPath.has_value()) {
    return {sessionController_.createDefaultSession(),
            QCoreApplication::translate("ApplicationController",
                                        "Drop an image here, or use File > Open Image...")};
  }

  const auto openResult = sessionController_.loadSession(*sessionPath);
  if (openResult.ok) {
    return {.session = openResult.session,
            .startupMessage = QCoreApplication::translate("ApplicationController", "Loaded %1")
                                  .arg(normalizeSessionPath(*sessionPath)),
            .issues = openResult.issues,
            .issuesTruncated = openResult.issuesTruncated};
  }

  return {.session = sessionController_.createDefaultSession(),
          .startupMessage =
              QCoreApplication::translate("ApplicationController", "Failed to load %1: %2")
                  .arg(*sessionPath, openResult.errorMessage),
          .issues = openResult.issues,
          .issuesTruncated = openResult.issuesTruncated,
          .loadFailed = true};
}

ApplicationController::OpenSessionResult ApplicationController::openSession(
    const QString &sessionPath) {
  const auto loadResult = sessionController_.loadSession(sessionPath);
  if (!loadResult.ok) {
    return {.ok = false,
            .userMessage = loadResult.errorMessage,
            .issues = loadResult.issues,
            .issuesTruncated = loadResult.issuesTruncated};
  }

  rememberRecentSession(sessionPath);
  return {.ok = true,
          .session = loadResult.session,
          .userMessage = QCoreApplication::translate("ApplicationController", "Loaded %1")
                             .arg(normalizeSessionPath(sessionPath)),
          .issues = loadResult.issues,
          .issuesTruncated = loadResult.issuesTruncated};
}

QStringList ApplicationController::recentSessionPaths() const {
  QSettings settings;
  return settings.value(kRecentSessionsKey).toStringList();
}

ApplicationController::SaveSessionResult ApplicationController::saveSession(
    const heiran::domain::SessionModel &session, const QString &sessionPath) {
  const auto saveResult = sessionController_.saveSession(session, sessionPath);
  if (!saveResult.ok) {
    return {.ok = false, .userMessage = saveResult.errorMessage};
  }

  const auto loadResult = sessionController_.loadSession(sessionPath);
  if (!loadResult.ok) {
    return {.ok = false, .userMessage = loadResult.errorMessage};
  }

  rememberRecentSession(sessionPath);
  return {.ok = true,
          .session = loadResult.session,
          .userMessage = QCoreApplication::translate("ApplicationController", "Saved %1")
                             .arg(normalizeSessionPath(sessionPath))};
}

void ApplicationController::rememberRecentSession(const QString &sessionPath) {
  QSettings settings;
  QStringList recentPaths = settings.value(kRecentSessionsKey).toStringList();
  const QString normalizedPath = normalizeSessionPath(sessionPath);
  recentPaths.removeAll(normalizedPath);
  recentPaths.prepend(normalizedPath);
  while (recentPaths.size() > kMaxRecentSessions) {
    recentPaths.removeLast();
  }
  settings.setValue(kRecentSessionsKey, recentPaths);
}

}  // namespace heiran::app
