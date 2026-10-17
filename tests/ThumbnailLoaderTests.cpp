#include "heiran/ui/ThumbnailLoader.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// 縦横比の分かる画像を書き出す。
QString writeImage(const QDir &directory, const QString &fileName, QSize size) {
  QImage image(size, QImage::Format_RGB32);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y,
                          QColor((x * 255) / std::max(1, size.width() - 1),
                                 (y * 255) / std::max(1, size.height() - 1), 96));
    }
  }
  const QString filePath = directory.filePath(fileName);
  return image.save(filePath) ? filePath : QString();
}

}  // namespace

class ThumbnailLoaderTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() {
    QVERIFY(directory_.isValid());
    const QDir dir(directory_.path());
    pngPath_ = writeImage(dir, "wide.png", QSize(800, 400));
    jpegPath_ = writeImage(dir, "wide.jpg", QSize(800, 400));
    smallPath_ = writeImage(dir, "small.png", QSize(40, 20));
    QVERIFY(!pngPath_.isEmpty());
    QVERIFY(!jpegPath_.isEmpty());
    QVERIFY(!smallPath_.isEmpty());
  }

  // 指定した大きさに収まること。縦横比は保つ。
  void fitsInsideTheRequestedSize() {
    for (const QString &path : {pngPath_, jpegPath_}) {
      const QImage thumbnail = heiran::ui::loadThumbnail(path, QSize(200, 200));
      QVERIFY2(!thumbnail.isNull(), qPrintable(path));
      QVERIFY2(thumbnail.width() <= 200 && thumbnail.height() <= 200,
               qPrintable(
                   QString("%1: %2x%3").arg(path).arg(thumbnail.width()).arg(thumbnail.height())));
      // 元は 2:1。縮めても保たれる。
      QVERIFY2(std::abs(static_cast<double>(thumbnail.width()) / thumbnail.height() - 2.0) < 0.05,
               qPrintable(
                   QString("%1: %2x%3").arg(path).arg(thumbnail.width()).arg(thumbnail.height())));
    }
  }

  // 元より小さい要求では縮めるが、元より大きい要求では引き伸ばさないこと。
  void doesNotUpscaleSmallImages() {
    const QImage thumbnail = heiran::ui::loadThumbnail(smallPath_, QSize(400, 400));
    QVERIFY(!thumbnail.isNull());
    QCOMPARE(thumbnail.size(), QSize(40, 20));
  }

  // 読めない経路では空を返すこと。落ちない。
  void returnsNullForMissingFiles() {
    QVERIFY(heiran::ui::loadThumbnail("does-not-exist.png", QSize(64, 64)).isNull());
    QVERIFY(heiran::ui::loadThumbnail(pngPath_, QSize()).isNull());
    QVERIFY(heiran::ui::loadThumbnail(QString(), QSize(64, 64)).isNull());
  }

  // 中身が保たれること。左上と右下で色が違う画像なので、縮めても差が残る。
  void keepsTheImageContent() {
    const QImage thumbnail = heiran::ui::loadThumbnail(pngPath_, QSize(100, 100));
    QVERIFY(!thumbnail.isNull());
    const QColor topLeft = thumbnail.pixelColor(1, 1);
    const QColor bottomRight = thumbnail.pixelColor(thumbnail.width() - 2, thumbnail.height() - 2);
    QVERIFY2(
        bottomRight.red() > topLeft.red() + 100,
        qPrintable(QString("topLeft=%1 bottomRight=%2").arg(topLeft.red()).arg(bottomRight.red())));
  }

  // JPEG は間引いて読むため、元解像度を読むより速いこと。
  // 数値は環境で変わるため、桁が逆転していないことだけを見る。
  void readsJpegThumbnailsFasterThanFullResolution() {
    QTemporaryDir bigDirectory;
    QVERIFY(bigDirectory.isValid());
    const QString bigJpeg = writeImage(QDir(bigDirectory.path()), "big.jpg", QSize(3000, 2000));
    QVERIFY(!bigJpeg.isEmpty());

    // 1 回目はファイルキャッシュの影響が大きいため計測から外す。
    QImageReader warmup(bigJpeg);
    QVERIFY(!warmup.read().isNull());

    QElapsedTimer timer;
    timer.start();
    QImageReader fullReader(bigJpeg);
    const QImage full = fullReader.read();
    const qint64 fullNs = timer.nsecsElapsed();
    QVERIFY(!full.isNull());

    timer.restart();
    const QImage thumbnail = heiran::ui::loadThumbnail(bigJpeg, QSize(256, 256));
    const qint64 thumbnailNs = timer.nsecsElapsed();
    QVERIFY(!thumbnail.isNull());

    qInfo() << "jpeg full" << fullNs / 1000000.0 << "ms thumbnail" << thumbnailNs / 1000000.0
            << "ms";
    QVERIFY2(thumbnailNs < fullNs,
             qPrintable(QString("full=%1ns thumbnail=%2ns").arg(fullNs).arg(thumbnailNs)));
  }

private:
  QTemporaryDir directory_;
  QString pngPath_;
  QString jpegPath_;
  QString smallPath_;
};

QTEST_MAIN(ThumbnailLoaderTests)

#include "ThumbnailLoaderTests.moc"
