#include "heiran/ui/GlobalZoomController.hpp"

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QSignalBlocker>
#include <QToolBar>

namespace heiran::ui {

GlobalZoomController::GlobalZoomController(QMainWindow *window, ViewerGridWidget *grid)
    : QObject(window), grid_(grid), zoom_(new QDoubleSpinBox(window)) {
  zoom_->setObjectName("globalZoomPercent");
  zoom_->setAccessibleName(tr("Overall zoom"));
  zoom_->setToolTip(tr("Set the overall zoom. Individual cell zoom ratios are preserved."));
  zoom_->setRange(0.01, 1000000.0);
  zoom_->setDecimals(2);
  zoom_->setSuffix("%");
  zoom_->setSingleStep(10.0);
  zoom_->setKeyboardTracking(false);
  zoom_->setFocusPolicy(Qt::ClickFocus);
  zoom_->setFixedWidth(112);
  auto *toolbar = window->findChild<QToolBar *>("navigationToolBar");
  const auto actions = toolbar->actions();
  const int index = actions.indexOf(window->findChild<QAction *>("actualSizeAction"));
  toolbar->insertWidget(actions.value(index + 1), zoom_);
  connect(zoom_->findChild<QLineEdit *>(), &QLineEdit::textEdited, this,
          [this]() { editing_ = true; });
  connect(zoom_, &QDoubleSpinBox::valueChanged, this,
          [this](double percent) { grid_->setGlobalScale(percent / 100.0); });
  connect(zoom_, &QDoubleSpinBox::editingFinished, this, [this]() {
    editing_ = false;
    refresh();
  });
  connect(grid_, &ViewerGridWidget::statusChanged, this, [this]() { refresh(); });
  connect(grid_, &ViewerGridWidget::viewChanged, this, &GlobalZoomController::refresh);
  connect(grid_, &ViewerGridWidget::sessionChanged, this, [this]() { refresh(); });
  refresh();
}

void GlobalZoomController::refresh() {
  const bool available = grid_->navigationPageCount() > 0;
  if (!available)
    editing_ = false;
  zoom_->setEnabled(available);
  if (editing_)
    return;
  const QSignalBlocker blocker(zoom_);
  zoom_->setValue(grid_->globalViewState().baseScale * 100.0);
}

}  // namespace heiran::ui
