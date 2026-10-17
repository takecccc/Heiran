#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace heiran::app {

// 表示言語の設定値。"auto" は OS の表示言語に従う。
inline constexpr auto kLanguageAuto = "auto";
inline constexpr auto kLanguageEnglish = "en";
inline constexpr auto kLanguageJapanese = "ja";

// 設定値 (auto / en / ja。ほかは auto 扱い) と OS の表示言語の並び (例: "ja-JP", "en-US") から、
// 使う言語を決める。返すのは "en" か "ja"。auto
// では、並びの先頭から見て、最初に対応している言語になる (どれも無ければ "en")。
QString resolveLanguage(const QString &setting, const QStringList &systemUiLanguages);

// 使う言語の翻訳 (アプリの文言と Qt 標準の文言) を、アプリへ入れる。英語では、何も入れない。
// 文言は起動のときに決まる。実行中に切り替えることはしない。返すのは、決めた言語。
QString installTranslators(QCoreApplication &application, const QString &setting);

}  // namespace heiran::app
