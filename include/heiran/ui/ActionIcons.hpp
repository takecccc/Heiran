#pragma once

#include <QHash>
#include <QIcon>
#include <QString>

class QWidget;

namespace heiran::ui {

// 配色に追従する線画のアイコン (`:/icons/<name>.svg`)。
//
// SVG を描くたびに、いまのパレットの文字色で塗る。明るい配色と暗い配色の切り替えにも、
// 無効状態にも追従するため、配色ごとに別の画像を用意しなくてよい。
// チェック中 (QIcon::On) は強調色で塗る。
QIcon themedIcon(const QString &name);

// 項目の文字と、付けるアイコンの対応。役割が文字から明らかなものだけに付ける。
const QHash<QString, QString> &actionIconNames();

// `root` の下にある、アイコンを持たない項目 (メニュー、ツールバー) へ、対応表に従って付ける。
void applyActionIcons(QWidget *root);

}  // namespace heiran::ui
