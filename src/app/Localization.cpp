#include "heiran/app/Localization.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace heiran::app {

QString resolveLanguage(const QString &setting, const QStringList &systemUiLanguages) {
  const QString requested = setting.trimmed().toLower();
  if (requested == kLanguageEnglish || requested == kLanguageJapanese) {
    return requested;
  }
  for (const QString &language : systemUiLanguages) {
    const QString primary = language.left(2).toLower();
    if (primary == kLanguageJapanese || primary == kLanguageEnglish) {
      return primary;
    }
  }
  return kLanguageEnglish;
}

QString installTranslators(QCoreApplication &application, const QString &setting) {
  const QString language = resolveLanguage(setting, QLocale::system().uiLanguages());
  if (language == kLanguageEnglish) {
    return language;
  }

  // アプリの文言は、実行ファイルへ埋め込んである。
  auto *appTranslator = new QTranslator(&application);
  if (appTranslator->load(QStringLiteral(":/i18n/heiran_%1.qm").arg(language))) {
    application.installTranslator(appTranslator);
  } else {
    delete appTranslator;
  }

  // Qt 標準の文言 (ファイルダイアログ、標準のボタンなど)。配布物では、bin の隣の translations
  // に置く。
  const QStringList directories = {
      QDir(application.applicationDirPath()).filePath(QStringLiteral("../translations")),
      QDir(application.applicationDirPath()).filePath(QStringLiteral("translations")),
      QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
  for (const QString &directory : directories) {
    auto *qtTranslator = new QTranslator(&application);
    if (qtTranslator->load(QStringLiteral("qt_%1").arg(language), directory)) {
      application.installTranslator(qtTranslator);
      break;
    }
    delete qtTranslator;
  }
  return language;
}

}  // namespace heiran::app
