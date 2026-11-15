#include "heiran/app/CrashReporter.hpp"
#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/ui/ThumbnailCache.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

class UserDataPathsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("Heiran");
    QCoreApplication::setApplicationName("Heiran");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
  }

  void storesAllUserDataUnderOneHeiranDirectory() {
    const QDir root(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                        .filePath("Heiran"));
    const QSettings settings;
    QCOMPARE(settings.format(), QSettings::IniFormat);
    QCOMPARE(QDir::cleanPath(settings.fileName()), root.filePath("Heiran.ini"));
    QCOMPARE(heiran::infra::FavoriteStore().filePath(), root.filePath("favorites.json"));
    QCOMPARE(heiran::ui::ThumbnailCache::standard().directory(), root.filePath("cache/thumbnails"));
    QCOMPARE(heiran::app::crashDumpDirectory(), root.filePath("crashes"));
  }
};

QTEST_GUILESS_MAIN(UserDataPathsTests)
#include "UserDataPathsTests.moc"
