#pragma once

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QString>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCloseEvent;
QT_END_NAMESPACE

namespace heiran::ui {

// 解析ウィンドウの共通部分。ヒストグラムと差分が同じ扱いになるようにする。
//
// - 閉じても破棄せず隠すだけにする。次に開いたときに操作の状態を保つため。
// - 隠れている間は計算しない。全画素を走査するため、見えていない窓のために
//   ページ送りが遅くなるのは割に合わない。隠れている間に届いた分は保持して、
//   次に開いたときにまとめて 1 度だけ計算する。
// - 表題へページ番号を入れる。どのページを見ているかが窓だけで分かる。
class AnalysisWindowBase : public QWidget {
public:
  // `titlePrefix` は表題の先頭。"Histogram" のように機能の名前を渡す。
  explicit AnalysisWindowBase(QString titlePrefix, QWidget *parent = nullptr);

  // 解析対象を差し替える。隠れている間は保持だけして計算しない。
  void setAnalysisImages(int pageNumber, const QVector<AnalysisImage> &images);

  // 前面へ出す。隠れている間に届いた分があればここで計算する。
  void showAndRaise();

protected:
  void closeEvent(QCloseEvent *event) override;

  // 解析対象が変わったときに呼ばれる。窓が見えているときだけ呼ばれる。
  virtual void analysisImagesChanged() = 0;

  int pageNumber() const { return pageNumber_; }
  const QVector<AnalysisImage> &analysisImages() const { return images_; }

private:
  QString titlePrefix_;
  int pageNumber_ = 1;
  QVector<AnalysisImage> images_;
  // 隠れている間に解析対象が届いたかどうか。
  bool refreshPending_ = false;
};

}  // namespace heiran::ui
