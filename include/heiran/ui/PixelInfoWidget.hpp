#pragma once

#include <QStringList>
#include <QWidget>

namespace heiran::ui {

// 画素値表示の行を並べる。等幅で描いて桁を縦に揃える。
class PixelInfoWidget final : public QWidget {
public:
  explicit PixelInfoWidget(QWidget *parent = nullptr);

  void setLines(const QStringList &lines);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QStringList lines_;
};

}  // namespace heiran::ui
