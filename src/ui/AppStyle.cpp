#include "heiran/ui/AppStyle.hpp"
#include "heiran/ui/ColorTheme.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace heiran::ui {

namespace {

ColorTheme activeTheme = ColorTheme::System;
CustomColorTheme activeCustomTheme;

QColor mix(const QColor &base, const QColor &shade, double weight) {
  return QColor::fromRgbF(base.redF() + (shade.redF() - base.redF()) * weight,
                          base.greenF() + (shade.greenF() - base.greenF()) * weight,
                          base.blueF() + (shade.blueF() - base.blueF()) * weight);
}

QString css(const QColor &color) {
  return QString("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

}  // namespace

bool isDarkPalette() {
  return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

AppStyleTokens AppStyleTokens::forDarkScheme(bool dark) {
  if (dark) {
    return {
        .window = QColor(31, 33, 37),
        .panel = QColor(38, 40, 45),
        .border = QColor(56, 59, 66),
        .text = QColor(216, 219, 224),
        .dimText = QColor(142, 148, 158),
        .hover = QColor(52, 55, 62),
        .pressed = QColor(62, 66, 74),
        .accent = QColor(92, 156, 245),
        .accentSoft = QColor(92, 156, 245, 48),
        .accentText = QColor(150, 195, 255),
        .input = QColor(45, 49, 56),
        .inputBorder = QColor(105, 115, 130),
    };
  }
  return {
      .window = QColor(243, 244, 246),
      .panel = QColor(235, 237, 241),
      .border = QColor(210, 214, 220),
      .text = QColor(32, 35, 41),
      .dimText = QColor(106, 112, 121),
      .hover = QColor(224, 228, 234),
      .pressed = QColor(212, 217, 225),
      .accent = QColor(47, 111, 224),
      .accentSoft = QColor(47, 111, 224, 34),
      .accentText = QColor(30, 86, 192),
      .input = QColor(255, 255, 255),
      .inputBorder = QColor(130, 141, 156),
  };
}

AppStyleTokens AppStyleTokens::current() {
  switch (activeTheme) {
    case ColorTheme::Custom:
      return forCustomTheme(activeCustomTheme);
    case ColorTheme::Slate:
      return {
          .window = QColor(27, 35, 43),
          .panel = QColor(35, 47, 58),
          .border = QColor(63, 78, 91),
          .text = QColor(224, 232, 239),
          .dimText = QColor(154, 173, 183),
          .hover = QColor(49, 64, 75),
          .pressed = QColor(58, 76, 88),
          .accent = QColor(112, 181, 174),
          .accentSoft = QColor(112, 181, 174, 42),
          .accentText = QColor(165, 223, 213),
          .input = QColor(42, 55, 66),
          .inputBorder = QColor(113, 137, 151),
      };
    case ColorTheme::Sand:
      return {
          .window = QColor(249, 246, 239),
          .panel = QColor(244, 238, 227),
          .border = QColor(214, 202, 184),
          .text = QColor(47, 40, 32),
          .dimText = QColor(108, 93, 79),
          .hover = QColor(233, 224, 209),
          .pressed = QColor(223, 210, 190),
          .accent = QColor(156, 106, 57),
          .accentSoft = QColor(156, 106, 57, 37),
          .accentText = QColor(116, 72, 34),
          .input = QColor(255, 252, 246),
          .inputBorder = QColor(151, 129, 102),
      };
    case ColorTheme::Sakura:
      return {
          .window = QColor(253, 246, 249),
          .panel = QColor(249, 236, 241),
          .border = QColor(219, 197, 207),
          .text = QColor(55, 38, 48),
          .dimText = QColor(112, 88, 101),
          .hover = QColor(242, 223, 232),
          .pressed = QColor(232, 207, 220),
          .accent = QColor(157, 75, 105),
          .accentSoft = QColor(157, 75, 105, 38),
          .accentText = QColor(118, 45, 75),
          .input = QColor(255, 251, 253),
          .inputBorder = QColor(151, 118, 135),
      };
    case ColorTheme::Light:
      return forDarkScheme(false);
    case ColorTheme::Dark:
      return forDarkScheme(true);
    case ColorTheme::System:
      if (const auto *hints = QGuiApplication::styleHints()) {
        if (hints->colorScheme() != Qt::ColorScheme::Unknown)
          return forDarkScheme(hints->colorScheme() == Qt::ColorScheme::Dark);
      }
      return forDarkScheme(isDarkPalette());
  }
  return forDarkScheme(isDarkPalette());
}

AppStyleTokens AppStyleTokens::forCustomTheme(const CustomColorTheme &custom) {
  auto tokens = forDarkScheme(custom.dark);
  const auto apply = [](QColor &target, const QColor &color) {
    if (color.isValid()) {
      target = color.toRgb();
      target.setAlpha(255);
    }
  };
  apply(tokens.window, custom.window);
  apply(tokens.panel, custom.panel);
  apply(tokens.input, custom.input);
  apply(tokens.text, custom.text);
  apply(tokens.accent, custom.accent);
  tokens.border = mix(tokens.panel, tokens.text, 0.23);
  tokens.inputBorder = mix(tokens.input, tokens.text, 0.48);
  tokens.dimText = mix(tokens.window, tokens.text, 0.65);
  tokens.hover = mix(tokens.panel, tokens.text, 0.12);
  tokens.pressed = mix(tokens.panel, tokens.text, 0.19);
  tokens.accentSoft = tokens.accent;
  tokens.accentSoft.setAlpha(42);
  tokens.accentText = mix(tokens.accent, tokens.text, 0.58);
  return tokens;
}

QColor AppStyleTokens::viewerBackground() {
  return QColor(56, 60, 66);
}

QString buildAppStyleSheet(const AppStyleTokens &t) {
  // 角丸と余白は、要素をまたいで揃える。
  return QString(R"(
QMainWindow, QDialog { background-color: %13; color: %2; }
QLabel, QCheckBox, QRadioButton { color: %2; }
QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled { color: %6; }
QPlainTextEdit, QTextEdit { background-color: %10; color: %2; }

QToolTip {
  background-color: %1; color: %2; border: 1px solid %3; padding: 4px 6px;
}

QLabel#favoriteBadge { color: %11; background-color: %12; border-radius: 4px; font-size: 18px; }

QMenuBar { background-color: %1; color: %2; padding: 2px 6px; border-bottom: 1px solid %3; }
QMenuBar::item { background: transparent; padding: 4px 8px; border-radius: 4px; }
QMenuBar::item:selected { background-color: %4; }
QMenuBar::item:pressed { background-color: %5; }

QMenu { background-color: %1; color: %2; border: 1px solid %3; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 12px; border-radius: 4px; }
QMenu::item:selected { background-color: %4; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }

QToolBar { background-color: %1; border: none; border-bottom: 1px solid %3; spacing: 2px; padding: 4px 8px; }
QToolBar::separator { background-color: %3; width: 1px; margin: 4px 6px; }
QToolBar QLabel { color: %6; padding: 0 4px; }
QToolButton { background: transparent; color: %2; border: 1px solid transparent; border-radius: 4px; padding: 4px 10px; }
QToolBar#navigationToolBar QToolButton, QToolBar#navigationSecondRow QToolButton { padding: 4px 8px; }
QToolButton:hover { background-color: %4; }
QToolButton:pressed { background-color: %5; }
QToolButton:checked { background-color: %7; color: %8; border-color: %9; }
QToolButton[thumbnailPreset="true"] { padding: 4px 5px; }
QToolButton[comparisonChoice="true"]:!checked { border-color: %3; }
QToolButton[comparisonChoice="true"]:focus { border-color: %9; }
QToolButton[comparisonChoice="true"]:checked:disabled { background-color: %4; border-color: %3; }
QToolButton:disabled { color: %6; }
QToolButton::menu-indicator { image: none; }
QToolButton[exposureActive="true"] { background-color: %7; color: %8; border-color: %9; }

QStatusBar { background-color: %1; color: %6; border-top: 1px solid %3; }
QStatusBar::item { border: none; }

QDockWidget { background-color: %13; color: %2; }
QDockWidget::title { background-color: %1; padding: 6px 8px; border-bottom: 1px solid %3; }

QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 5px 16px; min-width: 64px; }
QPushButton:hover { background-color: %4; }
QPushButton:pressed { background-color: %5; }
QPushButton:default:enabled { border-color: %9; }
QPushButton:disabled { color: %6; }
QPushButton#compareSelectedImages { padding: 5px 6px; }
QToolButton#imageListFavoritesOnly { border-color: %3; }
QToolButton#imageListFavoritesOnly:checked { border-color: %9; }
QPushButton:focus, QToolButton:focus { border-color: %9; }

QGroupBox { border: 1px solid %3; border-radius: 6px; margin-top: 12px; padding: 12px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 6px; color: %2; }

QAbstractItemView { selection-background-color: %7; selection-color: %8; }
QListWidget#settingsCategories { background-color: %1; color: %2; border-color: %3; outline: none; }
QListWidget#settingsCategories::item { border-left: 3px solid transparent; padding: 0 12px; }
QListWidget#settingsCategories::item:selected { background-color: %7; color: %8; border-left-color: %9; }
QListWidget#settingsCategories::item:focus:!selected { border-left-color: %9; }

QLineEdit {
  background-color: %10; color: %2; border: 1px solid %14; border-radius: 4px;
  padding: 3px 6px; selection-background-color: %7; selection-color: %8;
}
QLineEdit:focus { border: 2px solid %9; padding: 2px 5px; }
QLineEdit:disabled { background-color: %1; color: %6; border-color: %3; }
QComboBox, QAbstractSpinBox { background-color: %10; color: %2; }
QComboBox:disabled, QAbstractSpinBox:disabled { background-color: %1; color: %6; }
QComboBox QLineEdit, QComboBox QLineEdit:focus,
QAbstractSpinBox QLineEdit, QAbstractSpinBox QLineEdit:focus {
  background: transparent; border: none; padding: 0;
}

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %3; min-height: 24px; border-radius: 4px; margin: 1px; }
QScrollBar::handle:vertical:hover { background: %6; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %3; min-width: 24px; border-radius: 4px; margin: 1px; }
QScrollBar::handle:horizontal:hover { background: %6; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)")
      .arg(css(t.panel))                                         // %1
      .arg(css(t.text))                                          // %2
      .arg(css(t.border))                                        // %3
      .arg(css(t.hover))                                         // %4
      .arg(css(t.pressed))                                       // %5
      .arg(css(t.dimText))                                       // %6
      .arg(css(t.accentSoft))                                    // %7
      .arg(css(t.accentText))                                    // %8
      .arg(css(t.accent))                                        // %9
      .arg(css(t.input))                                         // %10
      .arg(css(AppStyleTokens::forDarkScheme(true).accentText))  // %11
      .arg(css(AppStyleTokens::viewerBackground()))              // %12
      .arg(css(t.window))                                        // %13
      .arg(css(t.inputBorder));                                  // %14
}

void installAppStyle(ColorTheme theme, const CustomColorTheme &custom) {
  activeTheme = theme;
  activeCustomTheme = custom;
  static bool connected = false;
  auto *hints = QGuiApplication::styleHints();
  const auto apply = []() {
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
      const auto sheet = buildAppStyleSheet(AppStyleTokens::current());
      if (app->styleSheet() != sheet)
        app->setStyleSheet(sheet);
    }
  };
  apply();
  if (!connected && hints != nullptr) {
    connected = true;
    QObject::connect(
        hints, &QStyleHints::colorSchemeChanged, qApp, [apply]() { apply(); },
        Qt::QueuedConnection);
  }
}

}  // namespace heiran::ui
