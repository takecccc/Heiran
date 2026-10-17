#include "heiran/ui/DisplayProfile.hpp"

#include <QDir>
#include <QFile>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

class DisplayProfileTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void convertsModeToAndFromStrings() {
    QCOMPARE(heiran::ui::displayProfileModeFromString("srgb"),
             heiran::ui::DisplayProfileMode::SRgb);
    QCOMPARE(heiran::ui::displayProfileModeFromString(" File "),
             heiran::ui::DisplayProfileMode::File);
    // 知らない値は画面の設定に従う扱いにする。
    QCOMPARE(heiran::ui::displayProfileModeFromString("nonsense"),
             heiran::ui::DisplayProfileMode::System);
    QCOMPARE(heiran::ui::displayProfileModeToString(heiran::ui::DisplayProfileMode::File),
             QString("file"));
  }

  // sRGB を選んだ場合は画面の設定を見ないこと。
  void ignoresTheScreenForTheSrgbMode() {
    bool systemWasAsked = false;
    const auto provider = [&systemWasAsked](const QScreen *) {
      systemWasAsked = true;
      return QColorSpace(QColorSpace::DisplayP3);
    };

    const QColorSpace resolved = heiran::ui::resolveDisplayColorSpace(
        heiran::ui::DisplayProfileMode::SRgb, QString(), nullptr, provider);
    QVERIFY(!systemWasAsked);
    QCOMPARE(resolved, QColorSpace(QColorSpace::SRgb));
  }

  // 画面の設定を使う場合はそれを返すこと。
  void usesTheScreenProfile() {
    const auto provider = [](const QScreen *) { return QColorSpace(QColorSpace::DisplayP3); };
    const QColorSpace resolved = heiran::ui::resolveDisplayColorSpace(
        heiran::ui::DisplayProfileMode::System, QString(), nullptr, provider);
    QCOMPARE(resolved, QColorSpace(QColorSpace::DisplayP3));
  }

  // 画面の設定が取れない場合は sRGB へ退避すること。表示は続けたい。
  void fallsBackToSrgbWhenTheScreenHasNoProfile() {
    const auto provider = [](const QScreen *) { return QColorSpace(); };
    const QColorSpace resolved = heiran::ui::resolveDisplayColorSpace(
        heiran::ui::DisplayProfileMode::System, QString(), nullptr, provider);
    QCOMPARE(resolved, QColorSpace(QColorSpace::SRgb));
  }

  // 指定した ICC ファイルを読めること。
  void readsAProfileFromAFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = QDir(directory.path()).filePath("p3.icc");

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QColorSpace(QColorSpace::DisplayP3).iccProfile());
    file.close();

    QCOMPARE(heiran::ui::colorSpaceFromIccFile(filePath).primaries(),
             QColorSpace::Primaries::DciP3D65);

    const QColorSpace resolved = heiran::ui::resolveDisplayColorSpace(
        heiran::ui::DisplayProfileMode::File, filePath, nullptr);
    QCOMPARE(resolved.primaries(), QColorSpace::Primaries::DciP3D65);
  }

  // 読めない経路を指していても表示は続けること。
  void fallsBackToSrgbForAnUnreadableFile() {
    const QColorSpace resolved = heiran::ui::resolveDisplayColorSpace(
        heiran::ui::DisplayProfileMode::File, "does-not-exist.icc", nullptr);
    QCOMPARE(resolved, QColorSpace(QColorSpace::SRgb));
    QVERIFY(!heiran::ui::colorSpaceFromIccFile(QString()).isValid());
  }

  // 実際の画面から取れるかどうかは環境による。取れた場合に妥当なものであること、
  // 取れない場合でも落ちないことだけを確かめる。
  void readsTheScreenProfileWithoutFailing() {
    heiran::ui::clearDisplayProfileCache();
    const QColorSpace fromScreen = heiran::ui::systemDisplayColorSpace(nullptr);
    if (!fromScreen.isValid()) {
      QSKIP("this screen has no ICC profile assigned");
    }
    QVERIFY(fromScreen.isValid());

    // 2 度目は取っておいたものが返ること。
    QCOMPARE(heiran::ui::systemDisplayColorSpace(nullptr), fromScreen);
  }
};

QTEST_MAIN(DisplayProfileTests)

#include "DisplayProfileTests.moc"
