#include "heiran/app/OpenGlCheck.hpp"

#include <QGuiApplication>
#include <QtTest>

class OpenGlCheckTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // offscreen のプラットフォームは、OpenGL を持たない。使えない環境では、理由を、説明として返す (何が要るかと、どうすればよいか)。
  void explainsWhyOpenGlCannotBeUsed() {
    if (QGuiApplication::platformName() != "offscreen") {
      QSKIP("Needs a platform without OpenGL (offscreen).");
    }
    const QString message = heiran::app::checkOpenGlSupport();
    QVERIFY(!message.isEmpty());
    QVERIFY(message.contains("OpenGL 3.0"));
    QVERIFY(message.contains("driver"));
  }

  // 使える環境では、空 (何も知らせない)。
  void staysSilentWhenOpenGlWorks() {
    if (QGuiApplication::platformName() == "offscreen") {
      QSKIP("Needs a platform with OpenGL.");
    }
    QVERIFY2(heiran::app::checkOpenGlSupport().isEmpty(),
             qPrintable(heiran::app::checkOpenGlSupport()));
  }
};

QTEST_MAIN(OpenGlCheckTests)
#include "OpenGlCheckTests.moc"
