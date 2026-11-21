#include "heiran/app/ApplicationController.hpp"
#include "heiran/app/CrashReporter.hpp"
#include "heiran/app/Localization.hpp"
#include "heiran/app/OpenGlCheck.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/ImageComparisonWidget.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/StartupPreparationController.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureSynchronizer>
#include <QIcon>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrentRun>

#ifdef HEIRAN_STARTUP_BENCHMARK
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
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
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
  const auto startupSettings = heiran::ui::AppSettings::load();
  heiran::app::installTranslators(
      app, qEnvironmentVariable("HEIRAN_LANGUAGE", startupSettings.language));
  heiran::infra::setAnimationStreamingThresholdBytes(
      heiran::ui::resolveAnimationStreamingThresholdBytes(
          startupSettings.animationStreamingThresholdMib,
          qEnvironmentVariableIntValue("HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB")));

  // 落ちたときの状態を残す。まれに 0xC0000005 で落ちる事象を追っており、
  // 再現手順が無いため、起きたときの記録だけが手掛かりになる。
  // 名前の設定より後に呼ぶ。書き出し先が組織名とアプリ名から決まる。
  heiran::app::installCrashReporter();
  QApplication::setWindowIcon(QIcon(":/heiran-256.png"));
  recordStartup("settings-and-localization");

  // .hrn を 1 つ渡された場合はセッションとして開く。それ以外は画像として開く。
  // 関連付けから複数選択で起動された場合は引数が複数並ぶ。
  std::optional<QString> sessionPath;
  std::optional<heiran::ui::StartupDiffTitles> diffTitles;
  QStringList imagePaths;
  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.value(1) == "--diff") {
    QCommandLineParser parser;
    parser.addOptions({QCommandLineOption("diff"), QCommandLineOption("left-title", {}, "title"),
                       QCommandLineOption("right-title", {}, "title")});
    const auto showError = [](const QString &message) {
      QMessageBox::critical(nullptr, QCoreApplication::translate("main", "Heiran"), message);
    };
    if (!parser.parse(arguments) || parser.positionalArguments().size() != 2) {
      showError(QCoreApplication::translate(
          "main", "Image diff requires two file paths.\n"
                  "Usage: heiran --diff [--left-title title] [--right-title title] left right"));
      return 1;
    }
    imagePaths = parser.positionalArguments();
    for (const auto &path : imagePaths) {
      if (!QFileInfo(path).isFile()) {
        showError(
            QCoreApplication::translate("main", "Image file could not be found: %1").arg(path));
        return 1;
      }
    }
    diffTitles = heiran::ui::StartupDiffTitles{.left = parser.value("left-title"),
                                               .right = parser.value("right-title")};
  } else if (arguments.size() == 2 && arguments.at(1).endsWith(".hrn", Qt::CaseInsensitive)) {
    sessionPath = arguments.at(1);
  } else {
    imagePaths = arguments.mid(1);
  }

  bool parallelInputs = true;
  bool decodeAhead = true;
#ifdef HEIRAN_STARTUP_BENCHMARK
  parallelInputs = !qEnvironmentVariableIsSet("HEIRAN_BENCHMARK_SEQUENTIAL_INPUTS");
  decodeAhead = !qEnvironmentVariableIsSet("HEIRAN_BENCHMARK_SEQUENTIAL_DECODE");
#endif
  heiran::ui::StartupPreparationController inputPreparation(
      sessionPath, imagePaths, QDir::currentPath(), parallelInputs, decodeAhead, diffTitles);

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

  const auto showStartupWindow = [&](QWidget &window) {
    recordStartup("session-apply");
    bool firstFrameRecorded = false;
    bool imageFrameRecorded = false;
    QMetaObject::Connection frameConnection;
    const auto *canvas = window.findChild<QOpenGLWidget *>();
    if (profileStartup && canvas != nullptr) {
      frameConnection = QObject::connect(canvas, &QOpenGLWidget::frameSwapped, &window, [&]() {
        if (!firstFrameRecorded) {
          firstFrameRecorded = true;
          recordStartup("first-frame");
        }
        if (!imageFrameRecorded) {
          bool loading = false;
          for (const auto *cell : window.findChildren<heiran::ui::ImageViewWidget *>()) {
            loading = loading || cell->cellStatus() == heiran::ui::CellStatus::Loading;
          }
          if (!loading) {
            imageFrameRecorded = true;
            recordStartup("images-frame");
#ifdef HEIRAN_STARTUP_BENCHMARK
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
#endif
          }
        }
      });
    }
#ifdef HEIRAN_STARTUP_BENCHMARK
    window.setAttribute(Qt::WA_ShowWithoutActivating);
#endif
    window.show();
    recordStartup("window-show");
    if (profileStartup) {
      QTimer::singleShot(0, &app, [&]() { recordStartup("event-loop"); });
    }
#ifdef HEIRAN_STARTUP_BENCHMARK
    QTimer::singleShot(10000, &app, &QCoreApplication::quit);
#endif

    const int result = app.exec();
    QObject::disconnect(frameConnection);
#ifdef HEIRAN_STARTUP_BENCHMARK
    if (!firstFrameRecorded || !imageFrameRecorded) {
      qWarning("The initial image frame was not presented; the startup measurement is incomplete.");
      return 1;
    }
#endif
    return result;
  };

  if (diffTitles) {
    heiran::ui::applyColorTheme(startupSettings.colorTheme, startupSettings.customColorTheme);
    heiran::ui::ImageComparisonWidget comparison;
    comparison.resize(1160, 700);
    QObject::connect(&comparison, &heiran::ui::ImageComparisonWidget::endRequested, &comparison,
                     &QWidget::close);
    recordStartup("window-construction");
    auto prepared = inputPreparation.takeResult();
    auto scheduler = inputPreparation.takeDecodeScheduler();
    recordStartup("session-load");
    comparison.openPreparedImages(std::move(*prepared.images), std::move(scheduler));
    return showStartupWindow(comparison);
  }

  heiran::app::ApplicationController applicationController;
  heiran::ui::MainWindow mainWindow;
  recordStartup("window-construction");
  auto prepared = inputPreparation.takeResult();
  auto startupScheduler = inputPreparation.takeDecodeScheduler();
  const auto &startupResult = prepared.startup;
  applicationController.completeStartup(startupResult);
  recordStartup("session-load");
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
  if (!prepared.images) {
    mainWindow.setSession(startupResult.session, 0, std::move(startupScheduler));
  }
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
  if (prepared.images) {
    mainWindow.openPreparedImages(*prepared.images, std::move(startupScheduler));
  }
  prepared = {};
  return showStartupWindow(mainWindow);
}
