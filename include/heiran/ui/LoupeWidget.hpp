#pragma once

#include "heiran/ui/PixelValueFormat.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QWidget>

namespace heiran::ui {

// マウス位置の周囲を升目で拡大して見せる。塗りは表示用画像から、数値は
// 解析用バッファから取る。
class LoupeWidget final : public QWidget {
public:
  explicit LoupeWidget(QWidget *parent = nullptr);

  void setPixelValueFormat(PixelValueFormat pixelValueFormat);
  void setTransparencyBackground(const TransparencyBackground &background);
  void setPreview(const LoupePreview &preview);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  LoupePreview preview_;
  TransparencyBackground transparencyBackground_;
  PixelValueFormat pixelValueFormat_ = PixelValueFormat::Normalized255;
};

}  // namespace heiran::ui
