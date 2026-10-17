#include "heiran/ui/AnalysisWindowBase.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <utility>

namespace heiran::ui {

AnalysisWindowBase::AnalysisWindowBase(QString titlePrefix, QWidget *parent)
    : QWidget(parent, Qt::Window), titlePrefix_(std::move(titlePrefix)) {
  setWindowTitle(titlePrefix_);
}

void AnalysisWindowBase::setAnalysisImages(int pageNumber, const QVector<AnalysisImage> &images) {
  pageNumber_ = pageNumber;
  images_ = images;
  setWindowTitle(QCoreApplication::translate("AnalysisWindowBase", "%1  Page %2")
                     .arg(titlePrefix_)
                     .arg(pageNumber_));

  if (!isVisible()) {
    refreshPending_ = true;
    return;
  }

  refreshPending_ = false;
  analysisImagesChanged();
}

void AnalysisWindowBase::showAndRaise() {
  show();
  raise();
  activateWindow();

  if (refreshPending_) {
    refreshPending_ = false;
    analysisImagesChanged();
  }
}

void AnalysisWindowBase::closeEvent(QCloseEvent *event) {
  // 破棄せずに隠す。操作の状態を次に開いたときまで保つ。
  hide();
  event->ignore();
}

}  // namespace heiran::ui
