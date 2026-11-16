#include "heiran/ui/FavoritesController.hpp"

#include "heiran/domain/SessionPages.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QFileDialog>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QtConcurrentRun>
#include <algorithm>

namespace heiran::ui {

FavoritesController::FavoritesController(QMainWindow *window, ViewerGridWidget *grid,
                                         QString storePath)
    : QObject(window), window_(window), grid_(grid), store_(std::move(storePath)) {
  setObjectName("favoritesController");
  auto *menu = window->findChild<QMenu *>("imagesMenu")->addMenu(tr("Favorites"));
  favoriteAction_ = menu->addAction(tr("Favorite"));
  favoriteAction_->setObjectName("favoriteAction");
  favoriteAction_->setCheckable(true);
  favoriteAction_->setShortcut(QKeySequence(Qt::Key_B));
  window->addAction(favoriteAction_);
  filterAction_ = menu->addAction(tr("Favorites Only"));
  filterAction_->setObjectName("favoritesOnlyAction");
  filterAction_->setCheckable(true);
  filterAction_->setShortcut(QKeySequence("Ctrl+Shift+B"));
  window->addAction(filterAction_);
  menu->addSeparator();
  copyAction_ = menu->addAction(tr("Copy Favorites..."));
  copyAction_->setObjectName("copyFavoritesAction");
  window->findChild<QToolBar *>("navigationToolBar")->addAction(favoriteAction_);
  window->findChild<QToolBar *>("navigationToolBar")->addAction(filterAction_);
  connect(favoriteAction_, &QAction::triggered, this,
          [this]() { toggle(grid_->targetCellFilePath()); });
  connect(filterAction_, &QAction::toggled, this, [this]() { applyFilter(); });
  connect(copyAction_, &QAction::triggered, this, &FavoritesController::copyFavorites);
  connect(grid, &ViewerGridWidget::favoriteToggleRequested, this, [this](int cell) {
    const auto source =
        heiran::domain::cellPageAt(grid_->sessionSnapshot(), cell, grid_->currentPage());
    if (grid_->navigationPageCount() > 0)
      toggle(source.path);
  });
  connect(grid, &ViewerGridWidget::viewChanged, this, &FavoritesController::refresh);
  connect(grid, &ViewerGridWidget::imageContentChanged, this, &FavoritesController::refresh);
  connect(grid, &ViewerGridWidget::activeCellChanged, this, &FavoritesController::refresh);
  connect(grid, &ViewerGridWidget::sessionChanged, this, [this](bool reset) {
    if (reset) {
      QSignalBlocker blocker(filterAction_);
      filterAction_->setChecked(false);
    }
    store_.load();
    rebuildScope();
    if (!reset && filterAction_->isChecked())
      applyFilter();
    refresh();
  });
  connect(&copyWatcher_, &QFutureWatcher<heiran::infra::FavoriteCopyResult>::finished, this,
          [this]() {
            if (copyProgress_) {
              copyProgress_->close();
              copyProgress_->deleteLater();
            }
            const auto result = copyWatcher_.result();
            refresh();
            const QString message =
                result.cancelled
                    ? tr("Copy cancelled. %1 image(s) were copied.").arg(result.copiedPaths.size())
                    : tr("Copied %1 favorite image(s). Skipped %2. Failed %3.")
                          .arg(result.copiedPaths.size())
                          .arg(result.skipped)
                          .arg(result.errors.size());
            QMessageBox report(result.errors.isEmpty() ? QMessageBox::Information
                                                       : QMessageBox::Warning,
                               tr("Copy Favorites"), message, QMessageBox::Ok, window_);
            report.setDetailedText((result.copiedPaths + result.errors).join("\n"));
            report.exec();
          });
  QString error;
  if (!store_.load(&error)) {
    QTimer::singleShot(0, this, [this, error]() {
      window_->statusBar()->showMessage(tr("Could not load favorites: %1").arg(error), 10000);
    });
  }
  rebuildScope();
  refresh();
}

FavoritesController::~FavoritesController() {
  if (cancelCopy_)
    cancelCopy_->store(true);
  copyWatcher_.waitForFinished();
}

void FavoritesController::rebuildScope() {
  scopedFavorites_.clear();
  QSet<QString> seen;
  const auto session = navigationSource_.value_or(grid_->sessionSnapshot());
  const int cells = session.viewer.layout.rows * session.viewer.layout.cols;
  QSet<int> sets;
  for (int cell = 0; cell < cells && cell < session.cells.size(); ++cell)
    sets.insert(session.cells.at(cell).imageIndex);
  for (int index : sets) {
    if (index < 0 || index >= session.images.size())
      continue;
    for (const auto &path : session.images.at(index).resolvedPaths) {
      const auto key = heiran::infra::FavoriteStore::pathKey(path);
      if (store_.contains(path) && !seen.contains(key)) {
        seen.insert(key);
        scopedFavorites_.push_back(path);
      }
    }
  }
  std::sort(scopedFavorites_.begin(), scopedFavorites_.end());
  Q_EMIT favoritesChanged();
}

void FavoritesController::refresh() {
  const auto path = grid_->targetCellFilePath();
  const QSignalBlocker blocker(favoriteAction_);
  favoriteAction_->setEnabled(!path.isEmpty());
  favoriteAction_->setChecked(store_.contains(path));
  copyAction_->setEnabled(!scopedFavorites_.isEmpty() && !copyWatcher_.isRunning());
  const auto cells = grid_->findChildren<ImageViewWidget *>();
  for (int index = 0; index < cells.size(); ++index) {
    const auto path = cells.at(index)->currentFilePath();
    const bool available = grid_->navigationPageCount() > 0 && !path.isEmpty();
    cells.at(index)->setFavorite(available && store_.contains(path), available);
  }
}

void FavoritesController::applyFilter() {
  if (navigationSource_) {
    refresh();
    return;
  }
  if (!filterAction_->isChecked()) {
    grid_->setPageFilter(std::nullopt);
  } else {
    QVector<int> pages;
    const auto session = grid_->sessionSnapshot();
    const int cells = session.viewer.layout.rows * session.viewer.layout.cols;
    for (int page = 0; page < grid_->totalPages(); ++page) {
      for (int cell = 0; cell < cells; ++cell) {
        const auto source = heiran::domain::cellPageAt(session, cell, page);
        if (source.hasFile() && store_.contains(source.path)) {
          pages.push_back(page);
          break;
        }
      }
    }
    grid_->setPageFilter(std::move(pages));
  }
  refresh();
}

void FavoritesController::setNavigationSource(std::optional<heiran::domain::SessionModel> source) {
  navigationSource_ = std::move(source);
  rebuildScope();
  refresh();
}

void FavoritesController::toggle(const QString &path) {
  if (path.isEmpty())
    return;
  setFavorites({path}, !store_.contains(path));
}

void FavoritesController::setFavorites(const QStringList &paths, bool favorite) {
  if (paths.isEmpty()) {
    return;
  }
  QString error;
  if (!store_.setFavorites(paths, favorite, &error)) {
    QMessageBox::warning(window_, tr("Favorites"), tr("Could not save favorites: %1").arg(error));
  }
  rebuildScope();
  if (filterAction_->isChecked())
    applyFilter();
  refresh();
}

void FavoritesController::copyFavorites() {
  const QString directory = QFileDialog::getExistingDirectory(window_, tr("Copy Favorites"));
  if (directory.isEmpty())
    return;
  cancelCopy_ = std::make_shared<std::atomic_bool>(false);
  copyProgress_ =
      new QProgressDialog(tr("Copying favorite images..."), tr("Cancel"), 0, 0, window_);
  copyProgress_->setWindowModality(Qt::WindowModal);
  copyProgress_->setMinimumDuration(0);
  connect(copyProgress_, &QProgressDialog::canceled, this,
          [cancel = cancelCopy_]() { cancel->store(true); });
  copyProgress_->show();
  copyWatcher_.setFuture(
      QtConcurrent::run([paths = scopedFavorites_, directory, cancel = cancelCopy_]() {
        return heiran::infra::copyFavoriteFiles(paths, directory, *cancel);
      }));
  refresh();
}

}  // namespace heiran::ui
