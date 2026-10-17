#include "heiran/ui/TransientMessageWidget.hpp"

namespace heiran::ui {

namespace {

constexpr int kMargin = 16;

}  // namespace

TransientMessageWidget::TransientMessageWidget(QWidget *parent) : QLabel(parent) {
  setObjectName("transientMessage");
  // 配色は画像の上に重なるため、テーマに関わらず暗い下地に白い文字で固定する。
  setStyleSheet("QLabel { background-color: rgba(0, 0, 0, 170); color: white;"
                " border-radius: 6px; padding: 6px 12px; }");
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);
  hide();

  hideTimer_.setSingleShot(true);
  connect(&hideTimer_, &QTimer::timeout, this, &QWidget::hide);
}

void TransientMessageWidget::showMessage(const QString &text, int durationMs) {
  setText(text);
  adjustSize();
  placeInCorner();
  raise();
  show();
  hideTimer_.start(durationMs);
}

void TransientMessageWidget::placeInCorner() {
  move(kMargin, kMargin);
}

}  // namespace heiran::ui
