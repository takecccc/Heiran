#include "heiran/ui/ManualLocator.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void touch(const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("<html></html>");
}

}  // namespace

class ManualLocatorTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // インストールした形 (bin/ の隣の doc/manual) を見つける。
  void findsTheInstalledManual() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bin = QDir(root.path()).filePath("bin");
    QDir().mkpath(bin);
    touch(QDir(root.path()).filePath("doc/manual/index.html"));
    const QString found = heiran::ui::findManualIndex(bin);
    QVERIFY(!found.isEmpty());
    QVERIFY(found.endsWith("doc/manual/index.html"));
  }

  // ビルドしたフォルダ (実行ファイルの隣の manual/) も見つける。
  void findsTheBuiltManualBesideTheExecutable() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    touch(QDir(root.path()).filePath("manual/index.html"));
    const QString found = heiran::ui::findManualIndex(root.path());
    QVERIFY(found.endsWith("manual/index.html"));
  }

  // 複数あるときは、インストールした形を優先する。
  void prefersTheInstalledLayout() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bin = QDir(root.path()).filePath("bin");
    touch(QDir(root.path()).filePath("doc/manual/index.html"));
    touch(QDir(bin).filePath("manual/index.html"));
    const QString found = heiran::ui::findManualIndex(bin);
    QVERIFY(found.endsWith("doc/manual/index.html"));
    QVERIFY(!found.contains("bin"));
  }

  // 無ければ空。存在しないフォルダでも落ちない。
  void returnsNothingWhenThereIsNoManual() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(heiran::ui::findManualIndex(root.path()).isEmpty());
    QVERIFY(heiran::ui::findManualIndex(QDir(root.path()).filePath("missing")).isEmpty());
  }
};

QTEST_MAIN(ManualLocatorTests)

#include "ManualLocatorTests.moc"
