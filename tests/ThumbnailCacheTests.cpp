#include "heiran/ui/ThumbnailCache.hpp"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <limits>

using heiran::ui::ThumbnailCache;

namespace {

QString writeImage(const QDir &directory, const QString &fileName, QSize size, int seed = 0) {
  QImage image(size, QImage::Format_RGB32);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y,
                          QColor((x * 255) / std::max(1, size.width() - 1) % 256,
                                 (y * 255) / std::max(1, size.height() - 1) % 256,
                                 (96 + seed) % 256));
    }
  }
  const QString filePath = directory.filePath(fileName);
  return image.save(filePath) ? filePath : QString();
}

int entryCount(const QString &directory) {
  return static_cast<int>(
      QDir(directory).entryList({"*.png"}, QDir::Files | QDir::NoDotAndDotDot).size());
}

}  // namespace

class ThumbnailCacheTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void init() {
    QVERIFY(sources_.isValid());
    QVERIFY(cacheDirectory_.isValid());
    // 各テストを独立にする。前のテストの項目が残ると、命中の判定が意味を失う。
    QDir(cacheDirectory_.path()).removeRecursively();
    QDir().mkpath(cacheDirectory_.path());
  }

  void createsAndKeepsAThumbnailOnAMiss() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200));
    ThumbnailCache cache(cacheDirectory_.path());
    QVERIFY(!cache.contains(path, QSize(160, 120)));

    const QImage created = cache.loadOrCreate(path, QSize(160, 120));

    QVERIFY(!created.isNull());
    QVERIFY(created.width() <= 160 && created.height() <= 120);
    QVERIFY(cache.contains(path, QSize(160, 120)));
    QCOMPARE(entryCount(cacheDirectory_.path()), 1);
  }

  // 2 回目は取っておいたものを返すこと。同じ画像が返り、項目も増えない。
  void returnsTheKeptThumbnailOnAHit() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200));
    ThumbnailCache cache(cacheDirectory_.path());
    const QImage first = cache.loadOrCreate(path, QSize(160, 120));

    const QImage second = cache.loadOrCreate(path, QSize(160, 120));

    QCOMPARE(second.size(), first.size());
    QCOMPARE(second.pixelColor(second.width() / 2, second.height() / 2),
             first.pixelColor(first.width() / 2, first.height() / 2));
    QCOMPARE(entryCount(cacheDirectory_.path()), 1);
  }

  // 取っておいたものを実際に使っていること。元を同じ大きさの壊れた中身へ差し替え、
  // 更新時刻を戻すと、鍵は変わらない。このとき画像が返るなら、元を読み直さずに
  // 項目を使っている (読み直せば壊れた元で失敗して空になる)。
  void servesTheKeptEntryWithoutReadingTheSource() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200), 0);
    ThumbnailCache cache(cacheDirectory_.path());
    const QImage before = cache.loadOrCreate(path, QSize(160, 120));
    QVERIFY(!before.isNull());

    const QDateTime originalTime = QFileInfo(path).lastModified();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    const qint64 size = file.size();
    QVERIFY(file.seek(0));
    QCOMPARE(file.write(QByteArray(static_cast<qsizetype>(size), '\0')), size);
    QVERIFY(file.setFileTime(originalTime, QFileDevice::FileModificationTime));
    file.close();
    QCOMPARE(QFileInfo(path).size(), size);
    QVERIFY(QImage(path).isNull());

    const QImage after = cache.loadOrCreate(path, QSize(160, 120));

    QVERIFY(!after.isNull());
    QCOMPARE(after.pixelColor(after.width() / 2, after.height() / 2),
             before.pixelColor(before.width() / 2, before.height() / 2));
  }

  // ファイルが変われば使わないこと。古い中身を出し続けない。
  void doesNotServeAThumbnailOfAChangedFile() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200), 0);
    ThumbnailCache cache(cacheDirectory_.path());
    cache.loadOrCreate(path, QSize(160, 120));
    QVERIFY(cache.contains(path, QSize(160, 120)));

    QImage replacement(QSize(300, 300), QImage::Format_RGB32);
    replacement.fill(Qt::white);
    QVERIFY(replacement.save(path));

    QVERIFY(!cache.contains(path, QSize(160, 120)));
    const QImage created = cache.loadOrCreate(path, QSize(160, 120));
    QVERIFY(!created.isNull());
    // 差し替えた 300x300 の縮小。古い 2:1 のものではない。
    QVERIFY(std::abs(static_cast<double>(created.width()) / created.height() - 1.0) < 0.05);
  }

  // 要求した大きさと EXIF の扱いは鍵に入ること。別の要求へ古い大きさを返さない。
  void keepsSeparateEntriesForSizeAndOrientation() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200));
    ThumbnailCache cache(cacheDirectory_.path());

    cache.loadOrCreate(path, QSize(160, 120), true);

    QVERIFY(cache.contains(path, QSize(160, 120), true));
    QVERIFY(!cache.contains(path, QSize(80, 60), true));
    QVERIFY(!cache.contains(path, QSize(160, 120), false));
  }

  // 読めない元は取っておかないこと。失敗を取っておくと、直したあとも空が返る。
  void doesNotKeepFailures() {
    ThumbnailCache cache(cacheDirectory_.path());
    QVERIFY(cache.loadOrCreate(QDir(sources_.path()).filePath("missing.png"), QSize(160, 120))
                .isNull());

    const QString broken = QDir(sources_.path()).filePath("broken.png");
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    QVERIFY(cache.loadOrCreate(broken, QSize(160, 120)).isNull());

    QCOMPARE(entryCount(cacheDirectory_.path()), 0);
  }

  // 壊れた項目は捨てて作り直すこと。書きかけで落ちた場合などに残り得る。
  void rebuildsACorruptEntry() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200));
    ThumbnailCache cache(cacheDirectory_.path());
    cache.loadOrCreate(path, QSize(160, 120));
    const QStringList entries = QDir(cacheDirectory_.path()).entryList({"*.png"}, QDir::Files);
    QCOMPARE(entries.size(), 1);
    QFile entry(QDir(cacheDirectory_.path()).filePath(entries.first()));
    QVERIFY(entry.open(QIODevice::WriteOnly | QIODevice::Truncate));
    entry.write("garbage");
    entry.close();

    const QImage rebuilt = cache.loadOrCreate(path, QSize(160, 120));

    QVERIFY(!rebuilt.isNull());
    QCOMPARE(entryCount(cacheDirectory_.path()), 1);
    QVERIFY(!QImage(QDir(cacheDirectory_.path()).filePath(entries.first())).isNull());
  }

  // 上限を超えたら、最後に使われたのが古いものから消えること。
  void prunesTheLeastRecentlyUsedFirst() {
    const QSize size(160, 120);
    const QStringList sources = {
        writeImage(QDir(sources_.path()), "1.png", QSize(400, 200), 1),
        writeImage(QDir(sources_.path()), "2.png", QSize(400, 200), 2),
        writeImage(QDir(sources_.path()), "3.png", QSize(400, 200), 3),
    };
    ThumbnailCache filler(cacheDirectory_.path());
    qint64 totalBytes = 0;
    for (const QString &source : sources) {
      filler.loadOrCreate(source, size);
      totalBytes += QFileInfo(filler.entryPath(source, size)).size();
    }
    // 古い順に 1 -> 2 -> 3。
    const QDateTime base = QDateTime::currentDateTime().addSecs(-1000);
    for (int index = 0; index < sources.size(); ++index) {
      QFile entry(filler.entryPath(sources.at(index), size));
      QVERIFY(entry.open(QIODevice::ReadWrite));
      QVERIFY(entry.setFileTime(base.addSecs(index * 100), QFileDevice::FileModificationTime));
    }

    // 合計が 9 割の上限を超える。8 割まで減らすには 1 枚消せば足りる。
    ThumbnailCache limited(cacheDirectory_.path(), totalBytes * 9 / 10);
    limited.prune();

    QVERIFY(!limited.contains(sources.at(0), size));
    QVERIFY(limited.contains(sources.at(1), size));
    QVERIFY(limited.contains(sources.at(2), size));
  }

  // 読んだものは新しい扱いになること。使われているものを先に消さない。
  void keepsAThumbnailThatWasJustRead() {
    const QSize size(160, 120);
    const QStringList sources = {
        writeImage(QDir(sources_.path()), "1.png", QSize(400, 200), 1),
        writeImage(QDir(sources_.path()), "2.png", QSize(400, 200), 2),
        writeImage(QDir(sources_.path()), "3.png", QSize(400, 200), 3),
    };
    ThumbnailCache filler(cacheDirectory_.path());
    qint64 totalBytes = 0;
    for (const QString &source : sources) {
      filler.loadOrCreate(source, size);
      totalBytes += QFileInfo(filler.entryPath(source, size)).size();
    }
    const QDateTime base = QDateTime::currentDateTime().addSecs(-1000);
    for (int index = 0; index < sources.size(); ++index) {
      QFile entry(filler.entryPath(sources.at(index), size));
      QVERIFY(entry.open(QIODevice::ReadWrite));
      QVERIFY(entry.setFileTime(base.addSecs(index * 100), QFileDevice::FileModificationTime));
    }

    // 最も古い 1 を読む。これで 2 が最も古くなる。
    QVERIFY(!filler.lookup(sources.at(0), size).isNull());
    ThumbnailCache limited(cacheDirectory_.path(), totalBytes * 9 / 10);
    limited.prune();

    QVERIFY(limited.contains(sources.at(0), size));
    QVERIFY(!limited.contains(sources.at(1), size));
    QVERIFY(limited.contains(sources.at(2), size));
  }

  // 上限に収まっていれば何も消さないこと。
  void prunesNothingWithinTheLimit() {
    const QString path = writeImage(QDir(sources_.path()), "a.png", QSize(400, 200));
    ThumbnailCache cache(cacheDirectory_.path());
    cache.loadOrCreate(path, QSize(160, 120));

    cache.prune();

    QVERIFY(cache.contains(path, QSize(160, 120)));
  }

  // 取っておいたものの方が速いこと。これが効かなければ取っておく意味が無い。
  //
  // 4K の PNG は読んで縮めるのに数十 ms かかる。取っておいたものは 160x120 の小さな
  // PNG を読むだけで済む。差は大きいため、環境のばらつきに負けない 2 倍で見る。
  void readsFasterFromTheCacheThanFromTheSource() {
    QImage image(3840, 2160, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
      auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        row[x] = qRgb(x % 256, y % 256, (x * y) % 256);
      }
    }
    const QString path = QDir(sources_.path()).filePath("large.png");
    QVERIFY(image.save(path));

    ThumbnailCache cache(cacheDirectory_.path());
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!cache.loadOrCreate(path, QSize(160, 120)).isNull());
    const qint64 coldNs = timer.nsecsElapsed();

    // 1 回目のばらつきを避けて、何度か測った最小を取る。
    qint64 warmNs = std::numeric_limits<qint64>::max();
    for (int attempt = 0; attempt < 5; ++attempt) {
      timer.restart();
      QVERIFY(!cache.loadOrCreate(path, QSize(160, 120)).isNull());
      warmNs = std::min(warmNs, timer.nsecsElapsed());
    }

    qInfo().noquote() << QString("cold %1 ms, warm %2 ms")
                             .arg(coldNs / 1'000'000.0, 0, 'f', 2)
                             .arg(warmNs / 1'000'000.0, 0, 'f', 2);
    QVERIFY2(warmNs * 2 < coldNs,
             qPrintable(QString("cold %1 ms, warm %2 ms").arg(coldNs / 1e6).arg(warmNs / 1e6)));
  }

  void sharesOneStandardCache() {
    QCOMPARE(&ThumbnailCache::standard(), &ThumbnailCache::standard());
  }

private:
  QTemporaryDir sources_;
  QTemporaryDir cacheDirectory_;
};

QTEST_MAIN(ThumbnailCacheTests)

#include "ThumbnailCacheTests.moc"
