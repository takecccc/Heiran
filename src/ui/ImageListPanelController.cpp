#include "heiran/ui/ImageListPanelController.hpp"

#include "heiran/domain/SessionPages.hpp"
#include "heiran/infra/FavoriteStore.hpp"
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
  favoritesFilter_ = favoritesFilter;
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
  connect(grid_, &ViewerGridWidget::sessionChanged, this, [this](bool reset) {
    if (reset && !changingSession_) {
      sourceSession_.reset();
    }
    invalidate();
  });
  connect(grid_, &ViewerGridWidget::pageNavigationChanged, this,
          &ImageListPanelController::invalidate);
  connect(favorites_, &FavoritesController::favoritesChanged, this, [this, favoritesFilter]() {
    if (sourceSession_ && favoritesFilter->isChecked())
      invalidate();
    else
      refreshFavoriteStates();
  });
  connect(panel_, &ImageListPanelWidget::pageSelected, this, [this](int row) {
    int page = grid_->navigationPageAt(row);
    if (sourceSession_) {
      const auto session = grid_->sessionSnapshot();
      page = heiran::domain::pageOfFile(session, 0, listedPaths_.value(row));
    }
    if (page >= 0) {
      grid_->setCurrentPage(page);
    }
  });
  connect(panel_, &ImageListPanelWidget::compareRequested, this,
          &ImageListPanelController::compareRequested);
  connect(panel_, &ImageListPanelWidget::favoritesRequested, favorites_,
          &FavoritesController::setFavorites);
}

void ImageListPanelController::beginSessionChange(
    std::optional<heiran::domain::SessionModel> sourceSession) {
  changingSession_ = true;
  sourceSession_ = std::move(sourceSession);
  pagesDirty_ = true;
}

void ImageListPanelController::endSessionChange() {
  changingSession_ = false;
  invalidate();
}

void ImageListPanelController::invalidate() {
  pagesDirty_ = true;
  refresh();
}

void ImageListPanelController::refresh() {
  if (changingSession_ || !dock_->isVisible()) {
    return;
  }
  const bool pagesChanged = pagesDirty_;
  if (pagesDirty_) {
    const auto session = sourceSession_.value_or(grid_->sessionSnapshot());
    listedPages_.clear();
    listedPaths_.clear();
    listedRowsByPath_.clear();
    const bool favoritesOnly = favoritesFilter_->isChecked();
    const int count =
        sourceSession_ ? heiran::domain::totalPageCount(session) : grid_->navigationPageCount();
    for (int index = 0; index < count; ++index) {
      const int page = sourceSession_ ? index : grid_->navigationPageAt(index);
      if (sourceSession_ && favoritesOnly && !pageHasFavorite(session, page))
        continue;
      listedPages_.push_back(page);
      listedPaths_.push_back(PageListing::pagePath(session, page));
      const auto &path = listedPaths_.last();
      if (!path.isEmpty()) {
        const auto key = heiran::infra::FavoriteStore::pathKey(path);
        if (!listedRowsByPath_.contains(key))
          listedRowsByPath_.insert(key, listedPaths_.size() - 1);
      }
    }
    panel_->setPages(listedPaths_, currentListRow());
    refreshFavoriteStates();
    pagesDirty_ = false;
  } else {
    panel_->setCurrentPage(currentListRow());
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
  if (changingSession_ || !dock_->isVisible()) {
    return;
  }
  const auto session = sourceSession_.value_or(grid_->sessionSnapshot());
  QVector<bool> states;
  for (int page : listedPages_) {
    states.push_back(pageHasFavorite(session, page));
  }
  panel_->setFavoriteStates(states);
}

int ImageListPanelController::currentListRow() const {
  if (!sourceSession_)
    return grid_->navigationPageIndex();
  const auto path = grid_->currentFilePathForCell(0);
  if (path.isEmpty())
    return -1;
  const auto key = heiran::infra::FavoriteStore::pathKey(path);
  return listedRowsByPath_.value(key, -1);
}

bool ImageListPanelController::pageHasFavorite(const heiran::domain::SessionModel &session,
                                               int page) const {
  const int cells = session.viewer.layout.rows * session.viewer.layout.cols;
  for (int cell = 0; cell < cells; ++cell) {
    const auto source = heiran::domain::cellPageAt(session, cell, page);
    if (source.hasFile() && favorites_->isFavorite(source.path))
      return true;
  }
  return false;
}

}  // namespace heiran::ui
