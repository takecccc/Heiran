#include "heiran/ui/StartupPreparationController.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using heiran::ui::StartupPreparationController;

class StartupPreparationTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() {
    QVERIFY(settingsDirectory_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
  }

  void preparesTheSelectedPageWithoutWritingHistory_data() {
    QTest::addColumn<bool>("parallel");
    QTest::newRow("parallel") << true;
    QTest::newRow("sequential") << false;
  }

  void preparesTheSelectedPageWithoutWritingHistory() {
    QFETCH(bool, parallel);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (const auto &name : {"image1.png", "image2.png", "image10.png"}) {
      const QString path = directory.filePath(name);
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      paths.push_back(path);
    }
    const auto history = QSettings().value("recent_images");
    StartupPreparationController preparation(std::nullopt, {paths.last()}, directory.path(),
                                             parallel);
    const auto result = preparation.takeResult();
    QVERIFY(result.images.has_value());
    QCOMPARE(result.images->session.images.size(), 1);
    QCOMPARE(result.images->session.images.first().resolvedPaths, paths);
    QCOMPARE(result.images->initialPage, 2);
    QCOMPARE(result.images->session.cells.first().imageIndex, 0);
    QVERIFY(!result.images->session.images.first().labelVisible);
    QCOMPARE(QSettings().value("recent_images"), history);
  }

  void doesNotRememberASessionUntilPreparationIsAccepted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("session.hrn");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{format_version: 1, viewer: {layout: {rows: 1, cols: 1}}, images: [], "
               "cells: [{image: -1}]}");
    file.close();
    const auto history = QSettings().value("recent_sessions");
    StartupPreparationController preparation(path, {}, directory.path());
    const auto result = preparation.takeResult();
    QVERIFY2(!result.startup.loadFailed, qPrintable(result.startup.startupMessage));
    QCOMPARE(result.startup.session.sessionFilePath, path);
    QCOMPARE(QSettings().value("recent_sessions"), history);
    heiran::app::ApplicationController controller;
    controller.completeStartup(result.startup);
    QCOMPARE(controller.recentSessionPaths().first(), path);
  }

  void returnsAnEmptySessionAndIssuesWhenTheSessionIsInvalid() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto history = QSettings().value("recent_sessions");
    StartupPreparationController preparation(directory.filePath("missing.hrn"), {},
                                             directory.path());
    const auto result = preparation.takeResult();
    QVERIFY(result.startup.loadFailed);
    QVERIFY(result.startup.session.images.isEmpty());
    QVERIFY(!result.startup.issues.isEmpty());
    heiran::app::ApplicationController().completeStartup(result.startup);
    QCOMPARE(QSettings().value("recent_sessions"), history);
  }

  void resolvesRelativeArgumentsAgainstTheCapturedDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath("relative.png"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    StartupPreparationController preparation(std::nullopt, {"relative.png"}, directory.path());
    const auto prepared = preparation.takeResult();
    QVERIFY(prepared.images.has_value());
    QCOMPARE(prepared.images->sourcePaths.first(), directory.filePath("relative.png"));
    QCOMPARE(prepared.images->session.images.first().resolvedPaths.first(),
             directory.filePath("relative.png"));
  }

  void preparesTheDefaultSessionWithoutInput() {
    StartupPreparationController preparation(std::nullopt, {}, QDir::currentPath());
    const auto prepared = preparation.takeResult();
    QVERIFY(!prepared.images);
    QVERIFY(!prepared.startup.loadFailed);
    QVERIFY(prepared.startup.session.images.isEmpty());
    QCOMPARE(prepared.startup.session.cells.size(), 1);
  }

  void reportsMalformedSessionIssuesFromTheWorker() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath("invalid.hrn"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{");
    file.close();
    StartupPreparationController preparation(file.fileName(), {}, directory.path());
    const auto prepared = preparation.takeResult();
    QVERIFY(prepared.startup.loadFailed);
    QVERIFY(!prepared.startup.issues.isEmpty());
  }

private:
  QTemporaryDir settingsDirectory_;
};

QTEST_GUILESS_MAIN(StartupPreparationTests)
#include "StartupPreparationTests.moc"
