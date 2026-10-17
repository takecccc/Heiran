#include "heiran/app/CrashReporter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// 子プロセスとして起動されたときの合図。書き出し先を続けて渡す。
constexpr const char *kCrashArgument = "--crash-into";

}  // namespace

class CrashReporterTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 書き出し先を作れること。落ちてから作るのでは間に合わない。
  void createsTheDumpDirectoryUpFront() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = QDir(directory.path()).filePath("nested/crashes");
    QVERIFY(!QDir(target).exists());

    QVERIFY(heiran::app::installCrashReporter(target));
    QVERIFY2(QDir(target).exists(), "the dump directory must exist before a crash happens");
    QCOMPARE(QDir(heiran::app::crashDumpDirectory()), QDir(target));
  }

  // 指定しなければ利用者ごとの場所を使うこと。
  void fallsBackToTheUserDataLocation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(heiran::app::installCrashReporter(directory.path()));

    // 空を渡すと既定へ戻る。
    QVERIFY(heiran::app::installCrashReporter());
    const QString fallback = heiran::app::crashDumpDirectory();
    QVERIFY(!fallback.isEmpty());
    QVERIFY2(fallback.endsWith("crashes"), qPrintable(fallback));
    QVERIFY(QDir(fallback).exists());
  }

  // 作れない場所を渡された場合は偽を返すこと。起動そのものは続ける。
  void reportsFailureForAnUnusableDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // ファイルを作り、その下へ掘ろうとする。
    const QString filePath = QDir(directory.path()).filePath("not-a-directory");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    QVERIFY(!heiran::app::installCrashReporter(filePath + "/crashes"));
  }

  // 落ちていなければダンプは無いこと。
  void hasNoDumpBeforeACrash() { QCOMPARE(heiran::app::lastCrashDumpPath(), QString()); }

  // 実際にアクセス違反で落としてダンプが出ること。
  //
  // ここが本題。書き出し先を作れることだけを確かめても、落ちたときに本当に
  // 書けるかは分からない。別のプロセスを立てて落とし、ファイルを見る。
  void writesADumpWhenTheProcessCrashes() {
#if !defined(Q_OS_WIN)
    QSKIP("minidumps are only written on Windows");
#endif
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString dumpDirectory = QDir(directory.path()).filePath("crashes");

    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {kCrashArgument, dumpDirectory});
    QVERIFY2(child.waitForStarted(5000), "the child process did not start");
    QVERIFY2(child.waitForFinished(20000), "the child process did not finish");

    // 落ちた側の終了コードは環境依存のため見ない。残ったものだけを見る。
    const QStringList dumps = QDir(dumpDirectory).entryList({"*.dmp"}, QDir::Files, QDir::Name);
    QVERIFY2(!dumps.isEmpty(), "no minidump was written for a crashing process");

    // 中身があること。0 バイトのファイルでは調べられない。
    const QFileInfo dumpInfo(QDir(dumpDirectory).filePath(dumps.first()));
    QVERIFY2(dumpInfo.size() > 4096,
             qPrintable(QString("%1 is only %2 bytes").arg(dumps.first()).arg(dumpInfo.size())));

    // ミニダンプの印で始まること。形式として読める見込みがある。
    QFile dumpFile(dumpInfo.absoluteFilePath());
    QVERIFY(dumpFile.open(QIODevice::ReadOnly));
    QCOMPARE(dumpFile.read(4), QByteArray("MDMP"));
  }
};

int main(int argc, char *argv[]) {
  // 子プロセスとして呼ばれた場合は、仕掛けてから落ちる。
  if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String(kCrashArgument)) {
    QCoreApplication app(argc, argv);
    heiran::app::installCrashReporter(QString::fromLocal8Bit(argv[2]));

    // アクセス違反を起こす。追っている事象と同じ 0xC0000005 になる。
    volatile int *nullPointer = nullptr;
    *nullPointer = 1;
    return 0;
  }

  QCoreApplication app(argc, argv);
  CrashReporterTests testObject;
  return QTest::qExec(&testObject, argc, argv);
}

#include "CrashReporterTests.moc"
