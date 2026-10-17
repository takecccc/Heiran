#include "heiran/ui/AboutDialog.hpp"

#include <QPlainTextEdit>
#include <QTabWidget>
#include <QtTest>

using heiran::ui::licenseText;

class AboutDialogTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 配布するライセンスの文章が、実行ファイルへ埋め込まれていること。
  void embedsTheLicenseTexts() {
    const QString license = licenseText("LICENSE");
    QVERIFY(license.contains("Apache License"));
    QVERIFY(license.contains("Version 2.0"));
    // 著作権表示の雛形 ([yyyy] [name of copyright owner]) が埋まっている。
    QVERIFY(license.contains("Copyright 2026 Atelier Takec"));
    QVERIFY(!license.contains("[yyyy]"));

    const QString notice = licenseText("NOTICE");
    QVERIFY(notice.contains("Copyright 2026 Atelier Takec"));
    QVERIFY(notice.contains("THIRD_PARTY_NOTICES.txt"));

    QVERIFY(licenseText("missing.txt").isEmpty());
  }

  // 依存ライブラリの表記に、使っているライブラリと、Qt の LGPL の条件があること。
  void thirdPartyNoticesCoverTheDependencies() {
    const QString notices = licenseText("THIRD_PARTY_NOTICES.txt");
    QVERIFY(!notices.isEmpty());
    // vcpkg.json の依存と、その下の主なライブラリ。
    for (const char *name :
         {"Qt", "Little CMS", "libspng", "libwebp", "Zstandard", "libjxl", "OpenEXR", "Imath",
          "libtiff", "libavif", "dav1d", "libjpeg-turbo", "zlib", "Lucide"}) {
      QVERIFY2(notices.contains(QString::fromLatin1(name)), name);
    }
    // Qt の LGPL: 全文、動的リンク、入れ替え、ソースの場所。
    QVERIFY(notices.contains("GNU LESSER GENERAL PUBLIC LICENSE"));
    QVERIFY(notices.contains("GNU GENERAL PUBLIC LICENSE"));
    QVERIFY(notices.contains("dynamically"));
    QVERIFY(notices.contains("replace them"));
    QVERIFY(notices.contains("https://download.qt.io/"));
    // 各ライブラリの著作権表示が、そのまま入っている。
    QVERIFY(notices.contains("Copyright (c) 2010, Google Inc."));  // libwebp
    QVERIFY(notices.contains("Copyright 2019 Joe Drago"));         // libavif
    QVERIFY(notices.contains("Copyright (c) 2026 Lucide Icons and Contributors"));
  }

  void showsTheLicensesInTabs() {
    heiran::ui::AboutDialog dialog;
    auto *tabs = dialog.findChild<QTabWidget *>("licenseTabs");
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QString("License"));
    QCOMPARE(tabs->tabText(2), QString("Third-Party Licenses"));
    for (int index = 0; index < tabs->count(); ++index) {
      auto *view = qobject_cast<QPlainTextEdit *>(tabs->widget(index));
      QVERIFY(view != nullptr);
      QVERIFY(view->isReadOnly());
      QVERIFY(view->toPlainText().size() > 200);
    }
  }
};

QTEST_MAIN(AboutDialogTests)

#include "AboutDialogTests.moc"
