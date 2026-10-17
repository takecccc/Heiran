#include "heiran/app/Localization.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QtTest>

class LocalizationTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void explicitSettingWinsOverTheSystemLanguage() {
    QCOMPARE(heiran::app::resolveLanguage("ja", {"en-US"}), QString("ja"));
    QCOMPARE(heiran::app::resolveLanguage("en", {"ja-JP"}), QString("en"));
    QCOMPARE(heiran::app::resolveLanguage(" JA ", {}), QString("ja"));
  }

  void autoFollowsTheFirstSupportedSystemLanguage() {
    QCOMPARE(heiran::app::resolveLanguage("auto", {"ja-JP", "en-US"}), QString("ja"));
    QCOMPARE(heiran::app::resolveLanguage("auto", {"en-US", "ja-JP"}), QString("en"));
    // 対応していない言語は飛ばして、次を見る。
    QCOMPARE(heiran::app::resolveLanguage("auto", {"de-DE", "ja"}), QString("ja"));
  }

  void fallsBackToEnglish() {
    QCOMPARE(heiran::app::resolveLanguage("auto", {}), QString("en"));
    QCOMPARE(heiran::app::resolveLanguage("auto", {"de-DE", "fr-FR"}), QString("en"));
    QCOMPARE(heiran::app::resolveLanguage("klingon", {"fr-FR"}), QString("en"));
  }
  // 日本語の翻訳は、すべて訳してあり、%1 などの置き換えの番号が、原文と同じであること。
  // 訳し忘れは、英語のまま出て、気付きにくい。番号の食い違いは、実行時に、値が入らない /
  // 余る不具合になる。
  void everyJapaneseMessageIsTranslatedWithTheSamePlaceholders() {
    QFile file(QStringLiteral(HEIRAN_TRANSLATION_SOURCE));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    QXmlStreamReader xml(&file);
    const QRegularExpression placeholder("%(\\d+)");
    const auto placeholders = [&placeholder](const QString &text) {
      QSet<QString> numbers;
      auto matches = placeholder.globalMatch(text);
      while (matches.hasNext()) {
        numbers.insert(matches.next().captured(1));
      }
      return numbers;
    };

    int messageCount = 0;
    QString source;
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement()) {
        continue;
      }
      if (xml.name() == u"source") {
        source = xml.readElementText();
      } else if (xml.name() == u"translation") {
        ++messageCount;
        const bool unfinished = xml.attributes().hasAttribute("type");
        const QString translation = xml.readElementText();
        QVERIFY2(!unfinished && !translation.isEmpty(), qPrintable("Not translated: " + source));
        QVERIFY2(placeholders(translation) == placeholders(source),
                 qPrintable("Placeholders differ: " + source));
      }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QVERIFY(messageCount > 100);
  }

  // 日本語を選ぶと、アプリの文言と、Qt 標準の文言が、日本語になる。英語では、そのまま。
  void translatesWhenJapaneseIsSelected() {
    QCOMPARE(QCoreApplication::translate("MainWindow", "&File"), QString("&File"));
    QCOMPARE(heiran::app::installTranslators(*QCoreApplication::instance(), "ja"), QString("ja"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "&File"), QString::fromUtf8("ファイル(&F)"));
  }
};

QTEST_MAIN(LocalizationTests)
#include "LocalizationTests.moc"
