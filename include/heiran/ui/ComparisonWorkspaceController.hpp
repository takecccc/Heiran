#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QAction;
class QMainWindow;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

namespace heiran::ui {

class ImageComparisonWidget;
class ViewerGridWidget;

class ComparisonWorkspaceController final : public QObject {
public:
  ComparisonWorkspaceController(QMainWindow *window, ViewerGridWidget *viewer);

  void openSetup();
  void openImages(const QStringList &paths);
  void finish();
  bool isActive() const { return comparison_ != nullptr; }

private:
  ImageComparisonWidget *start();

  QMainWindow *window_;
  ViewerGridWidget *viewer_;
  QStackedWidget *pages_;
  ImageComparisonWidget *comparison_ = nullptr;
  QByteArray viewerWindowState_;
  QList<QPointer<QAction>> viewerActions_;
  QPointer<QWidget> viewerFocus_;
  bool viewerMenuVisible_ = true;
  bool viewerStatusVisible_ = true;
};

}  // namespace heiran::ui
