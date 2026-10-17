#include "heiran/ui/PixelInfoWidget.hpp"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

namespace heiran::ui {

PixelInfoWidget::PixelInfoWidget(QWidget *parent) : QWidget(parent) {
  setMinimumWidth(380);
  QFont textFont = font();
  textFont.setFamilies({"Consolas", "Courier New", "monospace"});
  setFont(textFont);
}

void PixelInfoWidget::setLines(const QStringList &lines) {
  lines_ = lines;
  update();
}

void PixelInfoWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().base());

  painter.setPen(palette().text().color());
  const QStringList displayLines =
      lines_.isEmpty() ? QStringList{QCoreApplication::translate(
                             "PixelInfoWidget", "Hover over an image to inspect pixel values.")}
                       : lines_;

  const QFontMetrics metrics(font());
  const int left = 8;
  int y = 8 + metrics.ascent();
  for (const auto &line : displayLines) {
    painter.drawText(left, y, line);
    y += metrics.height();
    if (y > height()) {
      break;
    }
  }
}

}  // namespace heiran::ui
