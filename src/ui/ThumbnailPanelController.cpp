#include "heiran/ui/ThumbnailPanelController.hpp"

#include "heiran/ui/PageListing.hpp"
#include "heiran/ui/ThumbnailPanelWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QToolBar>

namespace heiran::ui {

ThumbnailPanelController::ThumbnailPanelController(QMainWindow *window, ViewerGridWidget *grid)
    : QObject(window), grid_(grid), dock_(new QDockWidget(tr("Thumbnails"), window)),
      panel_(new ThumbnailPanelWidget(dock_)) {
  dock_->setObjectName("thumbnailDock");
  dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
  dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  dock_->setMinimumWidth(220);
  dock_->setWidget(panel_);
  dock_->hide();
  if (!window->restoreDockWidget(dock_)) {
    window->addDockWidget(Qt::LeftDockWidgetArea, dock_);
    window->resizeDocks({dock_}, {240}, Qt::Horizontal);
  }
  auto *action = dock_->toggleViewAction();
  action->setObjectName("thumbnailsAction");
  action->setShortcut(QKeySequence(Qt::Key_T));
  action->setToolTip(tr("Show or hide the thumbnail list."));
  window->addAction(action);
  window->findChild<QMenu *>("viewMenu")->addAction(action);
  auto *toolbar = window->findChild<QToolBar *>("navigationToolBar");
  const auto actions = toolbar->actions();
  auto *selector = window->findChild<QComboBox *>("pageSelector");
  for (int index = 0; index < actions.size(); ++index) {
    if (toolbar->widgetForAction(actions.at(index)) == selector) {
      toolbar->insertAction(actions.value(index + 1), action);
      break;
    }
  }
  connect(dock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
    if (visible) {
      refresh();
    } else {
      panel_->cancelLoading();
      pagesDirty_ = true;
    }
  });
  connect(grid_, &ViewerGridWidget::viewChanged, this, &ThumbnailPanelController::refresh);
  connect(grid_, &ViewerGridWidget::sessionChanged, this, &ThumbnailPanelController::invalidate);
  connect(grid_, &ViewerGridWidget::pageNavigationChanged, this,
          &ThumbnailPanelController::invalidate);
  connect(panel_, &ThumbnailPanelWidget::pageSelected, this, [this](int row) {
    const int page = grid_->navigationPageAt(row);
    if (page >= 0) {
      grid_->setCurrentPage(page);
    }
  });
}

void ThumbnailPanelController::invalidate() {
  pagesDirty_ = true;
  refresh();
}

void ThumbnailPanelController::refresh() {
  if (!dock_->isVisible()) {
    return;
  }
  if (pagesDirty_) {
    const auto session = grid_->sessionSnapshot();
    QStringList paths;
    for (int index = 0; index < grid_->navigationPageCount(); ++index) {
      paths.push_back(PageListing::pagePath(session, grid_->navigationPageAt(index)));
    }
    panel_->setPages(paths, grid_->navigationPageIndex());
    pagesDirty_ = false;
  } else {
    panel_->setCurrentPage(grid_->navigationPageIndex());
  }
}

}  // namespace heiran::ui
