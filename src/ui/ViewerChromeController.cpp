#include "heiran/ui/ViewerChromeController.hpp"

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

namespace heiran::ui {

namespace {

class PageSelector final : public QComboBox {
public:
  explicit PageSelector(QWidget *parent) : QComboBox(parent) {
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    setMinimumWidth(96);
    setMaximumWidth(360);
    view()->setTextElideMode(Qt::ElideMiddle);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect field =
        style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
    option.currentText = fontMetrics().elidedText(option.currentText, Qt::ElideMiddle,
                                                  std::max(0, field.width() - 4));
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);
    painter.drawControl(QStyle::CE_ComboBoxLabel, option);
  }
};

}  // namespace

QComboBox *ViewerChromeController::createPageSelector(QWidget *parent) {
  return new PageSelector(parent);
}

ViewerChromeController::ViewerChromeController(QMainWindow *window, ViewerGridWidget *grid,
                                               QToolBar *navigation, QComboBox *pageSelector,
                                               QAction *previous, QAction *next,
                                               QAction *secondRowStart)
    : QObject(window), window_(window), navigation_(navigation),
      secondRow_(new QToolBar(navigation->windowTitle(), window)), secondRowStart_(secondRowStart),
      sourceSelector_(pageSelector) {
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

  secondRow_->setObjectName("navigationSecondRow");
  secondRow_->setMovable(false);
  secondRow_->setFloatable(false);
  secondRow_->toggleViewAction()->setVisible(false);
  window_->addToolBar(Qt::TopToolBarArea, secondRow_);
  window_->insertToolBarBreak(secondRow_);
  secondRow_->hide();
  window_->installEventFilter(this);
  navigation_->installEventFilter(this);
  secondRow_->installEventFilter(this);

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
  connect(navigation, &QToolBar::visibilityChanged, this, [this, compact](bool visible) {
    compact->setVisible(!visible);
    secondRow_->setVisible(visible && !wrappedActions_.isEmpty());
    scheduleNavigationLayout();
  });
  connect(compactSelector_, &QComboBox::currentIndexChanged, grid, [this, grid](int index) {
    if (index >= 0) {
      grid->setCurrentPage(compactSelector_->itemData(index).toInt());
    }
  });
  refreshPages();
}

void ViewerChromeController::setWrapNavigation(bool enabled) {
  wrapNavigation_ = enabled;
  scheduleNavigationLayout();
}

bool ViewerChromeController::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == window_ && event->type() == QEvent::Resize) ||
      ((watched == navigation_ || watched == secondRow_) &&
       event->type() == QEvent::LayoutRequest)) {
    scheduleNavigationLayout();
  }
  return QObject::eventFilter(watched, event);
}

void ViewerChromeController::scheduleNavigationLayout() {
  if (layoutPending_ || (!wrapNavigation_ && wrappedActions_.isEmpty()))
    return;
  layoutPending_ = true;
  QMetaObject::invokeMethod(
      this,
      [this]() {
        layoutPending_ = false;
        updateNavigationLayout();
      },
      Qt::QueuedConnection);
}

int ViewerChromeController::navigationWidthNeeded() const {
  const int selectorWidth =
      std::clamp(sourceSelector_->sizeHint().width(), sourceSelector_->minimumWidth(),
                 sourceSelector_->maximumWidth());
  const int secondRowWidth =
      wrappedActions_.isEmpty() ? 0 : secondRow_->sizeHint().width() + wrapWidthCorrection_;
  return navigation_->sizeHint().width() + secondRowWidth - selectorWidth +
         sourceSelector_->minimumWidth();
}

void ViewerChromeController::updateNavigationLayout() {
  const bool wrapped = !wrappedActions_.isEmpty();
  const int widthNeeded = navigationWidthNeeded();
  const bool shouldWrap = wrapNavigation_ && window_->contentsRect().width() < widthNeeded;
  if (shouldWrap == wrapped) {
    secondRow_->setVisible(navigation_->isVisible() && wrapped);
    return;
  }
  if (shouldWrap) {
    const auto actions = navigation_->actions();
    const auto split = actions.indexOf(secondRowStart_);
    if (split < 0)
      return;
    wrappedActions_ = actions.mid(split);
    for (auto *action : wrappedActions_) {
      navigation_->removeAction(action);
      secondRow_->addAction(action);
    }
    wrapWidthCorrection_ = 0;
    wrapWidthCorrection_ = widthNeeded - navigationWidthNeeded();
    secondRow_->setVisible(navigation_->isVisible());
  } else {
    secondRow_->hide();
    for (auto *action : wrappedActions_) {
      secondRow_->removeAction(action);
      navigation_->addAction(action);
    }
    wrappedActions_.clear();
  }
}

void ViewerChromeController::refreshPages() {
  QSignalBlocker blocker(compactSelector_);
  compactSelector_->clear();
  const int total = sourceSelector_->count();
  for (int index = 0; index < total; ++index) {
    compactSelector_->addItem(QString("%1 / %2").arg(index + 1).arg(total),
                              sourceSelector_->itemData(index));
    compactSelector_->setItemData(index, sourceSelector_->itemText(index), Qt::ToolTipRole);
  }
  compactSelector_->setCurrentIndex(sourceSelector_->currentIndex());
  compactSelector_->setEnabled(sourceSelector_->isEnabled());
  compactSelector_->setToolTip(sourceSelector_->currentText());
  sourceSelector_->setToolTip(sourceSelector_->currentText());
  scheduleNavigationLayout();
}

}  // namespace heiran::ui
