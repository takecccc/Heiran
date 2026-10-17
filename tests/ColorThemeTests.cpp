#include "heiran/ui/ColorTheme.hpp"

#include <QGuiApplication>
#include <QStyleHints>
#include <QtTest>

namespace {

// 明るい下地と暗い下地のパレットを組み立てる。
QPalette makePalette(const QColor &base, const QColor &text) {
  QPalette palette;
  palette.setColor(QPalette::Base, base);
  palette.setColor(QPalette::Text, text);
  return palette;
}

int contrastBetween(const QColor &left, const QColor &right) {
  return std::abs(left.lightness() - right.lightness());
}

}  // namespace

class ColorThemeTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void convertsToAndFromStrings() {
    QCOMPARE(heiran::ui::colorThemeFromString("dark"), heiran::ui::ColorTheme::Dark);
    QCOMPARE(heiran::ui::colorThemeFromString(" Light "), heiran::ui::ColorTheme::Light);
    // 知らない値は OS の設定に従う扱いにする。
    QCOMPARE(heiran::ui::colorThemeFromString("nonsense"), heiran::ui::ColorTheme::System);
    QCOMPARE(heiran::ui::colorThemeToString(heiran::ui::ColorTheme::Dark), QString("dark"));
  }

  // 指定した配色が Qt へ渡ること。ウィジェットの見た目は Qt に任せている。
  void appliesTheSchemeToQt() {
    // offscreen のプラットフォームは、配色の切り替えに対応していない (常に Unknown)。
    if (QGuiApplication::platformName() == "offscreen") {
      QSKIP("The offscreen platform does not support color schemes.");
    }
    auto *styleHints = QGuiApplication::styleHints();
    QVERIFY(styleHints != nullptr);

    heiran::ui::applyColorTheme(heiran::ui::ColorTheme::Dark);
    QCOMPARE(styleHints->colorScheme(), Qt::ColorScheme::Dark);

    heiran::ui::applyColorTheme(heiran::ui::ColorTheme::Light);
    QCOMPARE(styleHints->colorScheme(), Qt::ColorScheme::Light);

    // System へ戻すと、OS 側の値が返る。何であれ Qt が決めた値になる。
    heiran::ui::applyColorTheme(heiran::ui::ColorTheme::System);
    const Qt::ColorScheme systemScheme = styleHints->colorScheme();
    QVERIFY(systemScheme == Qt::ColorScheme::Light || systemScheme == Qt::ColorScheme::Dark ||
            systemScheme == Qt::ColorScheme::Unknown);
  }

  // グラフの下地はパレットの下地をそのまま使うこと。
  void takesTheChartBackgroundFromThePalette() {
    const auto light = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(250, 250, 250), QColor(20, 20, 20)));
    QCOMPARE(light.background, QColor(250, 250, 250));

    const auto dark = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(24, 24, 26), QColor(235, 235, 235)));
    QCOMPARE(dark.background, QColor(24, 24, 26));
  }

  // 文字と下地の明暗が十分に離れること。どちらの配色でも読める必要がある。
  void keepsTextReadableOnBothBackgrounds() {
    const auto light = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(250, 250, 250), QColor(20, 20, 20)));
    const auto dark = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(24, 24, 26), QColor(235, 235, 235)));

    for (const auto &chart : {light, dark}) {
      QVERIFY2(contrastBetween(chart.titleText, chart.background) > 100,
               qPrintable(QString("title contrast %1")
                              .arg(contrastBetween(chart.titleText, chart.background))));
      QVERIFY2(
          contrastBetween(chart.axisText, chart.background) > 40,
          qPrintable(
              QString("axis contrast %1").arg(contrastBetween(chart.axisText, chart.background))));
      QVERIFY2(contrastBetween(chart.seriesLabel, chart.background) > 80,
               qPrintable(QString("label contrast %1")
                              .arg(contrastBetween(chart.seriesLabel, chart.background))));
    }
  }

  // 目盛りの線は下地から少しだけ離すこと。濃すぎると読み取りの邪魔になる。
  void keepsGridLinesSubtle() {
    const auto light = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(250, 250, 250), QColor(20, 20, 20)));
    const int gridContrast = contrastBetween(light.gridLine, light.background);
    QVERIFY2(gridContrast > 5 && gridContrast < 60,
             qPrintable(QString("grid contrast %1").arg(gridContrast)));
    // 文字より薄いこと。
    QVERIFY(gridContrast < contrastBetween(light.axisText, light.background));
  }

  // 暗い下地では曲線を明るくすること。同じ色のままだと沈んで見えない。
  void brightensCurvesOnADarkBackground() {
    const auto light = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(250, 250, 250), QColor(20, 20, 20)));
    const auto dark = heiran::ui::AnalysisChartPalette::fromPalette(
        makePalette(QColor(24, 24, 26), QColor(235, 235, 235)));

    QVERIFY(dark.redCurve.lightness() > light.redCurve.lightness());
    QVERIFY(dark.greenCurve.lightness() > light.greenCurve.lightness());
    QVERIFY(dark.blueCurve.lightness() > light.blueCurve.lightness());
  }

  // 3 チャネルの色は互いに見分けが付くこと。
  void keepsTheChannelCurvesDistinct() {
    for (const auto &base : {QColor(250, 250, 250), QColor(24, 24, 26)}) {
      const auto chart = heiran::ui::AnalysisChartPalette::fromPalette(
          makePalette(base, base.lightness() < 128 ? QColor(235, 235, 235) : QColor(20, 20, 20)));
      QVERIFY(chart.redCurve != chart.greenCurve);
      QVERIFY(chart.greenCurve != chart.blueCurve);
      QVERIFY(chart.redCurve != chart.blueCurve);
      // 灰色表示の曲線とも別であること。
      QVERIFY(chart.grayscaleCurve != chart.redCurve);
    }
  }
};

QTEST_MAIN(ColorThemeTests)

#include "ColorThemeTests.moc"
