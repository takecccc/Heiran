#pragma once

#include <QList>
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
                         QComboBox *pageSelector, QAction *previous, QAction *next,
                         QAction *secondRowStart);
  static QComboBox *createPageSelector(QWidget *parent);
  void setWrapNavigation(bool enabled);
  void refreshPages();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void scheduleNavigationLayout();
  void updateNavigationLayout();
  int navigationWidthNeeded() const;

  QMainWindow *window_;
  QToolBar *navigation_;
  QToolBar *secondRow_;
  QAction *secondRowStart_;
  QComboBox *sourceSelector_;
  QComboBox *compactSelector_;
  QList<QAction *> wrappedActions_;
  int wrapWidthCorrection_ = 0;
  bool wrapNavigation_ = false;
  bool layoutPending_ = false;
};

}  // namespace heiran::ui
