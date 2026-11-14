#include "heiran/ui/SettingsHelpController.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QWidget>

namespace heiran::ui {

SettingsHelpController::SettingsHelpController(QWidget *dialog, QLabel *title,
                                               QPlainTextEdit *description)
    : QObject(dialog), dialog_(dialog), title_(title), description_(description) {
  dialog->setMouseTracking(true);
  for (auto *widget : dialog->findChildren<QWidget *>()) {
    widget->setMouseTracking(true);
  }
  qApp->installEventFilter(this);
}

SettingsHelpController::~SettingsHelpController() {
  qApp->removeEventFilter(this);
}

void SettingsHelpController::setHelp(QWidget *widget, const QString &title,
                                     const QString &description) {
  entries_.insert(widget, {.title = title, .description = description});
  widget->setAccessibleDescription(description);
}

bool SettingsHelpController::eventFilter(QObject *watched, QEvent *event) {
  const auto type = event->type();
  if (type != QEvent::Enter && type != QEvent::MouseMove && type != QEvent::FocusIn &&
      type != QEvent::ToolTip) {
    return false;
  }
  auto *widget = qobject_cast<QWidget *>(watched);
  if (widget == nullptr || !dialog_->isAncestorOf(widget)) {
    return false;
  }
  if (type == QEvent::MouseMove || type == QEvent::Enter) {
    const QPoint global = type == QEvent::MouseMove
                              ? static_cast<QMouseEvent *>(event)->globalPosition().toPoint()
                              : static_cast<QEnterEvent *>(event)->globalPosition().toPoint();
    if (auto *underPointer = dialog_->childAt(dialog_->mapFromGlobal(global))) {
      widget = underPointer;
    }
  }
  for (; widget != nullptr && widget != dialog_; widget = widget->parentWidget()) {
    const auto entry = entries_.constFind(widget);
    if (entry != entries_.cend()) {
      if (title_->text() != entry->title || description_->toPlainText() != entry->description) {
        title_->setText(entry->title);
        description_->setPlainText(entry->description);
      }
      return type == QEvent::ToolTip;
    }
  }
  return false;
}

}  // namespace heiran::ui
