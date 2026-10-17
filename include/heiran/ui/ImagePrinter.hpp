#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

class QPrinter;

namespace heiran::ui {

// 表示している画像を印刷する。
class ImagePrinter {
public:
  // 用紙の印刷できる範囲へ、縦横比を保って収まる最大の大きさで、中央に置く矩形。
  // 画像が用紙より小さくても拡大する。小さいまま隅へ置くと、印刷した意味が薄い。
  static QRect targetRect(const QSize &imageSize, const QRect &pageRect);

  // 画像の向きに合わせて用紙を縦か横にし、1 枚に収めて印刷する。
  // 印刷を始められなかった場合は false を返す。
  static bool print(QPrinter *printer, const QImage &image);
};

}  // namespace heiran::ui
