#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <optional>

class QDockWidget;
class QMainWindow;
class QAction;

namespace heiran::ui {

class ImageListPanelWidget;
class FavoritesController;
class ViewerGridWidget;

class ImageListPanelController final : public QObject {
  Q_OBJECT

public:
  ImageListPanelController(QMainWindow *window, ViewerGridWidget *grid);
  std::optional<heiran::domain::SessionModel> sourceSession() const { return sourceSession_; }
  void beginSessionChange(std::optional<heiran::domain::SessionModel> sourceSession);
  void endSessionChange();

Q_SIGNALS:
  void compareRequested(const QStringList &paths);

private:
  void refresh();
  void invalidate();
  void refreshFavoriteStates();
  int currentListRow() const;
  bool pageHasFavorite(const heiran::domain::SessionModel &session, int page) const;

  ViewerGridWidget *grid_;
  QDockWidget *dock_;
  ImageListPanelWidget *panel_;
  FavoritesController *favorites_;
  QAction *favoritesFilter_ = nullptr;
  QStringList displayedPaths_;
  QStringList listedPaths_;
  QHash<QString, int> listedRowsByPath_;
  QVector<int> listedPages_;
  std::optional<heiran::domain::SessionModel> sourceSession_;
  bool changingSession_ = false;
  bool pagesDirty_ = true;
};

}  // namespace heiran::ui
