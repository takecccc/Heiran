#include "heiran/ui/DirtyStateController.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>

namespace heiran::ui {

DirtyStateController::DirtyStateController(QMainWindow *window) : window_(window) {}

bool DirtyStateController::isDirty() const {
  return dirty_;
}

void DirtyStateController::setSessionFilePath(const QString &sessionFilePath) {
  sessionFilePath_ = sessionFilePath;
  updateWindowTitle();
}

void DirtyStateController::setDirty(bool dirty) {
  if (dirty_ == dirty) {
    return;
  }

  dirty_ = dirty;
  if (window_ != nullptr) {
    window_->setWindowModified(dirty_);
  }
  updateWindowTitle();
}

bool DirtyStateController::confirmContinueWithUnsavedChanges(
    const std::function<bool()> &saveAttempt) const {
  if (!dirty_) {
    return true;
  }

  const QMessageBox::StandardButton result = QMessageBox::warning(
      window_, QCoreApplication::translate("DirtyStateController", "Unsaved Changes"),
      QCoreApplication::translate("DirtyStateController", "There are unsaved changes. Save them?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (result == QMessageBox::Cancel) {
    return false;
  }
  if (result == QMessageBox::Discard) {
    return true;
  }

  return saveAttempt ? saveAttempt() : false;
}

void DirtyStateController::setDisplayNameOverride(const QString &displayName) {
  if (displayNameOverride_ == displayName) {
    return;
  }
  displayNameOverride_ = displayName;
  updateWindowTitle();
}

void DirtyStateController::updateWindowTitle() const {
  if (window_ == nullptr) {
    return;
  }

  QString displayName = sessionFilePath_.isEmpty()
                            ? QCoreApplication::translate("DirtyStateController", "Untitled")
                            : QFileInfo(sessionFilePath_).fileName();
  if (!displayNameOverride_.isEmpty()) {
    displayName = displayNameOverride_;
  }
  window_->setWindowFilePath(sessionFilePath_);
  window_->setWindowTitle(
      QCoreApplication::translate("DirtyStateController", "%1[*] - Heiran").arg(displayName));
}

}  // namespace heiran::ui
