#pragma once

#include <QObject>

class QDoubleSpinBox;
class QMainWindow;

namespace heiran::ui {

class ViewerGridWidget;

class GlobalZoomController final : public QObject {
  Q_OBJECT

public:
  GlobalZoomController(QMainWindow *window, ViewerGridWidget *grid);

private:
  void refresh();

  ViewerGridWidget *grid_;
  QDoubleSpinBox *zoom_;
  bool editing_ = false;
};

}  // namespace heiran::ui
