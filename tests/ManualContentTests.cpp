#include "heiran/app/Localization.hpp"
#include "heiran/ui/MainWindow.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QtTest>

namespace {

// アクセラレーターの印 (&F、(&F)) を除く。
QString plain(QString text) {
  static const QRegularExpression accelerator(R"(\(&.\))");
  text.remove(accelerator);
  text.remove('&');
  return text.trimmed();
}

const QAction *findChild(const QList<QAction *> &actions, const QString &title) {
  for (const auto *action : actions) {
    if (plain(action->text()) == title) {
      return action;
    }
  }
  return nullptr;
}

// 「File > Open Image...」のような道筋が、実際のメニューにあるか。
bool menuPathExists(const QMenuBar *menuBar, const QStringList &path) {
  const QAction *current = findChild(menuBar->actions(), path.front());
  for (int index = 1; current != nullptr && index < path.size(); ++index) {
    if (current->menu() == nullptr) {
      return false;
    }
    current = findChild(current->menu()->actions(), path[index]);
  }
  return current != nullptr;
}

QString readManual(const QString &name) {
  QFile file(QDir(HEIRAN_MANUAL_SOURCE_DIR).filePath(name));
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QStringList manualFiles() {
  return QDir(HEIRAN_MANUAL_SOURCE_DIR).entryList({"*.md"}, QDir::Files);
}

}  // namespace

// マニュアルの記述が、実際の画面とずれていないかを確かめる。
// メニューの名前は、日本語の表示の名前を太字で書き、初めて出るところへ、英語の名前を括弧で添える
// (**表示 > 前のセル** (View > Next
// Cell))。日本語の道筋は日本語の画面で、括弧の中の道筋は英語の画面で確かめる。
class ManualContentTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 太字で書いたメニューの道筋が、すべて実在する。括弧で添えた英語の道筋も。
  void everyMenuPathInTheManualExists() {
    QVERIFY2(!manualFiles().isEmpty(), "manual sources not found");
    const QRegularExpression boldPath(
        R"(\*\*([^*\n]+(?: > [^*\n]+)+)\*\*(?: \(([A-Za-z][^()\n]*)\))?)");

    // 英語の画面。翻訳を入れる前に作る。
    QStringList missing;
    int checked = 0;
    {
      heiran::ui::MainWindow english;
      for (const QString &name : manualFiles()) {
        auto matches = boldPath.globalMatch(readManual(name));
        while (matches.hasNext()) {
          const auto match = matches.next();
          if (match.captured(2).isEmpty() || !match.captured(2).contains(" > ")) {
            continue;
          }
          const QStringList path = match.captured(2).split(" > ");
          ++checked;
          if (!menuPathExists(english.menuBar(), path)) {
            missing.push_back(name + ": " + path.join(" > "));
          }
        }
      }
    }
    QVERIFY2(missing.isEmpty(), qPrintable("missing English menu items: " + missing.join("; ")));
    QVERIFY(checked > 0);

    // 日本語の画面。
    QCOMPARE(heiran::app::installTranslators(*QCoreApplication::instance(), "ja"), QString("ja"));
    heiran::ui::MainWindow japanese;
    checked = 0;
    for (const QString &name : manualFiles()) {
      auto matches = boldPath.globalMatch(readManual(name));
      while (matches.hasNext()) {
        const QStringList path = matches.next().captured(1).split(" > ");
        ++checked;
        if (!menuPathExists(japanese.menuBar(), path)) {
          missing.push_back(name + ": " + path.join(" > "));
        }
      }
    }
    QVERIFY2(missing.isEmpty(), qPrintable("missing Japanese menu items: " + missing.join("; ")));
    QVERIFY(checked > 0);
  }

  // メニューの道筋は、太字で書く (書き方の統一)。太字でない「X > Y」を残さない
  // (括弧の中の英語の道筋は、除く)。
  void menuPathsAreAlwaysBold() {
    const QRegularExpression plainPath(
        R"((?<!\*)\b(File|Images|Layout|View|Analysis|Help|ファイル|画像|レイアウト|表示|解析|ヘルプ) > [A-Za-z0-9\x{3040}-\x{9fff}][^*]*)");
    QStringList offenders;
    for (const QString &name : manualFiles()) {
      for (const QString &line : readManual(name).split('\n')) {
        // 太字の中と、括弧で添えた英語の名前を取り除いてから探す。
        QString rest = line;
        rest.remove(QRegularExpression(R"(\*\*[^*]*\*\*)"));
        rest.remove(QRegularExpression(R"(\([A-Za-z][^()]*\))"));
        if (plainPath.match(rest).hasMatch()) {
          offenders.push_back(name + ": " + line.trimmed());
        }
      }
    }
    QVERIFY2(offenders.isEmpty(), qPrintable("not bold: " + offenders.join("; ")));
  }
};

QTEST_MAIN(ManualContentTests)
#include "ManualContentTests.moc"
