#pragma once

#include <QMenu>
#include <QPointer>

class QActionGroup;

namespace heiran::ui {

class LayoutMenu : public QMenu {
public:
  LayoutMenu(const QString &title, QActionGroup *cellCounts, QWidget *parent);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  bool hasCellCountSubmenu(const QAction *action) const;
  bool isOverSubmenuArrow(QAction *action, const QPoint &position) const;

  const QActionGroup *cellCounts_;
  QPointer<QAction> pressedCellCount_;
  bool pressedArrow_ = false;
};

}  // namespace heiran::ui
