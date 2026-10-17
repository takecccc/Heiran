#pragma once

#include <QColorSpace>
#include <QString>
#include <functional>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace heiran::ui {

// 表示先の色空間をどう決めるか。
enum class DisplayProfileMode {
  // 画面に割り当てられているプロファイルを使う。取れなければ sRGB。
  System,
  // 常に sRGB として扱う。画面の設定を疑う場合や、他の環境と見え方を
  // そろえたい場合に使う。
  SRgb,
  // 指定した ICC ファイルを使う。
  File,
};

QString displayProfileModeToString(DisplayProfileMode mode);
DisplayProfileMode displayProfileModeFromString(const QString &text);

// 画面に割り当てられている ICC プロファイルを読む。取れなければ無効を返す。
//
// Windows では画面ごとに別のプロファイルを割り当てられるため、対象の画面を渡す。
// 画面が null の場合は既定の画面を使う。
//
// ファイルを読むため、結果は画面ごとに取っておく。取り直したい場合は
// `clearDisplayProfileCache()` を呼ぶ。
QColorSpace systemDisplayColorSpace(const QScreen *screen);

// ICC ファイルを読む。読めなければ無効を返す。
QColorSpace colorSpaceFromIccFile(const QString &filePath);

// 設定と画面から表示先の色空間を決める。決められない場合は sRGB を返す。
//
// `systemProvider` を渡すと画面のプロファイルの取得を差し替えられる。
// 実際の画面に依存せずに決め方を確かめるために開けてある。
QColorSpace resolveDisplayColorSpace(
    DisplayProfileMode mode, const QString &profilePath, const QScreen *screen,
    const std::function<QColorSpace(const QScreen *)> &systemProvider = {});

// 取っておいたプロファイルを捨てる。画面の設定を変えた後に呼ぶ。
void clearDisplayProfileCache();

}  // namespace heiran::ui
