#pragma once

#include <QLabel>
#include <QTimer>

namespace heiran::ui {

// 画像の上へ短く出るメッセージ。全画面ではステータスバーが隠れるため、ページの移動や
// 操作の結果を知らせる場所が無くなる。その代わりに使う。
//
// マウスを通すため、出ている間も画像への操作を妨げない。一定時間で自分で消える。
class TransientMessageWidget : public QLabel {
public:
  static constexpr int kDefaultDurationMs = 1800;

  explicit TransientMessageWidget(QWidget *parent);

  // 表示を始める。出ている最中に呼ぶと、内容を差し替えて消えるまでの時間を延ばす。
  void showMessage(const QString &text, int durationMs = kDefaultDurationMs);

private:
  void placeInCorner();

  QTimer hideTimer_;
};

}  // namespace heiran::ui
