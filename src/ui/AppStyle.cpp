#include "heiran/ui/AppStyle.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace heiran::ui {

namespace {

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
        .input = QColor(26, 28, 32),
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
  };
}

QColor AppStyleTokens::viewerBackground() {
  return QColor(56, 60, 66);
}

QString buildAppStyleSheet(const AppStyleTokens &t) {
  // 角丸と余白は、要素をまたいで揃える。
  return QString(R"(
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
QToolButton:disabled { color: %6; }
QToolButton::menu-indicator { image: none; }
QToolButton[exposureActive="true"] { background-color: %7; color: %8; border-color: %9; }

QStatusBar { background-color: %1; color: %6; border-top: 1px solid %3; }
QStatusBar::item { border: none; }

QDockWidget { color: %2; }
QDockWidget::title { background-color: %1; padding: 6px 8px; border-bottom: 1px solid %3; }

QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 5px 16px; min-width: 64px; }
QPushButton:hover { background-color: %4; }
QPushButton:pressed { background-color: %5; }
QPushButton:default { border-color: %9; }
QPushButton:disabled { color: %6; }
QPushButton[imageListCompact="true"] { padding: 5px 6px; }
QToolButton#imageListFavoritesOnly { border-color: %3; }
QToolButton#imageListFavoritesOnly:checked { border-color: %9; }

QGroupBox { border: 1px solid %3; border-radius: 6px; margin-top: 12px; padding: 12px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 6px; color: %6; }

QLineEdit { background-color: %10; color: %2; border: 1px solid %3; border-radius: 4px; padding: 3px 6px; selection-background-color: %9; }
QLineEdit:focus { border-color: %9; }

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
      .arg(css(AppStyleTokens::viewerBackground()));             // %12
}

void installAppStyle() {
  static bool connected = false;
  auto *hints = QGuiApplication::styleHints();
  const auto apply = []() {
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
      // 配色が切り替わった直後は、パレットの更新が後になる。判断は Qt の配色から行う。
      bool dark = isDarkPalette();
      if (auto *styleHints = QGuiApplication::styleHints()) {
        switch (styleHints->colorScheme()) {
          case Qt::ColorScheme::Dark:
            dark = true;
            break;
          case Qt::ColorScheme::Light:
            dark = false;
            break;
          case Qt::ColorScheme::Unknown:
          default:
            break;
        }
      }
      app->setStyleSheet(buildAppStyleSheet(AppStyleTokens::forDarkScheme(dark)));
    }
  };
  apply();
  if (!connected && hints != nullptr) {
    connected = true;
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, qApp, [apply]() { apply(); });
  }
}

}  // namespace heiran::ui
