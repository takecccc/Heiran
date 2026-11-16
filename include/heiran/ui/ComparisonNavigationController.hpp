#pragma once

#include <QObject>
#include <QStringList>

class QAction;
class QMainWindow;

namespace heiran::ui {

class FavoritesController;
class ImageListPanelController;
class ImageListPanelWidget;
class ViewerGridWidget;

class ComparisonNavigationController final : public QObject {
  Q_OBJECT

public:
  ComparisonNavigationController(QMainWindow *window, ViewerGridWidget *grid,
                                 ImageListPanelController *imageList);

private:
  void refresh();
  void invalidateCandidates();

  ViewerGridWidget *grid_;
  ImageListPanelController *imageList_;
  ImageListPanelWidget *panel_;
  FavoritesController *favorites_;
  QAction *filter_;
  QStringList basePaths_;
  QStringList lastPaths_;
  bool suspended_ = false;
  bool busy_ = false;
  bool active_ = false;
  bool sourceDirty_ = true;
  bool candidatesDirty_ = true;
};

}  // namespace heiran::ui
