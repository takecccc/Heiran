#pragma once

#include <QPalette>
#include <QString>

namespace heiran::ui {

// 画面の配色。
enum class ColorTheme {
  // OS の設定に従う。既定。
  System,
  Light,
  Dark,
  Slate,
  Sand,
  Sakura,
};

QString colorThemeToString(ColorTheme theme);
ColorTheme colorThemeFromString(const QString &text);

// 配色を適用する。`System` では OS の設定へ戻す。
//
// ウィジェットの配色は Qt が持つ仕組みへ委ねる。自前でパレットを組むと、
// 標準のダイアログやメニューだけ浮くことになる。
void applyColorTheme(ColorTheme theme);

// 解析グラフの配色。
//
// グラフは `QPainter` で自前に描くため、配色を切り替えても自動では追従しない。
// 色をここへ集めて、ウィジェットのパレットから導く。
//
// 画像を出す面 (セルの背景、ルーペ) はこれに従わない。画像を見比べるための
// 中立な下地であって、配色の一部ではないため。
struct AnalysisChartPalette {
  QColor background;
  QColor frame;
  QColor gridLine;
  QColor axisText;
  QColor titleText;
  QColor seriesLabel;
  QColor statisticsText;
  QColor grayscaleCurve;
  QColor redCurve;
  QColor greenCurve;
  QColor blueCurve;

  static AnalysisChartPalette fromPalette(const QPalette &palette);
};

}  // namespace heiran::ui
