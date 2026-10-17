#include "heiran/ui/CellContextMenu.hpp"

#include "heiran/ui/ActionIcons.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QInputDialog>
#include <QMenu>
#include <QVector>
#include <QWidget>
#include <cmath>

namespace heiran::ui {

namespace {

// 選択肢と現在値が同じかどうか。チェックの表示に使う。
bool isSameValue(double left, double right) {
  return std::abs(left - right) < 0.0001;
}

int normalizedQuarterTurns(int rotationQuarterTurns) {
  return ((rotationQuarterTurns % 4) + 4) % 4;
}

}  // namespace

CellContextMenuResult showCellContextMenu(QWidget *parent, const QPoint &globalPosition,
                                          const CellContextMenuState &state) {
  QMenu menu(parent);

  auto *syncAction = menu.addAction(
      state.syncEnabled ? QCoreApplication::translate("CellContextMenu", "Disable Sync")
                        : QCoreApplication::translate("CellContextMenu", "Enable Sync"));

  auto *scaleMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Scale"));
  QVector<QAction *> scaleActions;
  const double scaleValues[] = {0.5, 0.75, 1.0, 1.5, 2.0};
  for (const double scaleValue : scaleValues) {
    auto *action = scaleMenu->addAction(QString::number(scaleValue, 'f', 2));
    action->setCheckable(true);
    action->setChecked(isSameValue(state.scale, scaleValue));
    action->setData(scaleValue);
    scaleActions.push_back(action);
  }
  scaleMenu->addSeparator();
  auto *customScaleAction =
      scaleMenu->addAction(QCoreApplication::translate("CellContextMenu", "Custom..."));

  auto *gammaMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Gamma"));
  QVector<QAction *> gammaActions;
  const double gammaValues[] = {0.50, 0.75, 1.00, 1.25, 1.50, 2.00};
  for (const double gammaValue : gammaValues) {
    auto *action = gammaMenu->addAction(QString::number(gammaValue, 'f', 2));
    action->setCheckable(true);
    action->setChecked(isSameValue(state.gamma, gammaValue));
    action->setData(gammaValue);
    gammaActions.push_back(action);
  }
  gammaMenu->addSeparator();
  auto *customGammaAction =
      gammaMenu->addAction(QCoreApplication::translate("CellContextMenu", "Custom..."));

  auto *rotateMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Rotate"));
  QVector<QAction *> rotationActions;
  const struct {
    QString label;
    int quarterTurns;
  } rotationItems[] = {{QCoreApplication::translate("CellContextMenu", "0 deg"), 0},
                       {QCoreApplication::translate("CellContextMenu", "90 deg"), 1},
                       {QCoreApplication::translate("CellContextMenu", "180 deg"), 2},
                       {QCoreApplication::translate("CellContextMenu", "270 deg"), 3}};
  const int currentQuarterTurns = normalizedQuarterTurns(state.rotationQuarterTurns);
  for (const auto &rotationItem : rotationItems) {
    auto *action = rotateMenu->addAction(rotationItem.label);
    action->setCheckable(true);
    action->setChecked(currentQuarterTurns == rotationItem.quarterTurns);
    action->setData(rotationItem.quarterTurns);
    rotationActions.push_back(action);
  }

  auto *flipXAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Flip Horizontally"));
  flipXAction->setCheckable(true);
  flipXAction->setChecked(state.flipX);

  auto *invertAction = menu.addAction(QCoreApplication::translate("CellContextMenu", "Invert"));
  invertAction->setCheckable(true);
  invertAction->setChecked(state.invert);

  auto *flipYAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Flip Vertically"));
  flipYAction->setCheckable(true);
  flipYAction->setChecked(state.flipY);

  menu.addSeparator();
  auto *showLabelAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Show Label"));
  showLabelAction->setCheckable(true);
  showLabelAction->setChecked(state.labelVisible);
  showLabelAction->setEnabled(state.hasImageSet);

  auto *editLabelAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Edit Label..."));
  editLabelAction->setEnabled(state.hasImageSet);

  // 割り当てを外す。Assign Image の中の Empty と同じだが、探さずに選べるよう
  // 上の階層にも出す。
  auto *clearCellAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Clear Cell"));
  clearCellAction->setEnabled(state.hasImageSet);

  auto *assignImageMenu =
      menu.addMenu(QCoreApplication::translate("CellContextMenu", "Assign Image"));
  auto *emptyImageAction =
      assignImageMenu->addAction(QCoreApplication::translate("CellContextMenu", "Empty"));
  emptyImageAction->setCheckable(true);
  emptyImageAction->setChecked(state.imageSetIndex < 0);
  emptyImageAction->setData(-1);

  QVector<QAction *> assignImageActions;
  for (int imageIndex = 0; imageIndex < state.availableImageNames.size(); ++imageIndex) {
    auto *action = assignImageMenu->addAction(
        QString("%1: %2").arg(imageIndex + 1).arg(state.availableImageNames.at(imageIndex)));
    action->setCheckable(true);
    action->setChecked(state.imageSetIndex == imageIndex);
    action->setData(imageIndex);
    assignImageActions.push_back(action);
  }

  auto *resetAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Reset Cell Display"));

  // 差分。他のセルを基準にして、このセルに差分を出す。出している間は、やめる項目になる。
  auto *stopDiffAction =
      state.diffActive
          ? menu.addAction(
                state.diffIsWipe
                    ? QCoreApplication::translate("CellContextMenu", "Stop Showing Wipe")
                    : QCoreApplication::translate("CellContextMenu", "Stop Showing Difference"))
          : nullptr;
  QMenu *diffMenu = nullptr;
  QMenu *wipeMenu = nullptr;
  QVector<QAction *> diffBaseActions;
  QVector<QAction *> wipeBaseActions;
  if (!state.diffActive) {
    diffMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Show Difference vs"));
    wipeMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Show Wipe vs"));
    for (int cell = 0; cell < state.cellLabels.size(); ++cell) {
      if (cell == state.cellIndex) {
        continue;
      }
      auto *diffAction = diffMenu->addAction(state.cellLabels.at(cell));
      diffAction->setData(cell);
      diffBaseActions.push_back(diffAction);
      auto *wipeAction = wipeMenu->addAction(state.cellLabels.at(cell));
      wipeAction->setData(cell);
      wipeBaseActions.push_back(wipeAction);
    }
    diffMenu->setEnabled(!diffBaseActions.isEmpty());
    wipeMenu->setEnabled(!wipeBaseActions.isEmpty());
  }

  menu.addSeparator();
  auto *copyImageAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Copy Image"));
  copyImageAction->setEnabled(state.hasImage);
  auto *showInFolderAction =
      menu.addAction(QCoreApplication::translate("CellContextMenu", "Show in Folder"));
  showInFolderAction->setEnabled(state.hasImage);

  menu.addSeparator();
  auto *channelMenu = menu.addMenu(QCoreApplication::translate("CellContextMenu", "Channel"));
  QVector<QAction *> channelActions;
  const struct {
    QString label;
    ChannelMode mode;
  } channelItems[] = {
      {QCoreApplication::translate("CellContextMenu", "Color"), ChannelMode::Color},
      {QCoreApplication::translate("CellContextMenu", "Gray"), ChannelMode::Gray},
      {QCoreApplication::translate("CellContextMenu", "Red"), ChannelMode::Red},
      {QCoreApplication::translate("CellContextMenu", "Green"), ChannelMode::Green},
      {QCoreApplication::translate("CellContextMenu", "Blue"), ChannelMode::Blue},
      {QCoreApplication::translate("CellContextMenu", "Alpha"), ChannelMode::Alpha}};
  for (const auto &channelItem : channelItems) {
    auto *action = channelMenu->addAction(channelItem.label);
    action->setCheckable(true);
    action->setChecked(state.channelMode == channelItem.mode);
    action->setData(static_cast<int>(channelItem.mode));
    channelActions.push_back(action);
  }

  applyActionIcons(&menu);
  QAction *selectedAction = menu.exec(globalPosition);
  if (selectedAction == nullptr) {
    return {};
  }

  CellContextMenuResult result;
  if (selectedAction == syncAction) {
    result.action = CellContextMenuResult::Action::ToggleSync;
  } else if (selectedAction == customScaleAction) {
    bool ok = false;
    const double scale = QInputDialog::getDouble(
        parent, QCoreApplication::translate("CellContextMenu", "Custom Scale"),
        QCoreApplication::translate("CellContextMenu", "Scale"), state.scale, 0.01, 100.0, 2, &ok);
    if (ok) {
      result.action = CellContextMenuResult::Action::SetScale;
      result.scale = scale;
    }
  } else if (selectedAction == customGammaAction) {
    bool ok = false;
    const double gamma = QInputDialog::getDouble(
        parent, QCoreApplication::translate("CellContextMenu", "Custom Gamma"),
        QCoreApplication::translate("CellContextMenu", "Gamma"), state.gamma, 0.01, 10.0, 2, &ok);
    if (ok) {
      result.action = CellContextMenuResult::Action::SetGamma;
      result.gamma = gamma;
    }
  } else if (selectedAction == flipXAction) {
    result.action = CellContextMenuResult::Action::SetFlip;
    result.flipX = !state.flipX;
    result.flipY = state.flipY;
  } else if (selectedAction == flipYAction) {
    result.action = CellContextMenuResult::Action::SetFlip;
    result.flipX = state.flipX;
    result.flipY = !state.flipY;
  } else if (selectedAction == invertAction) {
    result.action = CellContextMenuResult::Action::SetInvert;
    result.invert = !state.invert;
  } else if (selectedAction == showLabelAction) {
    result.action = CellContextMenuResult::Action::SetLabelVisible;
    result.labelVisible = !state.labelVisible;
  } else if (selectedAction == editLabelAction) {
    result.action = CellContextMenuResult::Action::EditLabel;
  } else if (selectedAction == emptyImageAction || selectedAction == clearCellAction) {
    result.action = CellContextMenuResult::Action::AssignImageIndex;
    result.imageIndex = -1;
  } else if (stopDiffAction != nullptr && selectedAction == stopDiffAction) {
    result.action = CellContextMenuResult::Action::StopDiff;
  } else if (diffBaseActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::StartDiff;
    result.diffBaseCell = selectedAction->data().toInt();
  } else if (wipeBaseActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::StartWipe;
    result.diffBaseCell = selectedAction->data().toInt();
  } else if (selectedAction == resetAction) {
    result.action = CellContextMenuResult::Action::ResetCellDisplay;
  } else if (selectedAction == copyImageAction) {
    result.action = CellContextMenuResult::Action::CopyImageToClipboard;
  } else if (selectedAction == showInFolderAction) {
    result.action = CellContextMenuResult::Action::ShowInFolder;
  } else if (assignImageActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::AssignImageIndex;
    result.imageIndex = selectedAction->data().toInt();
  } else if (channelActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::SetChannelMode;
    result.channelMode = static_cast<ChannelMode>(selectedAction->data().toInt());
  } else if (scaleActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::SetScale;
    result.scale = selectedAction->data().toDouble();
  } else if (gammaActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::SetGamma;
    result.gamma = selectedAction->data().toDouble();
  } else if (rotationActions.contains(selectedAction)) {
    result.action = CellContextMenuResult::Action::SetRotation;
    result.rotationQuarterTurns = selectedAction->data().toInt();
  }

  return result;
}

}  // namespace heiran::ui
