#include "heiran/ui/LayoutMenu.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>

namespace heiran::ui {

LayoutMenu::LayoutMenu(const QString &title, QActionGroup *cellCounts, QWidget *parent)
    : QMenu(title, parent), cellCounts_(cellCounts) {}

void LayoutMenu::mousePressEvent(QMouseEvent *event) {
  const auto position = event->position().toPoint();
  auto *action = actionAt(position);
  pressedCellCount_.clear();
  if (event->button() == Qt::LeftButton && hasCellCountSubmenu(action)) {
    pressedCellCount_ = action;
    pressedArrow_ = isOverSubmenuArrow(action, position);
    setActiveAction(action);
  }
  QMenu::mousePressEvent(event);
}

void LayoutMenu::mouseReleaseEvent(QMouseEvent *event) {
  const auto position = event->position().toPoint();
  auto *action = actionAt(position);
  const bool pressedThisCount = event->button() == Qt::LeftButton && action == pressedCellCount_ &&
                                hasCellCountSubmenu(action);
  pressedCellCount_.clear();
  if (pressedThisCount && !pressedArrow_ && !isOverSubmenuArrow(action, position)) {
    hide();
    action->trigger();
    return;
  }

  QMenu::mouseReleaseEvent(event);
  if (pressedThisCount && pressedArrow_ && isOverSubmenuArrow(action, position) && isVisible()) {
    setActiveAction(action);
  }
}

void LayoutMenu::keyPressEvent(QKeyEvent *event) {
  auto *action = activeAction();
  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
      hasCellCountSubmenu(action)) {
    event->accept();
    hide();
    action->trigger();
    return;
  }
  QMenu::keyPressEvent(event);
}

bool LayoutMenu::hasCellCountSubmenu(const QAction *action) const {
  return action && action->isEnabled() && action->actionGroup() == cellCounts_ && action->menu();
}

bool LayoutMenu::isOverSubmenuArrow(QAction *action, const QPoint &position) const {
  const QRect row = actionGeometry(action);
  const int width = row.height();
  const QRect arrowArea(row.right() - width + 1, row.top(), width, row.height());
  return QStyle::visualRect(layoutDirection(), row, arrowArea).contains(position);
}

}  // namespace heiran::ui
