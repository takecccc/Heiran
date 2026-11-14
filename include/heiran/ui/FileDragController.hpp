#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

class QDrag;
class QWidget;

namespace heiran::ui {

class FileDragController : public QObject {
public:
  explicit FileDragController(QWidget *source);
  void setSourceFile(const QString &path);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  virtual void executeDrag(QDrag &drag, Qt::DropActions actions);

private:
  QWidget *source_;
  QString path_;
  QString pendingPath_;
  QPoint pressPosition_;
};

}  // namespace heiran::ui
