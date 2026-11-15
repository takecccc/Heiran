#pragma once

#include <QObject>

class QDockWidget;
class QMainWindow;

namespace heiran::ui {

class ImageListPanelWidget;
class ViewerGridWidget;

class ImageListPanelController final : public QObject {
  Q_OBJECT

public:
  ImageListPanelController(QMainWindow *window, ViewerGridWidget *grid);

private:
  void refresh();
  void invalidate();

  ViewerGridWidget *grid_;
  QDockWidget *dock_;
  ImageListPanelWidget *panel_;
  bool pagesDirty_ = true;
};

}  // namespace heiran::ui
