#include "heiran/ui/ColorTheme.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QGuiApplication>
#include <QStyleHints>

namespace heiran::ui {

namespace {

// 2 色を混ぜる。`ratio` が 0 で `from`、1 で `to`。
QColor blend(const QColor &from, const QColor &to, double ratio) {
  const double keep = 1.0 - ratio;
  return QColor(static_cast<int>(from.red() * keep + to.red() * ratio),
                static_cast<int>(from.green() * keep + to.green() * ratio),
                static_cast<int>(from.blue() * keep + to.blue() * ratio));
}

// 暗い下地の上では曲線を明るくする。同じ色をそのまま使うと沈んで見えない。
QColor curveColor(const QColor &base, bool darkBackground) {
  return darkBackground ? base.lighter(125) : base;
}

}  // namespace

QString colorThemeToString(ColorTheme theme) {
  switch (theme) {
    case ColorTheme::Light:
      return "light";
    case ColorTheme::Dark:
      return "dark";
    case ColorTheme::Slate:
      return "slate";
    case ColorTheme::Sand:
      return "sand";
    case ColorTheme::Sakura:
      return "sakura";
    case ColorTheme::Custom:
      return "custom";
    case ColorTheme::System:
    default:
      return "system";
  }
}

ColorTheme colorThemeFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == "light") {
    return ColorTheme::Light;
  }
  if (normalized == "dark") {
    return ColorTheme::Dark;
  }
  if (normalized == "slate") {
    return ColorTheme::Slate;
  }
  if (normalized == "sand") {
    return ColorTheme::Sand;
  }
  if (normalized == "sakura") {
    return ColorTheme::Sakura;
  }
  if (normalized == "custom") {
    return ColorTheme::Custom;
  }
  return ColorTheme::System;
}

void applyColorTheme(ColorTheme theme, const CustomColorTheme &custom) {
  auto *styleHints = QGuiApplication::styleHints();
  if (styleHints == nullptr) {
    return;
  }

  switch (theme) {
    case ColorTheme::Light:
    case ColorTheme::Sand:
    case ColorTheme::Sakura:
      styleHints->setColorScheme(Qt::ColorScheme::Light);
      break;
    case ColorTheme::Dark:
    case ColorTheme::Slate:
      styleHints->setColorScheme(Qt::ColorScheme::Dark);
      break;
    case ColorTheme::Custom:
      styleHints->setColorScheme(custom.dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
      break;
    case ColorTheme::System:
    default:
      styleHints->unsetColorScheme();
      break;
  }

  // 見た目の取り決め (余白、角丸、強調色)。配色に合わせて作り直す。以後の切り替えにも追従する。
  installAppStyle(theme, custom);
}

AnalysisChartPalette AnalysisChartPalette::fromPalette(const QPalette &palette) {
  const QColor background = palette.color(QPalette::Base);
  const QColor text = palette.color(QPalette::Text);
  // 下地が暗いかどうかで、線の明るさの向きが変わる。
  const bool darkBackground = background.lightness() < 128;

  return {
      .background = background,
      .frame = blend(background, text, 0.45),
      // 目盛りの線は下地から少しだけ離す。読み取りの邪魔をしない濃さにする。
      .gridLine = blend(background, text, 0.12),
      .axisText = blend(background, text, 0.55),
      .titleText = text,
      .seriesLabel = blend(background, text, 0.85),
      .statisticsText = blend(background, text, 0.75),
      .grayscaleCurve = blend(background, text, 0.7),
      .redCurve = curveColor(QColor(220, 68, 55), darkBackground),
      .greenCurve = curveColor(QColor(50, 160, 80), darkBackground),
      .blueCurve = curveColor(QColor(66, 133, 244), darkBackground),
  };
}

}  // namespace heiran::ui
