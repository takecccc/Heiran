#pragma once

#include <QObject>
#include <QStringList>

class QDockWidget;
class QMainWindow;

namespace heiran::ui {

class ImageListPanelWidget;
class FavoritesController;
class ViewerGridWidget;

class ImageListPanelController final : public QObject {
  Q_OBJECT

public:
  ImageListPanelController(QMainWindow *window, ViewerGridWidget *grid);

Q_SIGNALS:
  void compareRequested(const QStringList &paths);

private:
  void refresh();
  void invalidate();
  void refreshFavoriteStates();

  ViewerGridWidget *grid_;
  QDockWidget *dock_;
  ImageListPanelWidget *panel_;
  FavoritesController *favorites_;
  QStringList displayedPaths_;
  bool pagesDirty_ = true;
};

}  // namespace heiran::ui
