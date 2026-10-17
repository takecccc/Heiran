#pragma once

#include <QString>
#include <functional>

QT_BEGIN_NAMESPACE
class QMainWindow;
QT_END_NAMESPACE

namespace heiran::ui {

class DirtyStateController {
public:
  explicit DirtyStateController(QMainWindow *window);

  bool isDirty() const;
  void setSessionFilePath(const QString &sessionFilePath);

  // タイトルへ出す名前を差し替える。画像を 1 枚だけ見ている場合に、
  // セッション名ではなくそのファイル名を出すために使う。空なら従来どおり。
  void setDisplayNameOverride(const QString &displayName);
  void setDirty(bool dirty);
  bool confirmContinueWithUnsavedChanges(const std::function<bool()> &saveAttempt) const;

private:
  void updateWindowTitle() const;

  QMainWindow *window_ = nullptr;
  QString sessionFilePath_;
  QString displayNameOverride_;
  bool dirty_ = false;
};

}  // namespace heiran::ui
