#include "heiran/ui/CellStatusPainter.hpp"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>

namespace heiran::ui {

namespace {

constexpr int kReloadButtonWidth = 96;
constexpr int kReloadButtonHeight = 26;

constexpr QColor kEmptyTextColor(140, 146, 156);
constexpr QColor kPanelColor(64, 38, 40);
constexpr QColor kHeadlineColor(255, 186, 186);
constexpr QColor kFileNameColor(232, 226, 226);
constexpr QColor kReasonColor(196, 190, 190);
constexpr QColor kButtonColor(104, 62, 64);
constexpr QColor kButtonTextColor(245, 240, 240);

void paintFailure(QPainter *painter, const QRect &canvasRect, const CellStatusInfo &statusInfo) {
  const QRect panelRect(canvasRect.center().x() - 190, canvasRect.center().y() - 60, 380, 120);
  painter->setPen(Qt::NoPen);
  painter->setBrush(kPanelColor);
  painter->drawRoundedRect(panelRect, 6, 6);

  QFont headlineFont = painter->font();
  headlineFont.setBold(true);
  painter->setFont(headlineFont);
  painter->setPen(kHeadlineColor);
  painter->drawText(QRect(panelRect.left(), panelRect.top() + 10, panelRect.width(), 20),
                    Qt::AlignHCenter | Qt::AlignVCenter, cellStatusHeadline(statusInfo));

  QFont bodyFont = painter->font();
  bodyFont.setBold(false);
  painter->setFont(bodyFont);
  painter->setPen(kFileNameColor);
  const QString fileNameText = painter->fontMetrics().elidedText(
      statusInfo.fileName, Qt::ElideMiddle, panelRect.width() - 24);
  painter->drawText(QRect(panelRect.left() + 12, panelRect.top() + 34, panelRect.width() - 24, 18),
                    Qt::AlignHCenter | Qt::AlignVCenter, fileNameText);

  painter->setPen(kReasonColor);
  const QString reasonText =
      painter->fontMetrics().elidedText(statusInfo.reason, Qt::ElideRight, panelRect.width() - 24);
  painter->drawText(QRect(panelRect.left() + 12, panelRect.top() + 54, panelRect.width() - 24, 18),
                    Qt::AlignHCenter | Qt::AlignVCenter, reasonText);

  const QRect buttonRect = cellReloadButtonRect(canvasRect.size(), statusInfo.status);
  painter->setPen(Qt::NoPen);
  painter->setBrush(kButtonColor);
  painter->drawRoundedRect(buttonRect, 4, 4);
  painter->setPen(kButtonTextColor);
  painter->drawText(buttonRect, Qt::AlignCenter,
                    QCoreApplication::translate("CellStatusPainter", "Reload"));
}

// 空のセル。点線の枠と一言で、ここへ画像を落とせると伝える。
void paintEmpty(QPainter *painter, const QRect &canvasRect) {
  const QRect frame = canvasRect.adjusted(16, 16, -16, -16);
  painter->setRenderHint(QPainter::Antialiasing, true);
  QPen pen(QColor(86, 91, 100), 1.5, Qt::DashLine);
  pen.setDashPattern({4.0, 4.0});
  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  painter->drawRoundedRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

  QFont titleFont = painter->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
  painter->setFont(titleFont);
  painter->setPen(kEmptyTextColor);
  painter->drawText(frame.adjusted(0, 0, 0, -10), Qt::AlignCenter,
                    QCoreApplication::translate("CellStatusPainter", "Drop images here"));

  QFont hintFont = painter->font();
  hintFont.setPointSizeF(hintFont.pointSizeF() / 1.15 * 0.9);
  painter->setFont(hintFont);
  painter->setPen(QColor(110, 115, 124));
  painter->drawText(frame.adjusted(0, 34, 0, 0), Qt::AlignHCenter | Qt::AlignVCenter,
                    QCoreApplication::translate("CellStatusPainter", "or use File > Open"));
}

}  // namespace

QRect cellReloadButtonRect(const QSize &canvasSize, CellStatus status) {
  if (status != CellStatus::Failed) {
    return {};
  }

  return QRect((canvasSize.width() - kReloadButtonWidth) / 2, canvasSize.height() / 2 + 28,
               kReloadButtonWidth, kReloadButtonHeight);
}

void paintCellStatus(QPainter *painter, const QRect &canvasRect, const CellStatusInfo &statusInfo) {
  if (painter == nullptr || statusInfo.status == CellStatus::Ready) {
    return;
  }

  painter->save();
  if (statusInfo.status == CellStatus::Failed) {
    paintFailure(painter, canvasRect, statusInfo);
    painter->restore();
    return;
  }

  if (statusInfo.status == CellStatus::Empty && canvasRect.width() >= 200 &&
      canvasRect.height() >= 120) {
    paintEmpty(painter, canvasRect);
    painter->restore();
    return;
  }

  painter->setPen(kEmptyTextColor);
  const QString headline = cellStatusHeadline(statusInfo);
  const QString text =
      statusInfo.reason.isEmpty() ? headline : QString("%1\n%2").arg(headline, statusInfo.reason);
  painter->drawText(canvasRect, Qt::AlignCenter, text);
  painter->restore();
}

}  // namespace heiran::ui
