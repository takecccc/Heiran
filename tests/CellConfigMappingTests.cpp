#include "heiran/domain/CellConfigMapping.hpp"

#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::CellViewState;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;

namespace {

CellConfig makeConfig() {
  CellConfig config;
  config.imageIndex = 0;
  config.sync = false;
  config.scale = 2.5;
  config.gamma = 1.8;
  config.rotation = 3;
  config.flipX = true;
  config.flipY = true;
  config.invert = true;
  config.channel = heiran::domain::ChannelMode::Red;
  return config;
}

SessionModel makeSession(bool honorExifOrientation = true) {
  SessionModel session;
  ImageSet imageSet;
  imageSet.honorExifOrientation = honorExifOrientation;
  session.images = {imageSet};
  session.cells = {makeConfig()};
  return session;
}

}  // namespace

class CellConfigMappingTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void cellConfigAtReturnsTheConfiguredCell() {
    const auto session = makeSession();
    QCOMPARE(heiran::domain::cellConfigAt(session, 0).scale, 2.5);
  }

  // 範囲外のセルは既定の設定になること。格子がセル数より大きい場合に起こる。
  void cellConfigAtReturnsDefaultsOutsideTheCells() {
    const auto session = makeSession();
    QCOMPARE(heiran::domain::cellConfigAt(session, 1).imageIndex, -1);
    QCOMPARE(heiran::domain::cellConfigAt(session, -1).scale, 1.0);
  }

  void displayFieldsCopyOnlyTheDisplayValues() {
    CellViewState state;
    state.syncEnabled = true;
    state.offsetDeltaX = 4.0;
    state.honorExifOrientation = false;

    heiran::domain::applyCellConfigDisplayFields(makeConfig(), &state);

    QCOMPARE(state.scale, 2.5);
    QCOMPARE(state.gamma, 1.8);
    QCOMPARE(state.rotationQuarterTurns, 3);
    QVERIFY(state.flipX);
    QVERIFY(state.flipY);
    QVERIFY(state.invert);
    // リセットで消えてはいけないもの。
    QVERIFY(state.syncEnabled);
    QCOMPARE(state.offsetDeltaX, 4.0);
    QVERIFY(!state.honorExifOrientation);
  }

  // パンのずれを触らないこと。設定の反映でずれが消えると、開いたまま動かした位置が戻る。
  void applyToViewStateKeepsThePanOffset() {
    CellViewState state;
    state.offsetDeltaX = 4.0;
    state.offsetDeltaY = 5.0;

    heiran::domain::applyCellConfigToViewState(makeSession(), 0, &state);

    QVERIFY(!state.syncEnabled);
    QCOMPARE(state.scale, 2.5);
    QCOMPARE(state.offsetDeltaX, 4.0);
    QCOMPARE(state.offsetDeltaY, 5.0);
  }

  void applyToViewStateFollowsTheImageSetExifSetting() {
    CellViewState state;
    heiran::domain::applyCellConfigToViewState(makeSession(false), 0, &state);
    QVERIFY(!state.honorExifOrientation);

    heiran::domain::applyCellConfigToViewState(makeSession(true), 0, &state);
    QVERIFY(state.honorExifOrientation);
  }

  // 割り当て無しのセルは EXIF の向きを見ること。
  void applyToViewStateHonorsExifWithoutAnImageSet() {
    CellViewState state;
    state.honorExifOrientation = false;
    heiran::domain::applyCellConfigToViewState(SessionModel{}, 0, &state);
    QVERIFY(state.honorExifOrientation);
    QVERIFY(state.syncEnabled);
  }

  void writeBackRoundTripsTheState() {
    CellViewState state;
    heiran::domain::applyCellConfigToViewState(makeSession(), 0, &state);

    CellConfig written;
    heiran::domain::writeCellViewStateToConfig(state, &written);

    QVERIFY(!written.sync);
    QCOMPARE(written.scale, 2.5);
    QCOMPARE(written.gamma, 1.8);
    QCOMPARE(written.rotation, 3);
    QVERIFY(written.flipX);
    QVERIFY(written.flipY);
    QVERIFY(written.invert);
  }

  // 状態に無い値 (割り当てとチャネル) は書き換えないこと。
  void writeBackLeavesAssignmentAndChannelAlone() {
    CellConfig config = makeConfig();
    heiran::domain::writeCellViewStateToConfig(CellViewState{}, &config);

    QCOMPARE(config.imageIndex, 0);
    QCOMPARE(config.channel, heiran::domain::ChannelMode::Red);
    QVERIFY(config.sync);
  }
};

QTEST_APPLESS_MAIN(CellConfigMappingTests)

#include "CellConfigMappingTests.moc"
