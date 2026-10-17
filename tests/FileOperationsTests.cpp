#include "heiran/ui/FileOperations.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using heiran::ui::FileOperations;

namespace {

QString writeFile(const QDir &directory, const QString &name, const QByteArray &content = "x") {
  const QString path = directory.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write(content);
  return path;
}

}  // namespace

class FileOperationsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void acceptsOrdinaryNames() {
    QVERIFY(FileOperations::validateFileName("photo.png").isEmpty());
    QVERIFY(FileOperations::validateFileName("日本語 の 名前.jpg").isEmpty());
    QVERIFY(FileOperations::validateFileName("a.b.c").isEmpty());
  }

  // 使えない名前を弾くこと。通すと、名前の変更が失敗するか、別の場所へ動いてしまう。
  void rejectsNamesThatCannotBeUsed() {
    QVERIFY(!FileOperations::validateFileName("").isEmpty());
    QVERIFY(!FileOperations::validateFileName("   ").isEmpty());
    QVERIFY(!FileOperations::validateFileName("sub/name.png").isEmpty());
    QVERIFY(!FileOperations::validateFileName("sub\\name.png").isEmpty());
    QVERIFY(!FileOperations::validateFileName("a:b.png").isEmpty());
    QVERIFY(!FileOperations::validateFileName("what?.png").isEmpty());
    QVERIFY(!FileOperations::validateFileName("name.").isEmpty());
    QVERIFY(!FileOperations::validateFileName("..").isEmpty());
    QVERIFY(!FileOperations::validateFileName("CON.png").isEmpty());
    QVERIFY(!FileOperations::validateFileName("nul").isEmpty());
  }

  void renamesWithinTheSameFolder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir dir(directory.path());
    const QString path = writeFile(dir, "old.png", "content");

    const auto result = FileOperations::rename(path, "new.png");

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QVERIFY(!QFile::exists(path));
    QVERIFY(QFile::exists(dir.filePath("new.png")));
    QCOMPARE(QFileInfo(result.newPath), QFileInfo(dir.filePath("new.png")));
    QFile renamed(dir.filePath("new.png"));
    QVERIFY(renamed.open(QIODevice::ReadOnly));
    QCOMPARE(renamed.readAll(), QByteArray("content"));
  }

  // 既にある名前へは上書きしないこと。写真を失う操作になる。
  void refusesToOverwriteAnExistingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir dir(directory.path());
    const QString first = writeFile(dir, "a.png", "first");
    writeFile(dir, "b.png", "second");

    const auto result = FileOperations::rename(first, "b.png");

    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(QFile::exists(first));
    QFile other(dir.filePath("b.png"));
    QVERIFY(other.open(QIODevice::ReadOnly));
    QCOMPARE(other.readAll(), QByteArray("second"));
  }

  // 同じ名前への変更は何もせず成功にすること。ダイアログを何も変えずに閉じた場合。
  void treatsTheSameNameAsSuccess() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(QDir(directory.path()), "a.png");

    const auto result = FileOperations::rename(path, "a.png");

    QVERIFY(result.ok);
    QVERIFY(QFile::exists(path));
  }

  // 大文字小文字だけの変更が通ること。Windows では同じファイルを指すため、
  // そのまま変えようとすると「既にある」として失敗する。
  void changesOnlyTheCaseOfAName() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir dir(directory.path());
    const QString path = writeFile(dir, "photo.png", "content");

    const auto result = FileOperations::rename(path, "PHOTO.png");

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const QStringList names = dir.entryList(QDir::Files);
    QCOMPARE(names, QStringList({"PHOTO.png"}));
    // 一時の名前を残さない。
    QVERIFY(!QFile::exists(dir.filePath("photo.png.heiran-rename")));
  }

  void failsForAMissingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto result =
        FileOperations::rename(QDir(directory.path()).filePath("missing.png"), "x.png");

    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
  }

  void rejectsAnInvalidNewName() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(QDir(directory.path()), "a.png");

    const auto result = FileOperations::rename(path, "sub/a.png");

    QVERIFY(!result.ok);
    QVERIFY(QFile::exists(path));
  }

  // ごみ箱への移動は差し替えられること。テストが実際のごみ箱を汚さない。
  void movesToTheTrashThroughTheGivenFunction() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(QDir(directory.path()), "a.png");
    QString trashed;

    const auto result = FileOperations::moveToTrash(path, [&trashed](const QString &target) {
      trashed = target;
      return true;
    });

    QVERIFY(result.ok);
    QCOMPARE(QFileInfo(trashed), QFileInfo(path));
  }

  void reportsAFailureToTrash() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(QDir(directory.path()), "a.png");

    const auto result = FileOperations::moveToTrash(path, [](const QString &) { return false; });

    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
  }

  // 無いファイルは、ごみ箱の処理を呼ばずに失敗にすること。
  void doesNotTrashAMissingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    bool called = false;

    const auto result = FileOperations::moveToTrash(QDir(directory.path()).filePath("missing.png"),
                                                    [&called](const QString &) {
                                                      called = true;
                                                      return true;
                                                    });

    QVERIFY(!result.ok);
    QVERIFY(!called);
  }

  void formatsThePathForTheClipboard() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(QDir(directory.path()), "a.png");

    const QString text = FileOperations::pathForClipboard(path);

    QCOMPARE(text, QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    QVERIFY(QDir::isAbsolutePath(text));
  }
};

QTEST_APPLESS_MAIN(FileOperationsTests)

#include "FileOperationsTests.moc"
