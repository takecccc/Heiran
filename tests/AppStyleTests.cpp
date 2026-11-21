// 画面全体の見た目の取り決め (AppStyle)。

#include "heiran/ui/AppStyle.hpp"

#include <QtTest>

using heiran::ui::AppStyleTokens;
using heiran::ui::buildAppStyleSheet;

class AppStyleTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 明るい配色と暗い配色で、色が違うこと。同じなら、切り替えても見た目が変わらない。
  void hasDifferentColorsForLightAndDark() {
    const auto dark = AppStyleTokens::forDarkScheme(true);
    const auto light = AppStyleTokens::forDarkScheme(false);

    QVERIFY(dark.panel.lightness() < 128);
    QVERIFY(light.panel.lightness() > 128);
    QVERIFY(dark.text.lightness() > dark.panel.lightness());
    QVERIFY(light.text.lightness() < light.panel.lightness());
  }

  // 文字が読めること。下地と文字の明るさに、十分な差がある。
  void keepsTextReadableAgainstThePanel() {
    for (const bool dark : {true, false}) {
      const auto tokens = AppStyleTokens::forDarkScheme(dark);
      QVERIFY(std::abs(tokens.text.lightness() - tokens.panel.lightness()) > 100);
      // 補助の文字も、下地から離れている。
      QVERIFY(std::abs(tokens.dimText.lightness() - tokens.panel.lightness()) > 50);
    }
  }

  void keepsInputFieldsDistinct() {
    for (const bool dark : {true, false}) {
      const auto tokens = AppStyleTokens::forDarkScheme(dark);
      QVERIFY(std::abs(tokens.inputBorder.lightness() - tokens.input.lightness()) > 50);
      QVERIFY(std::abs(tokens.inputBorder.lightness() - tokens.panel.lightness()) > 50);
      const QString sheet = buildAppStyleSheet(tokens);
      QVERIFY(sheet.contains("QLineEdit:focus { border: 2px solid"));
      QVERIFY(sheet.contains("QMainWindow, QDialog"));
    }
  }

  // スタイルシートに、置き換え損ねた印 (%1 など) が残らないこと。
  void leavesNoUnresolvedPlaceholders() {
    for (const bool dark : {true, false}) {
      const QString sheet = buildAppStyleSheet(AppStyleTokens::forDarkScheme(dark));
      QVERIFY(!sheet.isEmpty());
      QVERIFY2(!sheet.contains(QRegularExpression(R"(%\d+)")), qPrintable(sheet));
    }
  }

  // 主要な部品を覆っていること。
  void coversTheMainWidgets() {
    const QString sheet = buildAppStyleSheet(AppStyleTokens::forDarkScheme(true));
    for (const char *selector : {"QMenuBar", "QMenu", "QToolBar", "QToolButton", "QStatusBar",
                                 "QPushButton", "QGroupBox"}) {
      QVERIFY2(sheet.contains(selector), selector);
    }
  }
};

QTEST_MAIN(AppStyleTests)

#include "AppStyleTests.moc"
