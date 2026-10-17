#include "heiran/domain/ChannelMode.hpp"
#include "heiran/infra/HrnConfigLoader.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// テスト用の .hrn を一時ファイルへ書き出して読み込む。
heiran::infra::HrnLoadResult loadSource(const QString &source) {
  QTemporaryDir tempDir;
  if (!tempDir.isValid()) {
    return {};
  }

  const QString filePath = QDir(tempDir.path()).filePath("case.hrn");
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return {};
  }
  file.write(source.toUtf8());
  file.close();

  return heiran::infra::HrnConfigLoader().loadFromFile(filePath);
}

}  // namespace

class HrnConfigLoaderTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void loadsJson5LikeConfig() {
    QDir tempRoot(QDir::temp().filePath("heiran_hrn_loader_test"));
    tempRoot.removeRecursively();
    QVERIFY(QDir().mkpath(tempRoot.path()));

    const QString filePath = tempRoot.filePath("sample.hrn");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"HRN(
{
  // comment
  format_version: 1,
  defaults: {
    scale: 1.25,
    sync: false,
    label_visible: false,
  },
  viewer: {
    layout: {
      rows: 2,
      cols: 2,
      order: 'row-major',
    },
    page_sort: 'natural',
  },
  images: [
    {
      name: 'Image A',
      label: 'Image A {width} x {height}',
      path: 'images/a/*.png',
      page_offset: 2,
    },
  ],
  cells: [
    {
      image: 0,
      sync: true,
      scale: 1.75,
      gamma: 1.25,
      rotation: 1,
      flip_x: true,
      flip_y: false,
      invert: true,
      ch: 'green',
    },
  ],
}
)HRN");
    file.close();

    heiran::infra::HrnConfigLoader loader;
    const auto result = loader.loadFromFile(filePath);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.document.formatVersion, 1);
    QCOMPARE(result.document.viewer.layout.rows, 2);
    QCOMPARE(result.document.viewer.layout.cols, 2);
    QCOMPARE(result.document.viewer.layout.order, QString("row-major"));
    QCOMPARE(result.document.viewer.pageSort, QString("natural"));
    QCOMPARE(result.document.images.size(), 1);
    QCOMPARE(result.document.images.at(0).name, QString("Image A"));
    QCOMPARE(result.document.images.at(0).label, QString("Image A {width} x {height}"));
    QCOMPARE(result.document.images.at(0).path, QString("images/a/*.png"));
    QCOMPARE(result.document.images.at(0).scale, 1.25);
    QCOMPARE(result.document.images.at(0).pageOffset, 2);
    QCOMPARE(result.document.images.at(0).labelVisible, false);
    QCOMPARE(result.document.cells.size(), 1);
    QCOMPARE(result.document.cells.at(0).imageIndex, 0);
    QCOMPARE(result.document.cells.at(0).sync, true);
    QCOMPARE(result.document.cells.at(0).scale, 1.75);
    QCOMPARE(result.document.cells.at(0).gamma, 1.25);
    QCOMPARE(result.document.cells.at(0).rotation, 1);
    QCOMPARE(result.document.cells.at(0).flipX, true);
    QCOMPARE(result.document.cells.at(0).flipY, false);
    QCOMPARE(result.document.cells.at(0).invert, true);
    QCOMPARE(result.document.cells.at(0).channel, heiran::domain::ChannelMode::Green);

    tempRoot.removeRecursively();
  }

  // 色空間の指定を読めること。既定値からも引き継げること。
  void readsTheColorSpaceSpec() {
    const auto result = loadSource(R"(
{
  format_version: 1,
  defaults: { color_space: 'srgb' },
  images: [
    { name: 'a', path: 'a/*.png' },
    { name: 'b', path: 'b/*.exr', color_space: 'srgb-linear' },
    { name: 'c', path: 'c/*.png', icc_profile: './profiles/custom.icc' },
  ],
  cells: [{ image: 0 }, { image: 1 }, { image: 2 }],
  viewer: { layout: { rows: 1, cols: 3 } },
}
)");

    QVERIFY(result.ok);
    QCOMPARE(result.document.images.size(), 3);
    // 指定が無ければ既定を引き継ぐ。
    QCOMPARE(result.document.images.at(0).colorSpaceName, QString("srgb"));
    QCOMPARE(result.document.images.at(1).colorSpaceName, QString("srgb-linear"));
    QCOMPARE(result.document.images.at(2).iccProfilePath, QString("./profiles/custom.icc"));
  }

  // 指定が無い場合は空のままにすること。空は「埋め込みに従う」を表す。
  void leavesTheColorSpaceEmptyWhenUnspecified() {
    const auto result = loadSource(R"(
{
  format_version: 1,
  images: [{ name: 'a', path: 'a/*.png' }],
  cells: [{ image: 0 }],
  viewer: { layout: { rows: 1, cols: 1 } },
}
)");

    QVERIFY(result.ok);
    QVERIFY(result.document.images.at(0).colorSpaceName.isEmpty());
    QVERIFY(result.document.images.at(0).iccProfilePath.isEmpty());
  }

  void reportsUnsupportedFormatVersionAsError() {
    const auto result = loadSource(R"HRN(
{
  format_version: 2,
  images: [ { name: 'A', path: 'a/*.png' } ],
}
)HRN");

    QVERIFY(!result.ok);
    QVERIFY(result.hasErrors());
    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.issues.at(0).location, QString("format_version"));
  }

  void rejectsFormatVersionBelowOne() {
    const auto result = loadSource(R"HRN(
{
  format_version: 0,
  images: [ { name: 'A', path: 'a/*.png' } ],
}
)HRN");

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.at(0).location, QString("format_version"));
  }

  void rejectsNonNumericFormatVersion() {
    const auto result = loadSource(R"HRN(
{
  format_version: 'one',
  images: [ { name: 'A', path: 'a/*.png' } ],
}
)HRN");

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.at(0).location, QString("format_version"));
  }

  void rejectsLayoutBeyondCellLimit() {
    const auto result = loadSource(R"HRN(
{
  viewer: { layout: { rows: 8, cols: 8 } },
  images: [ { name: 'A', path: 'a/*.png' } ],
}
)HRN");

    QVERIFY(!result.ok);
    QVERIFY(result.hasErrors());
  }

  void reportsMissingRequiredImageKeys() {
    const auto result = loadSource(R"HRN(
{
  images: [ { label: 'no name and no path' } ],
}
)HRN");

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.size(), 2);
    QCOMPARE(result.issues.at(0).location, QString("images[0].name"));
    QCOMPARE(result.issues.at(1).location, QString("images[0].path"));
  }

  void treatsOutOfRangeValuesAsWarningsAndKeepsLoading() {
    const auto result = loadSource(R"HRN(
{
  images: [ { name: 'A', path: 'a/*.png' } ],
  cells: [
    { image: 0, rotation: 7, gamma: -1.0, ch: 'magenta' },
  ],
}
)HRN");

    // 既定値へ読み替えられるものは警告に留め、読込は成功させる。
    QVERIFY(result.ok);
    QVERIFY(!result.hasErrors());
    QCOMPARE(result.issues.size(), 3);
    for (const auto &issue : result.issues) {
      QCOMPARE(issue.severity, heiran::infra::HrnIssueSeverity::Warning);
    }
    QCOMPARE(result.document.cells.at(0).rotation, 0);
    QCOMPARE(result.document.cells.at(0).gamma, 1.0);
    QCOMPARE(result.document.cells.at(0).channel, heiran::domain::ChannelMode::Color);
  }

  void warnsWhenCellReferencesMissingImage() {
    const auto result = loadSource(R"HRN(
{
  images: [ { name: 'A', path: 'a/*.png' } ],
  cells: [ { image: 5 } ],
}
)HRN");

    QVERIFY(result.ok);
    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.issues.at(0).severity, heiran::infra::HrnIssueSeverity::Warning);
    QCOMPARE(result.document.cells.at(0).imageIndex, -1);
  }

  void collectsIssuesAcrossItemsInsteadOfStoppingAtTheFirst() {
    const auto result = loadSource(R"HRN(
{
  images: [
    { path: 'a/*.png' },
    { name: 'B' },
    { name: 'C', path: 'c/*.png' },
  ],
}
)HRN");

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.size(), 2);
    QCOMPARE(result.issues.at(0).location, QString("images[0].name"));
    QCOMPARE(result.issues.at(1).location, QString("images[1].path"));
    // 問題のない項目は読み込まれている。
    QCOMPARE(result.document.images.size(), 1);
    QCOMPARE(result.document.images.at(0).name, QString("C"));
  }

  void capsIssueCountAndMarksTruncation() {
    QString source = "{\n  images: [\n";
    for (int index = 0; index < heiran::infra::kMaxHrnIssues + 5; ++index) {
      source += "    { label: 'broken' },\n";
    }
    source += "  ],\n}\n";

    const auto result = loadSource(source);

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.size(), heiran::infra::kMaxHrnIssues);
    QVERIFY(result.issuesTruncated);
    QVERIFY(result.errorMessage.contains("Only the first"));
  }

  void summarizesErrorAndWarningCounts() {
    const auto result = loadSource(R"HRN(
{
  images: [ { name: 'A', path: 'a/*.png' }, { name: 'B' } ],
  cells: [ { image: 0, rotation: 9 } ],
}
)HRN");

    QVERIFY(result.errorMessage.contains("1 error(s)"));
    QVERIFY(result.errorMessage.contains("1 warning(s)"));
  }

  void reportsParseErrorWithLocation() {
    const auto result = loadSource("{ images: [ ");

    QVERIFY(!result.ok);
    QCOMPARE(result.issues.size(), 1);
    QVERIFY(result.issues.at(0).detail.contains("Parse error"));
  }

  // セルの diff。省略した target は、そのセル自身の画像を指す。値は既定で埋まる。
  void readsACellDiff() {
    const auto result = loadSource(R"(
{
  format_version: 1,
  images: [{ name: 'a', path: 'a/*.png' }, { name: 'b', path: 'b/*.png' }],
  cells: [
    { image: 0 },
    { image: 1, diff: { base: 0, mode: 'signed', view: 'overlay', gain: 2.5, threshold: 4,
                        auto_range: false } },
    { diff: { base: 0, target: 1 } },
  ],
  viewer: { layout: { rows: 1, cols: 3 } },
}
)");
    QVERIFY(result.ok);
    QVERIFY(result.issues.isEmpty());
    QVERIFY(!result.document.cells.at(0).diff);

    const auto &own = result.document.cells.at(1).diff;
    QVERIFY(own);
    QCOMPARE(own->baseCell, 0);
    QCOMPARE(own->targetCell, -1);  // 自身の画像
    QCOMPARE(own->settings.mode, heiran::domain::DiffMode::Signed);
    QCOMPARE(own->settings.view, heiran::domain::DiffViewMode::Overlay);
    QCOMPARE(own->settings.gain, 2.5);
    QCOMPARE(own->settings.threshold, 4.0);
    QCOMPARE(own->settings.autoRange, false);

    const auto &dedicated = result.document.cells.at(2).diff;
    QVERIFY(dedicated);
    QCOMPARE(dedicated->targetCell, 1);
    QCOMPARE(dedicated->settings, heiran::domain::DiffSettings{});  // 既定
  }

  // 不正な diff は、警告を出して捨てる (読み込みは失敗させない)。
  void dropsAnInvalidCellDiffWithAWarning() {
    const auto result = loadSource(R"(
{
  format_version: 1,
  images: [{ name: 'a', path: 'a/*.png' }],
  cells: [
    { image: 0, diff: { base: 0 } },
    { image: 0, diff: { base: 9 } },
    { diff: { base: 0 } },
    { image: 0, diff: { base: 0, target: 0 } },
    { image: 0, diff: 'x' },
  ],
  viewer: { layout: { rows: 1, cols: 5 } },
}
)");
    QVERIFY(result.ok);
    for (int index = 0; index < 5; ++index) {
      QVERIFY2(!result.document.cells.at(index).diff, qPrintable(QString::number(index)));
    }
    QCOMPARE(result.issues.size(), 5);
    QCOMPARE(result.issues.at(0).location, QString("cells[0].diff.base"));  // 自分自身
    QCOMPARE(result.issues.at(1).location, QString("cells[1].diff.base"));  // 範囲外
    QCOMPARE(result.issues.at(2).location,
             QString("cells[2].diff.target"));  // 画像が無く target も無い
  }

  void formatsIssueForDisplay() {
    const heiran::infra::HrnIssue issue{heiran::infra::HrnIssueSeverity::Warning,
                                        "cells[1].rotation", "Using 0."};
    QCOMPARE(issue.toDisplayText(), QString("Warning  cells[1].rotation: Using 0."));
  }
};

QTEST_MAIN(HrnConfigLoaderTests)

#include "HrnConfigLoaderTests.moc"
