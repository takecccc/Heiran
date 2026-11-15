#pragma once

#include <QObject>

class QDockWidget;
class QMainWindow;

namespace heiran::ui {

class ThumbnailPanelWidget;
class ViewerGridWidget;

class ThumbnailPanelController final : public QObject {
  Q_OBJECT

public:
  ThumbnailPanelController(QMainWindow *window, ViewerGridWidget *grid);

private:
  void refresh();
  void invalidate();

  ViewerGridWidget *grid_;
  QDockWidget *dock_;
  ThumbnailPanelWidget *panel_;
  bool pagesDirty_ = true;
};

}  // namespace heiran::ui
