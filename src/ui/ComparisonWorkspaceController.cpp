#include "heiran/ui/ComparisonWorkspaceController.hpp"

#include "heiran/ui/ComparisonSetupDialog.hpp"
#include "heiran/ui/ImageComparisonWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <utility>

namespace heiran::ui {

ComparisonWorkspaceController::ComparisonWorkspaceController(QMainWindow *window,
                                                             ViewerGridWidget *viewer)
    : QObject(window), window_(window), viewer_(viewer), pages_(new QStackedWidget(window)) {
  pages_->addWidget(viewer_);
  window_->setCentralWidget(pages_);
}

void ComparisonWorkspaceController::openSetup() {
  if (isActive()) {
    return;
  }
  ComparisonSetupDialog setup(window_);
  if (setup.exec() != QDialog::Accepted) {
    return;
  }
  const auto paths = setup.paths();
  if (setup.comparesFolders()) {
    if (auto *comparison = start()) {
      comparison->openFolders(paths.at(0), paths.at(1));
    }
  } else {
    openImages(paths);
  }
}

void ComparisonWorkspaceController::openImages(const QStringList &paths) {
  if (paths.size() != 2) {
    return;
  }
  if (auto *comparison = start()) {
    comparison->openImages(paths);
  }
}

ImageComparisonWidget *ComparisonWorkspaceController::start() {
  if (isActive()) {
    return nullptr;
  }
  viewerWindowState_ = window_->saveState();
  viewerMenuVisible_ = !window_->menuBar()->isHidden();
  viewerStatusVisible_ = !window_->statusBar()->isHidden();
  viewerFocus_ = window_->focusWidget();
  for (auto *action : window_->actions()) {
    viewerActions_.append(action);
    window_->removeAction(action);
  }
  comparison_ = new ImageComparisonWidget(pages_);
  comparison_->setPixelValueFormat(viewer_->pixelValueFormat());
  connect(comparison_, &ImageComparisonWidget::endRequested, this,
          &ComparisonWorkspaceController::finish);
  pages_->addWidget(comparison_);
  pages_->setCurrentWidget(comparison_);
  window_->menuBar()->hide();
  window_->statusBar()->hide();
  for (auto *bar : window_->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
    bar->hide();
  }
  for (auto *dock : window_->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
    dock->hide();
  }
  comparison_->setFocus(Qt::OtherFocusReason);
  return comparison_;
}

void ComparisonWorkspaceController::finish() {
  auto *comparison = std::exchange(comparison_, nullptr);
  if (comparison == nullptr) {
    return;
  }
  window_->restoreState(viewerWindowState_);
  window_->menuBar()->setVisible(viewerMenuVisible_);
  window_->statusBar()->setVisible(viewerStatusVisible_);
  for (const auto &action : std::as_const(viewerActions_)) {
    if (action) {
      window_->addAction(action);
    }
  }
  viewerActions_.clear();
  viewerWindowState_.clear();
  pages_->setCurrentWidget(viewer_);
  pages_->removeWidget(comparison);
  comparison->deleteLater();
  if (viewerFocus_ && viewerFocus_->isVisibleTo(window_)) {
    viewerFocus_->setFocus();
  } else {
    viewer_->setFocus();
  }
  viewerFocus_.clear();
}

}  // namespace heiran::ui
