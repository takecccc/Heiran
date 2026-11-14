#include "heiran/ui/ViewerChromeController.hpp"

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace heiran::ui {

ViewerChromeController::ViewerChromeController(QMainWindow *window, ViewerGridWidget *grid,
                                               QToolBar *navigation, QComboBox *pageSelector,
                                               QAction *previous, QAction *next)
    : QObject(window), sourceSelector_(pageSelector) {
  auto *viewMenu = window->findChild<QMenu *>("viewMenu");
  auto *headers =
      viewMenu->addAction(QCoreApplication::translate("ViewerChromeController", "Cell Headers"));
  headers->setObjectName("cellHeadersAction");
  headers->setCheckable(true);
  headers->setChecked(QSettings().value("display/showCellHeaders", true).toBool());
  grid->setCellHeadersVisible(headers->isChecked());
  connect(headers, &QAction::toggled, this, [grid](bool visible) {
    grid->setCellHeadersVisible(visible);
    QSettings().setValue("display/showCellHeaders", visible);
  });
  viewMenu->addAction(navigation->toggleViewAction());

  auto *compact = new QWidget(window);
  compact->setObjectName("compactPageNavigation");
  auto *layout = new QHBoxLayout(compact);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *previousButton = new QToolButton(compact);
  previousButton->setObjectName("compactPreviousPage");
  previousButton->setDefaultAction(previous);
  previousButton->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(previousButton);
  compactSelector_ = new QComboBox(compact);
  compactSelector_->setObjectName("compactPageSelector");
  compactSelector_->setAccessibleName(
      QCoreApplication::translate("ViewerChromeController", "Page"));
  compactSelector_->setFocusPolicy(Qt::NoFocus);
  compactSelector_->setFixedWidth(76);
  layout->addWidget(compactSelector_);
  auto *nextButton = new QToolButton(compact);
  nextButton->setObjectName("compactNextPage");
  nextButton->setDefaultAction(next);
  nextButton->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(nextButton);
  window->statusBar()->addPermanentWidget(compact);
  compact->hide();
  connect(navigation, &QToolBar::visibilityChanged, compact,
          [compact](bool visible) { compact->setVisible(!visible); });
  connect(compactSelector_, &QComboBox::currentIndexChanged, grid, [grid](int index) {
    if (index >= 0) {
      grid->setCurrentPage(index);
    }
  });
  refreshPages();
}

void ViewerChromeController::refreshPages() {
  QSignalBlocker blocker(compactSelector_);
  compactSelector_->clear();
  const int total = sourceSelector_->count();
  for (int index = 0; index < total; ++index) {
    compactSelector_->addItem(QString("%1 / %2").arg(index + 1).arg(total));
    compactSelector_->setItemData(index, sourceSelector_->itemText(index), Qt::ToolTipRole);
  }
  compactSelector_->setCurrentIndex(sourceSelector_->currentIndex());
  compactSelector_->setEnabled(sourceSelector_->isEnabled());
  compactSelector_->setToolTip(sourceSelector_->currentText());
}

}  // namespace heiran::ui
