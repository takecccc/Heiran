#include "heiran/app/FolderComparison.hpp"
#include "heiran/domain/SessionPages.hpp"
#include "heiran/ui/DecodeCache.hpp"
#include "heiran/ui/DecodeScheduler.hpp"
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
  void anUnreadableRootDoesNotMakeTheOtherFilesLeftOnly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile left(directory.filePath("image.png"));
    QVERIFY(left.open(QIODevice::WriteOnly));
    left.close();
    const auto result =
        heiran::app::compareFolders(directory.path(), directory.filePath("missing"));
    QCOMPARE(result.entries.size(), 1);
    QCOMPARE(result.entries.first().status, heiran::app::FileComparisonStatus::Unreadable);
    QCOMPARE(result.unreadableDirectories.size(), 1);
  }

  void comparesFolderEntriesByRelativePathAndFileContents() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath("left/sub"));
    QVERIFY(root.mkpath("right/sub"));
    const auto write = [&](const QString &path, const QByteArray &data) {
      QFile file(root.filePath(path));
      return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    QVERIFY(write("left/sub/image.png", "before"));
    QVERIFY(write("right/sub/image.png", "after!"));
    QVERIFY(write("left/same.txt", "same"));
    QVERIFY(write("right/same.txt", "same"));
    QVERIFY(write("left/left.txt", "left"));
    QVERIFY(write("right/right.txt", "right"));
    QVERIFY(root.mkpath("left/.git"));
    QVERIFY(write("left/.git/config", "ignored"));

    using heiran::app::FileComparisonStatus;
    const auto result = heiran::app::compareFolders(root.filePath("left"), root.filePath("right"));
    QCOMPARE(result.entries.size(), 4);
    QCOMPARE(result.entries[0].status, FileComparisonStatus::LeftOnly);
    QCOMPARE(result.entries[1].status, FileComparisonStatus::RightOnly);
    QCOMPARE(result.entries[2].status, FileComparisonStatus::Identical);
    QCOMPARE(result.entries[3].relativePath, "sub/image.png");
    QCOMPARE(result.entries[3].status, FileComparisonStatus::Different);
    QVERIFY(result.entries[3].image);
    QVERIFY(!result.entries[2].image);
    QVERIFY(result.unreadableDirectories.isEmpty());
  }

  void comparesDecodedRgbaIndependentlyOfImageStorageFormat() {
    heiran::infra::DecodedImage left;
    heiran::infra::DecodedImage right;
    left.displayImage = QImage(2, 1, QImage::Format_RGB32);
    left.displayImage.fill(Qt::red);
    right.displayImage = left.displayImage.convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(heiran::app::equalDecodedPixels(left, right));
    right.displayImage.setPixelColor(0, 0, QColor(255, 0, 0, 127));
    QVERIFY(!heiran::app::equalDecodedPixels(left, right));
    left.displayImage = left.displayImage.convertToFormat(QImage::Format_RGBA8888);
    left.displayImage.setPixelColor(0, 0, QColor(200, 0, 0, 0));
    right.displayImage.setPixelColor(0, 0, QColor(0, 200, 0, 0));
    QVERIFY(!heiran::app::equalDecodedPixels(left, right));
    QVERIFY(heiran::app::equalDecodedPixels(left, right, true));
    right.displayImage.setPixelColor(0, 0, QColor(0, 200, 0, 1));
    QVERIFY(!heiran::app::equalDecodedPixels(left, right, true));
  }

  void queuesOnlyTheInitialPageAndDeduplicatesCells() {
    QTemporaryDir directory;
    QFile first(directory.filePath("image1.png"));
    QVERIFY(first.open(QIODevice::WriteOnly));
    first.close();
    QFile second(directory.filePath("image2.png"));
    QVERIFY(second.open(QIODevice::WriteOnly));
    second.close();
    StartupPreparationController preparation(std::nullopt, {second.fileName(), second.fileName()},
                                             directory.path());
    const auto prepared = preparation.takeResult();
    auto scheduler = preparation.takeDecodeScheduler();
    QVERIFY(scheduler != nullptr);
    auto ready = scheduler->activate();
    if (ready.isEmpty()) {
      const auto key = heiran::ui::DecodeCache::makeKey(second.fileName(), true);
      std::optional<heiran::ui::DecodeCompletion> received;
      QTRY_VERIFY(received.has_value() || (received = scheduler->takeCompleted(key)).has_value());
      QCOMPARE(received->filePath, second.fileName());
    } else {
      QCOMPARE(ready.size(), 1);
      QCOMPARE(ready.first().filePath, second.fileName());
    }
    QCOMPARE(prepared.images->initialPage, 1);
  }

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

  void preparesOnlyTheTwoExplicitDiffFilesInArgumentOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir subdirectory(directory.path());
    QVERIFY(subdirectory.mkdir("temp revisions"));
    const QString left = "temp revisions/change 2.png";
    const QString right = "temp revisions/change 1.png";
    for (const auto &relative : {left, right, QString("temp revisions/change 3.png")}) {
      QFile file(directory.filePath(relative));
      QVERIFY(file.open(QIODevice::WriteOnly));
    }

    StartupPreparationController preparation(
        std::nullopt, {left, right}, directory.path(), true, false,
        heiran::ui::StartupDiffTitles{.left = "Previous revision", .right = {}});
    const auto result = preparation.takeResult();
    QVERIFY(result.images.has_value());
    QVERIFY(result.images->externalDiff);
    const auto &session = result.images->session;
    QCOMPARE(session.viewer.layout.rows, 1);
    QCOMPARE(session.viewer.layout.cols, 3);
    QCOMPARE(session.images.size(), 2);
    QCOMPARE(session.images[0].label, "Previous revision");
    QCOMPARE(session.images[1].label, "change 1.png");
    QCOMPARE(heiran::domain::cellPageAt(session, 0, 0).path, directory.filePath(left));
    QCOMPARE(heiran::domain::cellPageAt(session, 1, 0).path, directory.filePath(right));
    QCOMPARE(heiran::domain::totalPageCount(session), 1);
    QVERIFY(session.cells[2].diff.has_value());
    QCOMPARE(session.cells[2].diff->baseCell, 0);
    QCOMPARE(session.cells[2].diff->targetCell, 1);
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
    QVERIFY(!preparation.takeDecodeScheduler());
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
