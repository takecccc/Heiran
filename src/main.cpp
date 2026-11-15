#include "heiran/app/ApplicationController.hpp"
#include "heiran/app/CrashReporter.hpp"
#include "heiran/app/Localization.hpp"
#include "heiran/app/OpenGlCheck.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFutureSynchronizer>
#include <QIcon>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QTimer>
#include <QtConcurrentRun>

#ifdef HEIRAN_STARTUP_BENCHMARK
#include <QSettings>
#include <QTemporaryDir>
#endif

#include <optional>

int main(int argc, char *argv[]) {
  QElapsedTimer startupTimer;
  startupTimer.start();
#ifdef HEIRAN_STARTUP_BENCHMARK
  const bool profileStartup = true;
#else
  const bool profileStartup = qEnvironmentVariableIntValue("HEIRAN_PROFILE_STARTUP") != 0;
#endif
  qint64 previousStageNs = 0;
  const auto recordStartup = [&](const char *stage) {
    if (profileStartup) {
      const qint64 elapsedNs = startupTimer.nsecsElapsed();
      qInfo("startup %s: stage=%.3f ms total=%.3f ms", stage,
            static_cast<double>(elapsedNs - previousStageNs) / 1'000'000.0,
            static_cast<double>(elapsedNs) / 1'000'000.0);
      previousStageNs = elapsedNs;
    }
  };
  QApplication app(argc, argv);
  recordStartup("application");
  QCoreApplication::setOrganizationName(QCoreApplication::translate("main", "Heiran"));
  QCoreApplication::setApplicationName(QCoreApplication::translate("main", "Heiran"));
#ifdef HEIRAN_STARTUP_BENCHMARK
  QTemporaryDir benchmarkSettings;
  if (!benchmarkSettings.isValid()) {
    return 1;
  }
  QSettings originalSettings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, benchmarkSettings.path());
  QSettings isolatedSettings;
  for (const auto &key : originalSettings.allKeys()) {
    isolatedSettings.setValue(key, originalSettings.value(key));
  }
#endif

  // 表示言語。名前の設定より後に読む (設定の保存先が、組織名とアプリ名から決まる)。
  // HEIRAN_LANGUAGE (auto / en / ja) は、設定より優先する (確認用)。
  heiran::app::installTranslators(
      app, qEnvironmentVariable("HEIRAN_LANGUAGE", heiran::ui::AppSettings::load().language));

  // 落ちたときの状態を残す。まれに 0xC0000005 で落ちる事象を追っており、
  // 再現手順が無いため、起きたときの記録だけが手掛かりになる。
  // 名前の設定より後に呼ぶ。書き出し先が組織名とアプリ名から決まる。
  heiran::app::installCrashReporter();
  QApplication::setWindowIcon(QIcon(":/heiran-256.png"));
  recordStartup("settings-and-localization");

  QFutureSynchronizer<int> fontWarmup;
  bool parallelFonts = true;
#ifdef HEIRAN_STARTUP_BENCHMARK
  parallelFonts = !qEnvironmentVariableIsSet("HEIRAN_BENCHMARK_SEQUENTIAL_FONTS");
#endif
  if (parallelFonts) {
    fontWarmup.addFuture(QtConcurrent::run([font = QApplication::font()]() {
      return QFontMetrics(font).horizontalAdvance(QStringLiteral("Heiran"));
    }));
  }
  if (!qEnvironmentVariableIsSet("HEIRAN_SKIP_OPENGL_CHECK")) {
    const QString problem = heiran::app::checkOpenGlSupport();
    if (!problem.isEmpty()) {
      QMessageBox::critical(nullptr, QCoreApplication::translate("main", "Heiran"), problem);
      return 1;
    }
  }
  recordStartup("opengl-check");

  fontWarmup.waitForFinished();
  recordStartup("font-ready");

  // .hrn を 1 つ渡された場合はセッションとして開く。それ以外は画像として開く。
  // 関連付けから複数選択で起動された場合は引数が複数並ぶ。
  std::optional<QString> sessionPath;
  QStringList imagePaths;
  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.size() == 2 && arguments.at(1).endsWith(".hrn", Qt::CaseInsensitive)) {
    sessionPath = arguments.at(1);
  } else {
    imagePaths = arguments.mid(1);
  }

  heiran::app::ApplicationController applicationController;
  const auto startupResult = applicationController.startup(sessionPath);
  recordStartup("session-load");

  heiran::ui::MainWindow mainWindow;
  recordStartup("window-construction");
  mainWindow.setOpenSessionHandler([&applicationController, &mainWindow](const QString &path) {
    const auto openResult = applicationController.openSession(path);
    if (!openResult.ok) {
      mainWindow.setStartupMessage(
          QCoreApplication::translate("main", "Failed to load %1.").arg(path));
      mainWindow.reportSessionIssues(
          {.failure = QCoreApplication::translate("main", "The .hrn file could not be loaded."),
           .target = path,
           .nextStep = QCoreApplication::translate(
               "main", "Fix the listed settings, or open another file."),
           .issues = openResult.issues,
           .truncated = openResult.issuesTruncated,
           .fatal = true});
      return;
    }

    mainWindow.setSession(openResult.session);
    mainWindow.setStartupMessage(openResult.userMessage);
    mainWindow.setRecentSessions(applicationController.recentSessionPaths());
    if (!openResult.issues.isEmpty()) {
      mainWindow.reportSessionIssues(
          {.failure = QCoreApplication::translate(
               "main", "The .hrn file was loaded with some settings replaced by defaults."),
           .target = path,
           .nextStep = QCoreApplication::translate(
               "main", "Review the listed settings if the result looks unexpected."),
           .issues = openResult.issues,
           .truncated = openResult.issuesTruncated,
           .fatal = false});
    }
  });
  mainWindow.setSaveSessionHandler(
      [&applicationController, &mainWindow](const heiran::domain::SessionModel &session,
                                            const QString &path) {
        const auto saveResult = applicationController.saveSession(session, path);
        if (!saveResult.ok) {
          mainWindow.setStartupMessage(
              QCoreApplication::translate("main", "Failed to save %1.").arg(path));
          mainWindow.reportSessionIssues(
              {.failure = QCoreApplication::translate("main", "The session could not be saved."),
               .target = path,
               .nextStep = QCoreApplication::translate(
                   "main", "Check that the location is writable, then save again."),
               .issues = {{heiran::infra::HrnIssueSeverity::Error, path, saveResult.userMessage}},
               .truncated = false,
               .fatal = false});
          return;
        }

        mainWindow.setSession(saveResult.session);
        mainWindow.setStartupMessage(saveResult.userMessage);
        mainWindow.setRecentSessions(applicationController.recentSessionPaths());
        QMessageBox::information(&mainWindow, QCoreApplication::translate("main", "Save Complete"),
                                 QCoreApplication::translate("main", "Saved %1.").arg(path));
      });
  mainWindow.setSession(startupResult.session);
  mainWindow.setStartupMessage(startupResult.startupMessage);
  mainWindow.setRecentSessions(applicationController.recentSessionPaths());
  if (startupResult.loadFailed) {
    mainWindow.reportSessionIssues(
        {.failure = QCoreApplication::translate(
             "main", "The .hrn file given on the command line could not be loaded."),
         .target = sessionPath.value_or(QString()),
         .nextStep =
             QCoreApplication::translate("main", "Fix the listed settings, or open another file."),
         .issues = startupResult.issues,
         .truncated = startupResult.issuesTruncated,
         .fatal = true});
  } else if (!startupResult.issues.isEmpty()) {
    mainWindow.reportSessionIssues(
        {.failure = QCoreApplication::translate(
             "main", "The .hrn file was loaded with some settings replaced by defaults."),
         .target = sessionPath.value_or(QString()),
         .nextStep = QCoreApplication::translate(
             "main", "Review the listed settings if the result looks unexpected."),
         .issues = startupResult.issues,
         .truncated = startupResult.issuesTruncated,
         .fatal = false});
  }
  if (!imagePaths.isEmpty()) {
    mainWindow.openImageFiles(imagePaths);
  }
  recordStartup("session-apply");
  bool firstFrameRecorded = false;
  const auto *canvas = mainWindow.findChild<QOpenGLWidget *>();
  if (profileStartup && canvas != nullptr) {
    QObject::connect(canvas, &QOpenGLWidget::frameSwapped, &mainWindow, [&]() {
      if (!firstFrameRecorded) {
        firstFrameRecorded = true;
        recordStartup("first-frame");
      }
    });
  }
#ifdef HEIRAN_STARTUP_BENCHMARK
  mainWindow.setAttribute(Qt::WA_ShowWithoutActivating);
#endif
  mainWindow.show();
  recordStartup("window-show");
  if (profileStartup) {
    QTimer::singleShot(0, &app, [&]() { recordStartup("event-loop"); });
  }
#ifdef HEIRAN_STARTUP_BENCHMARK
  QTimer::singleShot(250, &app, &QCoreApplication::quit);
#endif

  const int result = app.exec();
#ifdef HEIRAN_STARTUP_BENCHMARK
  if (!firstFrameRecorded) {
    qWarning("The first frame was not presented; the startup measurement is incomplete.");
    return 1;
  }
#endif
  return result;
}
