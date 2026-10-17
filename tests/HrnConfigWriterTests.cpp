#include "heiran/domain/ChannelMode.hpp"
#include "heiran/infra/HrnConfigLoader.hpp"
#include "heiran/infra/HrnConfigWriter.hpp"

#include <QDir>
#include <QFile>
#include <QtTest>

class HrnConfigWriterTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void savesAndReloadsSession() {
    QDir tempRoot(QDir::temp().filePath("heiran_hrn_writer_test"));
    tempRoot.removeRecursively();
    QVERIFY(QDir().mkpath(tempRoot.path()));

    heiran::domain::SessionDocument document;
    document.formatVersion = 1;
    document.viewer.layout.rows = 2;
    document.viewer.layout.cols = 1;
    document.viewer.layout.order = "row-major";
    document.viewer.pageSort = "natural";
    document.images = {
        {.name = "Image A",
         .label = "Image A {filesize:MB}",
         .path = "images/a/*.png",
         .scale = 1.5,
         .pageOffset = 2,
         .labelVisible = true,
         .honorExifOrientation = true},
        {.name = "Image B",
         .label = "",
         .path = "images/b/*.png",
         .scale = 0.75,
         .pageOffset = 0,
         .labelVisible = false,
         .honorExifOrientation = false},
    };
    document.cells = {
        {.imageIndex = 0,
         .sync = true,
         .scale = 1.5,
         .gamma = 1.0,
         .rotation = 0,
         .flipX = false,
         .flipY = false,
         .invert = false,
         .channel = heiran::domain::ChannelMode::Color},
        {.imageIndex = 1,
         .sync = false,
         .scale = 0.9,
         .gamma = 1.4,
         .rotation = 3,
         .flipX = true,
         .flipY = false,
         .invert = true,
         .channel = heiran::domain::ChannelMode::Blue},
    };

    const QString filePath = tempRoot.filePath("saved.hrn");
    heiran::infra::HrnConfigWriter writer;
    const auto saveResult = writer.saveToFile(document, filePath);
    QVERIFY2(saveResult.ok, qPrintable(saveResult.errorMessage));

    heiran::infra::HrnConfigLoader loader;
    const auto loadResult = loader.loadFromFile(filePath);
    QVERIFY2(loadResult.ok, qPrintable(loadResult.errorMessage));
    QCOMPARE(loadResult.document.images.size(), 2);
    QCOMPARE(loadResult.document.images.at(0).name, QString("Image A"));
    QCOMPARE(loadResult.document.images.at(0).label, QString("Image A {filesize:MB}"));
    QCOMPARE(loadResult.document.images.at(0).scale, 1.5);
    QCOMPARE(loadResult.document.images.at(0).pageOffset, 2);
    QCOMPARE(loadResult.document.images.at(1).labelVisible, false);
    QCOMPARE(loadResult.document.images.at(1).honorExifOrientation, false);
    QCOMPARE(loadResult.document.cells.size(), 2);
    QCOMPARE(loadResult.document.cells.at(1).sync, false);
    QCOMPARE(loadResult.document.cells.at(1).scale, 0.9);
    QCOMPARE(loadResult.document.cells.at(1).gamma, 1.4);
    QCOMPARE(loadResult.document.cells.at(1).rotation, 3);
    QCOMPARE(loadResult.document.cells.at(1).flipX, true);
    QCOMPARE(loadResult.document.cells.at(1).flipY, false);
    QCOMPARE(loadResult.document.cells.at(1).invert, true);
    QCOMPARE(loadResult.document.cells.at(1).channel, heiran::domain::ChannelMode::Blue);

    tempRoot.removeRecursively();
  }

  // セルの diff を保存して、読み直しても同じになること。
  void savesAndReloadsACellDiff() {
    QDir tempRoot(QDir::temp().filePath("heiran_hrn_writer_diff_test"));
    tempRoot.removeRecursively();
    QVERIFY(QDir().mkpath(tempRoot.path()));

    heiran::domain::SessionDocument document;
    document.viewer.layout.rows = 1;
    document.viewer.layout.cols = 3;
    document.images = {{.name = "A", .path = "a/*.png"}, {.name = "B", .path = "b/*.png"}};
    heiran::domain::DiffSettings settings;
    settings.mode = heiran::domain::DiffMode::Binary;
    settings.view = heiran::domain::DiffViewMode::SignedColor;
    settings.gain = 3.0;
    settings.threshold = 12.5;
    settings.autoRange = false;
    settings.sizeMatch = heiran::domain::DiffSizeMatch::ToLarger;
    heiran::domain::DiffSettings colorSettings;
    colorSettings.mode = heiran::domain::DiffMode::DeltaE;
    colorSettings.threshold = 2.3;
    document.cells = {
        {.imageIndex = 0},
        {.imageIndex = 1,
         .diff = heiran::domain::CellDiff{.baseCell = 0, .targetCell = -1, .settings = settings}},
        {.imageIndex = -1,
         .diff = heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1, .settings = colorSettings}}};

    const QString filePath = tempRoot.filePath("diff.hrn");
    const auto saveResult = heiran::infra::HrnConfigWriter().saveToFile(document, filePath);
    QVERIFY2(saveResult.ok, qPrintable(saveResult.errorMessage));
    const auto loadResult = heiran::infra::HrnConfigLoader().loadFromFile(filePath);
    QVERIFY2(loadResult.ok, qPrintable(loadResult.errorMessage));
    QVERIFY(loadResult.issues.isEmpty());
    QVERIFY(!loadResult.document.cells.at(0).diff);
    QCOMPARE(loadResult.document.cells.at(1).diff, document.cells.at(1).diff);
    QCOMPARE(loadResult.document.cells.at(2).diff, document.cells.at(2).diff);

    tempRoot.removeRecursively();
  }
  // ワイプの切り方と位置が、保存して読み直しても残る。
  void savesAndReloadsAWipeCell() {
    QDir tempRoot(QDir::temp().filePath("heiran_hrn_writer_wipe_test"));
    tempRoot.removeRecursively();
    QVERIFY(QDir().mkpath(tempRoot.path()));

    heiran::domain::SessionDocument document;
    document.viewer.layout.rows = 1;
    document.viewer.layout.cols = 3;
    document.images = {{.name = "A", .path = "a/*.png"}, {.name = "B", .path = "b/*.png"}};
    heiran::domain::DiffSettings settings;
    settings.mode = heiran::domain::DiffMode::Wipe;
    settings.wipeOrientation = heiran::domain::WipeOrientation::TopBottom;
    settings.wipePosition = 0.375;
    document.cells = {{.imageIndex = 0},
                      {.imageIndex = 1},
                      {.imageIndex = -1,
                       .diff = heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1, .settings = settings}}};

    const QString filePath = tempRoot.filePath("wipe.hrn");
    QVERIFY(heiran::infra::HrnConfigWriter().saveToFile(document, filePath).ok);
    const auto loadResult = heiran::infra::HrnConfigLoader().loadFromFile(filePath);
    QVERIFY2(loadResult.ok, qPrintable(loadResult.errorMessage));
    QVERIFY(loadResult.issues.isEmpty());
    QCOMPARE(loadResult.document.cells.at(2).diff, document.cells.at(2).diff);
    QCOMPARE(loadResult.document.cells.at(2).diff->settings.wipePosition, 0.375);

    tempRoot.removeRecursively();
  }
};

QTEST_MAIN(HrnConfigWriterTests)

#include "HrnConfigWriterTests.moc"
