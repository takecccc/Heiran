#include "heiran/ui/FileDragController.hpp"

#include <QApplication>
#include <QDrag>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QUrl>
#include <QWidget>

namespace heiran::ui {

FileDragController::FileDragController(QWidget *source) : QObject(source), source_(source) {
  source_->installEventFilter(this);
}

void FileDragController::setSourceFile(const QString &path) {
  if (path_ != path) {
    pendingPath_.clear();
    path_ = path;
  }
}

void FileDragController::executeDrag(QDrag &drag, Qt::DropActions actions) {
  drag.exec(actions, Qt::CopyAction);
}

bool FileDragController::eventFilter(QObject *watched, QEvent *event) {
  if (watched != source_) {
    return false;
  }
  if (event->type() == QEvent::MouseButtonPress) {
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton) {
      return false;
    }
    pendingPath_.clear();
    if (mouse->buttons() != Qt::LeftButton || mouse->modifiers() != Qt::ControlModifier) {
      return false;
    }
    const QFileInfo file(path_);
    if (!path_.isEmpty() && file.isFile()) {
      pendingPath_ = file.absoluteFilePath();
      pressPosition_ = mouse->position().toPoint();
    }
    event->accept();
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::UngrabMouse ||
      event->type() == QEvent::Hide) {
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) {
      return false;
    }
    const bool pending = !pendingPath_.isEmpty();
    pendingPath_.clear();
    return pending && event->type() == QEvent::MouseButtonRelease;
  }
  if (event->type() != QEvent::MouseMove || pendingPath_.isEmpty()) {
    return false;
  }
  const auto *mouse = static_cast<QMouseEvent *>(event);
  if (!(mouse->buttons() & Qt::LeftButton)) {
    pendingPath_.clear();
    return false;
  }
  event->accept();
  if ((mouse->position().toPoint() - pressPosition_).manhattanLength() <
      QApplication::startDragDistance()) {
    return true;
  }
  const QString path = pendingPath_;
  pendingPath_.clear();
  if (QFileInfo(path).isFile()) {
    QPointer<QDrag> drag = new QDrag(source_);
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    drag->setMimeData(mime);
    executeDrag(*drag, Qt::CopyAction);
    if (drag) {
      drag->deleteLater();
    }
  }
  return true;
}

}  // namespace heiran::ui
