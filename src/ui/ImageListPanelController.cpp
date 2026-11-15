#include "heiran/ui/ImageListPanelController.hpp"

#include "heiran/domain/SessionPages.hpp"
#include "heiran/ui/FavoritesController.hpp"
#include "heiran/ui/ImageListPanelWidget.hpp"
#include "heiran/ui/PageListing.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QToolBar>

namespace heiran::ui {

ImageListPanelController::ImageListPanelController(QMainWindow *window, ViewerGridWidget *grid)
    : QObject(window), grid_(grid), dock_(new QDockWidget(tr("Image List"), window)),
      panel_(new ImageListPanelWidget(dock_)),
      favorites_(window->findChild<FavoritesController *>()) {
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
  action->setObjectName("imageListAction");
  action->setShortcut(QKeySequence(Qt::Key_T));
  action->setToolTip(tr("Show or hide the image list."));
  auto *favoritesFilter = window->findChild<QAction *>("favoritesOnlyAction");
  panel_->setFavoritesOnly(favoritesFilter->isChecked());
  connect(panel_, &ImageListPanelWidget::favoritesOnlyRequested, favoritesFilter,
          &QAction::setChecked);
  connect(favoritesFilter, &QAction::toggled, panel_, &ImageListPanelWidget::setFavoritesOnly);
  panel_->setDetailsVisible(QSettings().value("display/imageListMode", "thumbnails").toString() ==
                            "details");
  connect(panel_, &ImageListPanelWidget::detailsVisibleChanged, this, [this, window](bool details) {
    QSettings().setValue("display/imageListMode", details ? "details" : "thumbnails");
    if (details && dock_->isVisible() && dock_->width() < 500) {
      window->resizeDocks({dock_}, {520}, Qt::Horizontal);
    }
  });
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
  connect(grid_, &ViewerGridWidget::viewChanged, this, &ImageListPanelController::refresh);
  connect(grid_, &ViewerGridWidget::sessionChanged, this, &ImageListPanelController::invalidate);
  connect(grid_, &ViewerGridWidget::pageNavigationChanged, this,
          &ImageListPanelController::invalidate);
  connect(favorites_, &FavoritesController::favoritesChanged, this,
          &ImageListPanelController::refreshFavoriteStates);
  connect(panel_, &ImageListPanelWidget::pageSelected, this, [this](int row) {
    const int page = grid_->navigationPageAt(row);
    if (page >= 0) {
      grid_->setCurrentPage(page);
    }
  });
  connect(panel_, &ImageListPanelWidget::compareRequested, this,
          &ImageListPanelController::compareRequested);
  connect(panel_, &ImageListPanelWidget::favoritesRequested, favorites_,
          &FavoritesController::setFavorites);
}

void ImageListPanelController::invalidate() {
  pagesDirty_ = true;
  refresh();
}

void ImageListPanelController::refresh() {
  if (!dock_->isVisible()) {
    return;
  }
  const bool pagesChanged = pagesDirty_;
  if (pagesDirty_) {
    const auto session = grid_->sessionSnapshot();
    QStringList paths;
    for (int index = 0; index < grid_->navigationPageCount(); ++index) {
      paths.push_back(PageListing::pagePath(session, grid_->navigationPageAt(index)));
    }
    panel_->setPages(paths, grid_->navigationPageIndex());
    refreshFavoriteStates();
    pagesDirty_ = false;
  } else {
    panel_->setCurrentPage(grid_->navigationPageIndex());
  }
  QStringList displayed;
  const auto session = grid_->sessionSnapshot();
  const int count = session.viewer.layout.rows * session.viewer.layout.cols;
  for (int cell = 0; cell < count; ++cell) {
    displayed.push_back(grid_->currentFilePathForCell(cell));
  }
  if (pagesChanged || displayedPaths_ != displayed) {
    displayedPaths_ = displayed;
    panel_->setDisplayedFiles(displayed);
  }
}

void ImageListPanelController::refreshFavoriteStates() {
  if (!dock_->isVisible()) {
    return;
  }
  const auto session = grid_->sessionSnapshot();
  QVector<bool> states;
  const int cells = session.viewer.layout.rows * session.viewer.layout.cols;
  for (int index = 0; index < grid_->navigationPageCount(); ++index) {
    bool favorite = false;
    for (int cell = 0; cell < cells; ++cell) {
      const auto source = heiran::domain::cellPageAt(session, cell, grid_->navigationPageAt(index));
      if (source.hasFile() && favorites_->isFavorite(source.path)) {
        favorite = true;
        break;
      }
    }
    states.push_back(favorite);
  }
  panel_->setFavoriteStates(states);
}

}  // namespace heiran::ui
