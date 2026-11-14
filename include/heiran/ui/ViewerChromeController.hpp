#pragma once

#include <QObject>

class QAction;
class QComboBox;
class QMainWindow;
class QToolBar;
class QWidget;

namespace heiran::ui {

class ViewerGridWidget;

class ViewerChromeController final : public QObject {
public:
  ViewerChromeController(QMainWindow *window, ViewerGridWidget *grid, QToolBar *navigation,
                         QComboBox *pageSelector, QAction *previous, QAction *next);
  void refreshPages();

private:
  QComboBox *sourceSelector_;
  QComboBox *compactSelector_;
};

}  // namespace heiran::ui
