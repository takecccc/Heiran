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
// 親 (画像の面) の全体へ広がり、下の空セルの案内を覆う。内容が収まらなければスクロールする。
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
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void rebuildRecentList();
  void updateRecentButtonTexts();

  QWidget *recentContainer_ = nullptr;
  QVBoxLayout *recentLayout_ = nullptr;
  QStringList recentImages_;
};

}  // namespace heiran::ui
