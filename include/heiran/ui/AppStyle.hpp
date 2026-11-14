#pragma once

#include <QColor>
#include <QString>

namespace heiran::ui {

// 画面全体の見た目 (余白、角丸、ホバー、強調色) の取り決め。
//
// 配色そのもの (ウィンドウや文字の色) は Qt の配色に任せる。ここは、標準のままだと味気ない
// 部分 (ツールバー、メニュー、ステータス、ボタン、グループ) を、画像を邪魔しない控えめな
// 見た目へ揃える。明るい配色でも暗い配色でも、同じ形で色だけが変わる。
struct AppStyleTokens {
  QColor window;
  QColor panel;
  QColor border;
  QColor text;
  QColor dimText;
  QColor hover;
  QColor pressed;
  QColor accent;
  QColor accentSoft;
  QColor accentText;
  QColor input;

  static AppStyleTokens forDarkScheme(bool dark);
  static QColor viewerBackground();
};

// トークンからアプリ全体のスタイルシートを作る。
QString buildAppStyleSheet(const AppStyleTokens &tokens);

// アプリ全体のスタイルシートを、いまの配色に合わせて設定する。配色が切り替わったら、
// 自動で作り直す (初回の呼び出しで接続する)。
void installAppStyle();

// 暗い配色か。Qt のパレットから判断する。
bool isDarkPalette();

}  // namespace heiran::ui
