#pragma once

#include "heiran/domain/SessionModel.hpp"
#include "heiran/infra/FavoriteExporter.hpp"
#include "heiran/infra/FavoriteStore.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>
#include <optional>

class QAction;
class QMainWindow;
class QProgressDialog;

namespace heiran::ui {

class ViewerGridWidget;

class FavoritesController final : public QObject {
  Q_OBJECT

public:
  FavoritesController(QMainWindow *window, ViewerGridWidget *grid, QString storePath = {});
  ~FavoritesController() override;
  QStringList favoriteFiles() const { return scopedFavorites_; }
  bool isFavorite(const QString &path) const { return store_.contains(path); }
  void setFavorites(const QStringList &paths, bool favorite);
  void setNavigationSource(std::optional<heiran::domain::SessionModel> source);

Q_SIGNALS:
  void favoritesChanged();

private:
  void refresh();
  void rebuildScope();
  void applyFilter();
  void toggle(const QString &path);
  void copyFavorites();

  QMainWindow *window_;
  ViewerGridWidget *grid_;
  heiran::infra::FavoriteStore store_;
  QAction *favoriteAction_;
  QAction *filterAction_;
  QAction *copyAction_;
  QStringList scopedFavorites_;
  std::optional<heiran::domain::SessionModel> navigationSource_;
  QFutureWatcher<heiran::infra::FavoriteCopyResult> copyWatcher_;
  std::shared_ptr<std::atomic_bool> cancelCopy_;
  QPointer<QProgressDialog> copyProgress_;
};

}  // namespace heiran::ui
