#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QStringList>
#include <QThreadPool>

QT_BEGIN_NAMESPACE
class QListWidget;
QT_END_NAMESPACE

namespace heiran::ui {

// ページのサムネイル一覧。選んだページへ飛ぶために使う。
//
// 多くのページを持つ画像セットで、目当ての 1 枚を探すのに要る。ページ送りで
// 1 枚ずつ見ていくより速い。
class PageThumbnailDialog : public QDialog {
  Q_OBJECT

public:
  explicit PageThumbnailDialog(QWidget *parent = nullptr);
  ~PageThumbnailDialog() override;

  // ページごとの代表ファイル。空の要素は読めるファイルが無いページを表す。
  void setPages(const QStringList &pagePaths, int currentPage);

  // 選ばれたページ。選ばれなかった場合は負を返す。
  int selectedPage() const { return selectedPage_; }

private:
  void startLoading(const QStringList &pagePaths);
  void applyThumbnail(int index, const QImage &thumbnail);

  QListWidget *listWidget_ = nullptr;
  int selectedPage_ = -1;

  // サムネイルの読み込みは専用のスレッドプールで行う。
  //
  // 復号の中で `QtConcurrent::blockingMap` を使う経路があり、共有のプールで
  // 動かすと、外側の仕事がプールを埋めた状態で内側が空きを待つことになる。
  QThreadPool loaderPool_;
  QFutureWatcher<QImage> loaderWatcher_;
};

}  // namespace heiran::ui
