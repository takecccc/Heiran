#include "heiran/ui/ComparisonNavigationController.hpp"

#include "heiran/domain/SessionPages.hpp"
#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/ui/FavoritesController.hpp"
#include "heiran/ui/ImageListPanelController.hpp"
#include "heiran/ui/ImageListPanelWidget.hpp"
#include "heiran/ui/PageListing.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QDir>
#include <QHash>
#include <QMainWindow>
#include <QScopedValueRollback>
#include <QSet>
#include <algorithm>
#include <limits>

namespace heiran::ui {

ComparisonNavigationController::ComparisonNavigationController(QMainWindow *window,
                                                               ViewerGridWidget *grid,
                                                               ImageListPanelController *imageList)
    : QObject(window), grid_(grid), imageList_(imageList),
      panel_(window->findChild<ImageListPanelWidget *>()),
      favorites_(window->findChild<FavoritesController *>()),
      filter_(window->findChild<QAction *>("favoritesOnlyAction")) {
  connect(imageList_, &ImageListPanelController::sourceSessionChanging, this, [this]() {
    suspended_ = true;
    active_ = imageList_->sourceSession().has_value();
    favorites_->setNavigationSource(imageList_->sourceSession());
    if (active_)
      grid_->setPageFilter(std::nullopt);
  });
  connect(imageList_, &ImageListPanelController::sourceSessionChanged, this, [this]() {
    suspended_ = false;
    sourceDirty_ = true;
    candidatesDirty_ = true;
    refresh();
  });
  connect(grid_, &ViewerGridWidget::sessionChanged, this, [this]() { refresh(); });
  connect(grid_, &ViewerGridWidget::viewChanged, this, [this]() { refresh(); });
  connect(favorites_, &FavoritesController::favoritesChanged, this,
          &ComparisonNavigationController::invalidateCandidates);
  connect(filter_, &QAction::toggled, this, &ComparisonNavigationController::invalidateCandidates);
}

void ComparisonNavigationController::invalidateCandidates() {
  if (busy_)
    return;
  candidatesDirty_ = true;
  refresh();
}

void ComparisonNavigationController::refresh() {
  if (suspended_ || busy_)
    return;
  const QScopedValueRollback guard(busy_, true);
  const auto source = imageList_->sourceSession();
  if (!source) {
    if (active_) {
      active_ = false;
      favorites_->setNavigationSource(std::nullopt);
    }
    basePaths_.clear();
    lastPaths_.clear();
    sourceDirty_ = true;
    panel_->setExcludedDisplayedFiles({});
    return;
  }
  active_ = true;
  if (sourceDirty_) {
    basePaths_.clear();
    QSet<QString> seen;
    for (int page = 0; page < heiran::domain::totalPageCount(*source); ++page) {
      const auto path = PageListing::pagePath(*source, page);
      const auto key = heiran::infra::FavoriteStore::pathKey(path);
      if (!path.isEmpty() && !seen.contains(key)) {
        basePaths_.push_back(path);
        seen.insert(key);
      }
    }
    sourceDirty_ = false;
    candidatesDirty_ = true;
  }
  auto session = grid_->sessionSnapshot();
  const int cells = session.viewer.layout.rows * session.viewer.layout.cols;
  QStringList paths;
  for (int cell = 0; cell < cells; ++cell)
    paths.push_back(heiran::domain::cellPageAt(session, cell, grid_->currentPage()).path);
  if (!candidatesDirty_ && paths == lastPaths_)
    return;
  lastPaths_ = paths;
  candidatesDirty_ = false;

  QHash<QString, int> positions;
  QVector<int> eligible;
  QSet<QString> eligibleKeys;
  for (int index = 0; index < basePaths_.size(); ++index) {
    const auto &path = basePaths_.at(index);
    const auto key = heiran::infra::FavoriteStore::pathKey(path);
    positions.insert(key, index);
    if (!filter_->isChecked() || favorites_->isFavorite(path)) {
      eligible.push_back(index);
      eligibleKeys.insert(key);
    }
  }
  QVector<QStringList> before(cells), after(cells);
  QStringList excluded;
  int previous = std::numeric_limits<int>::max();
  int next = std::numeric_limits<int>::max();
  int populated = 0;
  for (int cell = 0; cell < cells; ++cell) {
    const auto &path = paths.at(cell);
    const auto key = heiran::infra::FavoriteStore::pathKey(path);
    excluded.push_back(!path.isEmpty() && !eligibleKeys.contains(key) ? path : QString());
    if (path.isEmpty())
      continue;
    ++populated;
    const int position = positions.value(key, -1);
    if (position >= 0) {
      for (int index : eligible) {
        if (index < position)
          before[cell].push_back(basePaths_.at(index));
        else if (index > position)
          after[cell].push_back(basePaths_.at(index));
      }
    }
    previous = std::min(previous, static_cast<int>(before.at(cell).size()));
    next = std::min(next, static_cast<int>(after.at(cell).size()));
  }
  panel_->setExcludedDisplayedFiles(excluded);
  if (populated == 0)
    return;

  QVector<heiran::domain::ImageSet> images;
  bool changed = false;
  for (int cell = 0; cell < cells; ++cell) {
    if (paths.at(cell).isEmpty())
      continue;
    auto image = *heiran::domain::imageSetForCell(session, cell);
    QStringList sequence = before.at(cell).mid(before.at(cell).size() - previous);
    sequence.push_back(paths.at(cell));
    sequence.append(after.at(cell).mid(0, next));
    const auto path = session.sessionDirectory.isEmpty()
                          ? paths.at(cell)
                          : QDir(session.sessionDirectory).relativeFilePath(paths.at(cell));
    changed |= image.pageOffset != 0 || image.resolvedPaths != sequence || image.path != path ||
               session.cells.at(cell).imageIndex != images.size();
    image.pageOffset = 0;
    image.path = path;
    image.resolvedPaths = sequence;
    session.cells[cell].imageIndex = images.size();
    images.push_back(std::move(image));
  }
  changed |= session.images.size() != images.size();
  if (changed || grid_->currentPage() != previous) {
    session.images = std::move(images);
    grid_->setSessionPreservingView(session, previous, false);
  }
}

}  // namespace heiran::ui
