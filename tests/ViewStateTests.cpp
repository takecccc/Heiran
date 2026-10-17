#include "heiran/domain/ViewState.hpp"

#include <QtTest>
#include <cmath>
#include <vector>

class ViewStateTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 同じ表示位置を指す画像座標へ写せること。
  //
  // セルごとに拡大率とオフセットが違うため、同じ画像座標が画面上の同じ場所を
  // 指すとは限らない。選択範囲の重ね描きとカーソルの同期がこれに依存する。
  void mapsImagePointsBetweenCellsByViewPosition() {
    const heiran::domain::FinalViewState referenceState{
        .scale = 2.0, .offsetX = 10.0, .offsetY = 20.0};
    const heiran::domain::FinalViewState targetState{
        .scale = 4.0, .offsetX = 100.0, .offsetY = 200.0};

    const double imageX = 30.0;
    const double mappedX =
        heiran::domain::mapImageXBetweenCells(referenceState, targetState, imageX);

    // 写した先で表示座標が一致すること。値そのものではなく、指す場所を確かめる。
    QCOMPARE(heiran::domain::imageToViewX(targetState, mappedX),
             heiran::domain::imageToViewX(referenceState, imageX));
    // (30 - 10) * 2 = 40 の表示座標。対象では 40 / 4 + 100 = 110。
    QCOMPARE(mappedX, 110.0);

    const double imageY = 25.0;
    const double mappedY =
        heiran::domain::mapImageYBetweenCells(referenceState, targetState, imageY);
    QCOMPARE(heiran::domain::imageToViewY(targetState, mappedY),
             heiran::domain::imageToViewY(referenceState, imageY));
  }

  // 同じ状態どうしなら値が変わらないこと。同期中のセルはこの経路を通る。
  void keepsImagePointsUnchangedBetweenIdenticalCells() {
    const heiran::domain::FinalViewState state{.scale = 1.5, .offsetX = -7.0, .offsetY = 3.0};

    QCOMPARE(heiran::domain::mapImageXBetweenCells(state, state, 42.0), 42.0);
    QCOMPARE(heiran::domain::mapImageYBetweenCells(state, state, -13.0), -13.0);
  }

  // 拡大率が 0 以下なら写しようがない。元の値を返して落ちないこと。
  void returnsTheInputWhenAScaleIsNotUsable() {
    const heiran::domain::FinalViewState usableState{.scale = 2.0, .offsetX = 1.0, .offsetY = 1.0};
    const heiran::domain::FinalViewState brokenState{.scale = 0.0, .offsetX = 5.0, .offsetY = 5.0};

    QCOMPARE(heiran::domain::mapImageXBetweenCells(brokenState, usableState, 9.0), 9.0);
    QCOMPARE(heiran::domain::mapImageXBetweenCells(usableState, brokenState, 9.0), 9.0);
    QCOMPARE(heiran::domain::mapImageYBetweenCells(brokenState, usableState, 9.0), 9.0);
    QCOMPARE(heiran::domain::mapImageYBetweenCells(usableState, brokenState, 9.0), 9.0);
  }

  // 往復させると元へ戻ること。片方向だけ合っていても同期は成り立たない。
  void mapsImagePointsBackAndForth() {
    const heiran::domain::FinalViewState referenceState{
        .scale = 3.0, .offsetX = 12.0, .offsetY = -4.0};
    const heiran::domain::FinalViewState targetState{
        .scale = 0.25, .offsetX = -60.0, .offsetY = 8.0};

    const double imageX = 17.5;
    const double forwardX =
        heiran::domain::mapImageXBetweenCells(referenceState, targetState, imageX);
    const double backX =
        heiran::domain::mapImageXBetweenCells(targetState, referenceState, forwardX);
    QVERIFY2(std::abs(backX - imageX) < 1e-9, qPrintable(QString::number(backX)));
  }

  void computesFinalViewState() {
    heiran::domain::GlobalViewState globalState{
        .baseScale = 2.0,
        .baseOffsetX = 10.0,
        .baseOffsetY = 20.0,
    };
    heiran::domain::CellViewState cellState{
        .syncEnabled = true,
        .offsetDeltaX = 3.0,
        .offsetDeltaY = -2.0,
        .scale = 0.5,
    };

    const auto finalState = heiran::domain::computeFinalViewState(globalState, cellState);
    QCOMPARE(finalState.scale, 1.0);
    QVERIFY(qFuzzyCompare(finalState.offsetX, 26.0));
    QVERIFY(qFuzzyCompare(finalState.offsetY, 36.0));
  }

  void appliesGlobalPan() {
    heiran::domain::GlobalViewState globalState;
    heiran::domain::applyPanToGlobal(&globalState, 4.0, -6.0);

    QCOMPARE(globalState.baseOffsetX, 4.0);
    QCOMPARE(globalState.baseOffsetY, -6.0);
  }

  void appliesCellPan() {
    heiran::domain::CellViewState cellState;
    heiran::domain::applyPanToCell(&cellState, -2.5, 3.5);

    QCOMPARE(cellState.offsetDeltaX, -2.5);
    QCOMPARE(cellState.offsetDeltaY, 3.5);
  }

  void scalesOffsetsByCellScale() {
    heiran::domain::GlobalViewState globalState{
        .baseScale = 1.0,
        .baseOffsetX = 100.0,
        .baseOffsetY = 80.0,
    };
    heiran::domain::CellViewState cellState{
        .syncEnabled = true,
        .offsetDeltaX = 0.0,
        .offsetDeltaY = 0.0,
        .scale = 0.75,
    };

    const auto finalState = heiran::domain::computeFinalViewState(globalState, cellState);
    QVERIFY(qFuzzyCompare(finalState.scale, 0.75));
    QVERIFY(qFuzzyCompare(finalState.offsetX, 133.33333333333334));
    QVERIFY(qFuzzyCompare(finalState.offsetY, 106.66666666666667));
  }

  void appliesZoomAroundAnchor() {
    heiran::domain::GlobalViewState globalState{
        .baseScale = 1.0,
        .baseOffsetX = 0.0,
        .baseOffsetY = 0.0,
    };
    heiran::domain::applyZoom(&globalState, 2.0, 10.0, 20.0);

    QCOMPARE(globalState.baseScale, 2.0);
    QCOMPARE(globalState.baseOffsetX, 5.0);
    QCOMPARE(globalState.baseOffsetY, 10.0);
  }

  // docs/テスト観点.md 座標変換 Case 2: オフセットのみ
  void mapsOffsetOnlyTransform() {
    const heiran::domain::FinalViewState finalState{.scale = 1.0, .offsetX = 10.0, .offsetY = 20.0};

    QCOMPARE(heiran::domain::imageToViewX(finalState, 10.0), 0.0);
    QCOMPARE(heiran::domain::imageToViewY(finalState, 20.0), 0.0);
    QCOMPARE(heiran::domain::imageToViewX(finalState, 11.0), 1.0);
    QCOMPARE(heiran::domain::imageToViewY(finalState, 20.0), 0.0);
  }

  // docs/テスト観点.md 座標変換 Case 3: スケールのみ
  void mapsScaleOnlyTransform() {
    const heiran::domain::FinalViewState finalState{.scale = 2.0};

    QCOMPARE(heiran::domain::imageToViewX(finalState, 1.0), 2.0);
    QCOMPARE(heiran::domain::imageToViewY(finalState, 1.0), 2.0);
  }

  void roundTripsViewAndImageCoordinates() {
    const heiran::domain::FinalViewState finalState{
        .scale = 1.75, .offsetX = -12.5, .offsetY = 33.25};

    for (double imageX = -20.0; imageX < 20.0; imageX += 0.25) {
      const double viewX = heiran::domain::imageToViewX(finalState, imageX);
      QVERIFY(
          qFuzzyCompare(heiran::domain::viewToImageX(finalState, viewX) + 100.0, imageX + 100.0));
    }
  }

  void keepsFlooredPixelAfterRoundTrip() {
    // 逆変換後に floor しても期待する画素へ戻ることを確認する。
    const heiran::domain::FinalViewState finalState{.scale = 2.0, .offsetX = 3.0};

    for (int imagePixel = 0; imagePixel < 16; ++imagePixel) {
      // 画素の中心を通す。境界ちょうどは半開区間の扱いになるため中心で確かめる。
      const double viewX = heiran::domain::imageToViewX(finalState, imagePixel + 0.5);
      const double restored = heiran::domain::viewToImageX(finalState, viewX);
      QCOMPARE(static_cast<int>(std::floor(restored)), imagePixel);
    }
  }

  void treatsNonPositiveScaleAsOffsetOnly() {
    const heiran::domain::FinalViewState finalState{.scale = 0.0, .offsetX = 7.0, .offsetY = 8.0};

    QCOMPARE(heiran::domain::viewToImageX(finalState, 100.0), 7.0);
    QCOMPARE(heiran::domain::viewToImageY(finalState, 100.0), 8.0);
  }

  // docs/テスト観点.md 座標変換 Case 13: 同期有効セルのズーム
  void keepsAnchorPositionWhenZoomingSyncedCell() {
    heiran::domain::GlobalViewState globalState{
        .baseScale = 1.0, .baseOffsetX = 4.0, .baseOffsetY = 6.0};
    const heiran::domain::CellViewState cellState{.syncEnabled = true};

    const double anchorImageX = 25.0;
    const double anchorImageY = 40.0;
    const auto before = heiran::domain::computeFinalViewState(globalState, cellState);
    const double viewXBefore = heiran::domain::imageToViewX(before, anchorImageX);
    const double viewYBefore = heiran::domain::imageToViewY(before, anchorImageY);

    heiran::domain::applyZoom(&globalState, 1.25, anchorImageX, anchorImageY);

    const auto after = heiran::domain::computeFinalViewState(globalState, cellState);
    QVERIFY(qFuzzyCompare(heiran::domain::imageToViewX(after, anchorImageX) + 100.0,
                          viewXBefore + 100.0));
    QVERIFY(qFuzzyCompare(heiran::domain::imageToViewY(after, anchorImageY) + 100.0,
                          viewYBefore + 100.0));
    QVERIFY(globalState.baseScale > 1.0);
  }

  // docs/テスト観点.md 座標変換 Case 14: 同期無効セルのズーム
  void appliesZoomToAllCellsRegardlessOfSyncFlag() {
    heiran::domain::GlobalViewState globalState{.baseScale = 1.0};
    const heiran::domain::CellViewState syncedCell{.syncEnabled = true};
    const heiran::domain::CellViewState unsyncedCell{
        .syncEnabled = false, .offsetDeltaX = 12.0, .offsetDeltaY = -5.0};

    const double scaleBefore = heiran::domain::computeFinalViewState(globalState, syncedCell).scale;

    heiran::domain::applyZoom(&globalState, 2.0, 10.0, 10.0);

    // sync_enabled はズーム反映可否に影響しない。
    QVERIFY(heiran::domain::computeFinalViewState(globalState, syncedCell).scale > scaleBefore);
    QVERIFY(heiran::domain::computeFinalViewState(globalState, unsyncedCell).scale > scaleBefore);
    // ズーム操作だけでは offset_delta を直接更新しない。
    QCOMPARE(unsyncedCell.offsetDeltaX, 12.0);
    QCOMPARE(unsyncedCell.offsetDeltaY, -5.0);
  }

  void keepsAnchorPositionForUnsyncedCell() {
    heiran::domain::GlobalViewState globalState{.baseScale = 1.0};
    const heiran::domain::CellViewState cellState{
        .syncEnabled = false, .offsetDeltaX = 8.0, .offsetDeltaY = 3.0};

    const double anchorImageX = 30.0;
    const auto before = heiran::domain::computeFinalViewState(globalState, cellState);
    const double viewXBefore = heiran::domain::imageToViewX(before, anchorImageX);

    // 固定点は操作対象セルの座標で渡す。
    heiran::domain::applyZoom(&globalState, 1.5, anchorImageX - cellState.offsetDeltaX, 0.0);

    const auto after = heiran::domain::computeFinalViewState(globalState, cellState);
    QVERIFY(qFuzzyCompare(heiran::domain::imageToViewX(after, anchorImageX) + 100.0,
                          viewXBefore + 100.0));
  }

  // docs/テスト観点.md 同期 Case 1: 同期有効セルのパン
  void panOnSyncedCellUpdatesOnlyGlobalOffset() {
    heiran::domain::GlobalViewState globalState;
    heiran::domain::CellViewState otherCell{.syncEnabled = false, .offsetDeltaX = 5.0};
    const double otherOffsetBefore = otherCell.offsetDeltaX;

    heiran::domain::applyPanToGlobal(&globalState, 3.0, -2.0);

    QCOMPARE(globalState.baseOffsetX, 3.0);
    QCOMPARE(globalState.baseOffsetY, -2.0);
    // 同期無効セルの見た目は動かない。
    QCOMPARE(otherCell.offsetDeltaX, otherOffsetBefore);
  }

  // docs/テスト観点.md 同期 Case 2: 同期無効セルのパン
  void panOnUnsyncedCellLeavesGlobalStateUntouched() {
    heiran::domain::GlobalViewState globalState{.baseOffsetX = 1.0, .baseOffsetY = 2.0};
    heiran::domain::CellViewState cellState{.syncEnabled = false};

    heiran::domain::applyPanToCell(&cellState, 4.0, 5.0);

    QCOMPARE(cellState.offsetDeltaX, 4.0);
    QCOMPARE(cellState.offsetDeltaY, 5.0);
    QCOMPARE(globalState.baseOffsetX, 1.0);
    QCOMPARE(globalState.baseOffsetY, 2.0);
  }

  // docs/テスト観点.md 同期 Case 5 と 6: 同期の切り替え
  void keepsCellOffsetWhenSyncIsToggled() {
    heiran::domain::GlobalViewState globalState{.baseScale = 1.5, .baseOffsetX = 10.0};
    heiran::domain::CellViewState cellState{.syncEnabled = true, .offsetDeltaX = 7.0};

    const auto beforeDisable = heiran::domain::computeFinalViewState(globalState, cellState);
    cellState.syncEnabled = false;
    const auto afterDisable = heiran::domain::computeFinalViewState(globalState, cellState);
    cellState.syncEnabled = true;
    const auto afterEnable = heiran::domain::computeFinalViewState(globalState, cellState);

    // 同期の切り替えだけでは見た目が変わらず、セル固有のずれも消えない。
    QCOMPARE(afterDisable.offsetX, beforeDisable.offsetX);
    QCOMPARE(afterEnable.offsetX, beforeDisable.offsetX);
    QCOMPARE(cellState.offsetDeltaX, 7.0);
  }

  // docs/テスト観点.md 同期 Case 7: ページ切り替え
  void keepsViewStateAcrossPageChange() {
    heiran::domain::GlobalViewState globalState{
        .baseScale = 2.0, .baseOffsetX = 11.0, .baseOffsetY = 12.0, .currentPage = 0};
    const heiran::domain::CellViewState cellState{
        .syncEnabled = false, .offsetDeltaX = 3.0, .offsetDeltaY = 4.0, .scale = 0.5};

    const auto before = heiran::domain::computeFinalViewState(globalState, cellState);
    globalState.currentPage = 2;
    const auto after = heiran::domain::computeFinalViewState(globalState, cellState);

    // ページ切り替えは offset_delta と scale を保持する。
    QCOMPARE(after.scale, before.scale);
    QCOMPARE(after.offsetX, before.offsetX);
    QCOMPARE(after.offsetY, before.offsetY);
  }

  void fitsSingleImageIntoCell() {
    const auto result =
        heiran::domain::computeFitView({{.imageWidth = 200, .imageHeight = 100}}, 400, 400);

    QVERIFY(result.valid);
    // 横が制約になるため 400 / 200 = 2.0 倍。
    QCOMPARE(result.baseScale, 2.0);
  }

  void fitsByTheConstrainingAxis() {
    const auto result =
        heiran::domain::computeFitView({{.imageWidth = 100, .imageHeight = 400}}, 400, 200);

    QVERIFY(result.valid);
    // 縦が制約になるため 200 / 400 = 0.5 倍。
    QCOMPARE(result.baseScale, 0.5);
  }

  void centersTheImageInTheCell() {
    const heiran::domain::GlobalViewState globalState = [] {
      const auto fit =
          heiran::domain::computeFitView({{.imageWidth = 200, .imageHeight = 100}}, 400, 400);
      return heiran::domain::GlobalViewState{.baseScale = fit.baseScale,
                                             .baseOffsetX = fit.baseOffsetX,
                                             .baseOffsetY = fit.baseOffsetY};
    }();
    const heiran::domain::CellViewState cellState;
    const auto finalState = heiran::domain::computeFinalViewState(globalState, cellState);

    // 画像の左右端が枠内に収まり、余白が上下左右で均等になる。
    const double left = heiran::domain::imageToViewX(finalState, 0.0);
    const double right = heiran::domain::imageToViewX(finalState, 200.0);
    QVERIFY(qFuzzyCompare(left + 100.0, 0.0 + 100.0));
    QVERIFY(qFuzzyCompare(right + 100.0, 400.0 + 100.0));

    const double top = heiran::domain::imageToViewY(finalState, 0.0);
    const double bottom = heiran::domain::imageToViewY(finalState, 100.0);
    QVERIFY(qFuzzyCompare(top + 1000.0, 100.0 + 1000.0));
    QVERIFY(qFuzzyCompare(bottom + 1000.0, 300.0 + 1000.0));
  }

  void picksTheMostConstrainingTargetSoEveryImageFits() {
    const auto result = heiran::domain::computeFitView(
        {{.imageWidth = 100, .imageHeight = 100}, {.imageWidth = 400, .imageHeight = 100}}, 400,
        400);

    QVERIFY(result.valid);
    // 幅 400 の画像が制約になるため 1.0 倍。
    QCOMPARE(result.baseScale, 1.0);
  }

  void keepsEveryImageInsideTheCell() {
    const std::vector<heiran::domain::FitTarget> targets = {
        {.imageWidth = 640, .imageHeight = 480},
        {.imageWidth = 200, .imageHeight = 300},
        {.imageWidth = 100, .imageHeight = 100},
    };
    const int cellWidth = 320;
    const int cellHeight = 240;
    const auto fit = heiran::domain::computeFitView(targets, cellWidth, cellHeight);
    QVERIFY(fit.valid);

    const heiran::domain::GlobalViewState globalState{
        .baseScale = fit.baseScale, .baseOffsetX = fit.baseOffsetX, .baseOffsetY = fit.baseOffsetY};

    for (const auto &target : targets) {
      const heiran::domain::CellViewState cellState{.scale = target.cellScale};
      const auto finalState = heiran::domain::computeFinalViewState(globalState, cellState);
      const double left = heiran::domain::imageToViewX(finalState, 0.0);
      const double right = heiran::domain::imageToViewX(finalState, target.imageWidth);
      const double top = heiran::domain::imageToViewY(finalState, 0.0);
      const double bottom = heiran::domain::imageToViewY(finalState, target.imageHeight);

      QVERIFY2(left >= -0.001 && right <= cellWidth + 0.001,
               qPrintable(QString("left=%1 right=%2").arg(left).arg(right)));
      QVERIFY2(top >= -0.001 && bottom <= cellHeight + 0.001,
               qPrintable(QString("top=%1 bottom=%2").arg(top).arg(bottom)));
    }
  }

  void accountsForPerCellScale() {
    // 個別拡大率 2.0 のセルは、同じ画素数でも 2 倍の大きさで表示される。
    const auto result = heiran::domain::computeFitView(
        {{.imageWidth = 100, .imageHeight = 100, .cellScale = 2.0}}, 400, 400);

    QVERIFY(result.valid);
    QCOMPARE(result.baseScale, 2.0);
  }

  void ignoresTargetsWithoutSize() {
    const auto result = heiran::domain::computeFitView(
        {{.imageWidth = 0, .imageHeight = 0}, {.imageWidth = 200, .imageHeight = 200}}, 400, 400);

    QVERIFY(result.valid);
    QCOMPARE(result.baseScale, 2.0);
  }

  void isInvalidWithoutUsableTargets() {
    QVERIFY(!heiran::domain::computeFitView({}, 400, 400).valid);
    QVERIFY(!heiran::domain::computeFitView({{.imageWidth = 0, .imageHeight = 0}}, 400, 400).valid);
  }

  void isInvalidForEmptyCell() {
    QVERIFY(!heiran::domain::computeFitView({{.imageWidth = 100, .imageHeight = 100}}, 0, 0).valid);
  }
};

QTEST_MAIN(ViewStateTests)

#include "ViewStateTests.moc"
