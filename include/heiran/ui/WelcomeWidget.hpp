#pragma once

#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
QT_END_NAMESPACE

namespace heiran::ui {

// 画像を 1 枚も開いていないときに、画像の面へ出す案内。何をすれば始められるかを示す
// (ドロップ、開くボタンとキー、マニュアル、最近開いた画像)。
//
// 親 (画像の面) の全体へ広がる。背景は塗らず、マウス操作は、案内の部品 (ボタン) だけが受ける。
// ファイルのドロップは、案内の上でも受けて、`filesDropped` で返す。
class WelcomeWidget : public QWidget {
  Q_OBJECT

public:
  static constexpr int kMaxRecentImages = 5;

  explicit WelcomeWidget(QWidget *parent);

  // 最近開いた画像 (新しい順)。多いときは、先頭の数件だけを出す。
  void setRecentImages(const QStringList &paths);

Q_SIGNALS:
  void openImageRequested();
  void recentImageRequested(const QString &path);
  void filesDropped(const QStringList &paths);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void rebuildRecentList();

  QWidget *recentContainer_ = nullptr;
  QVBoxLayout *recentLayout_ = nullptr;
  QStringList recentImages_;
};

}  // namespace heiran::ui
