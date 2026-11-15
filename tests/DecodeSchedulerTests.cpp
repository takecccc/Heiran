#include "heiran/ui/DecodeScheduler.hpp"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class DecodeSchedulerTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void buffersAndDeduplicatesResultsUntilActivated() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("image.png");
    QImage image(16, 12, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    heiran::ui::DecodeScheduler scheduler(nullptr, true);
    QSignalSpy buffered(&scheduler, &heiran::ui::DecodeScheduler::completionBuffered);
    QSignalSpy delivered(&scheduler, &heiran::ui::DecodeScheduler::decodeCompleted);
    scheduler.request(path, true, false);
    QTRY_COMPARE(buffered.count(), 1);
    scheduler.request(path, true, false);
    const auto completed = scheduler.activate();
    QCOMPARE(completed.size(), 1);
    QVERIFY(completed.first().result.ok);
    QCOMPARE(completed.first().result.decodedImage.displayImage.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(delivered.count(), 0);
    QVERIFY(scheduler.activate().isEmpty());
  }

  void retainsCompletionsUntilTheUiConsumesThem() {
    QTemporaryDir directory;
    heiran::ui::DecodeScheduler scheduler;
    QSignalSpy delivered(&scheduler, &heiran::ui::DecodeScheduler::decodeCompleted);
    const QString path = directory.filePath("missing.png");
    scheduler.request(path, true, false);
    QTRY_COMPARE(delivered.count(), 1);
    const auto notification =
        qvariant_cast<heiran::ui::DecodeCompletion>(delivered.first().first());
    scheduler.request(path, true, false);
    const auto completed = scheduler.takeCompleted(notification.cacheKey);
    QVERIFY(completed.has_value());
    QVERIFY(!completed->result.ok);
    QVERIFY(!scheduler.takeCompleted(notification.cacheKey));
    scheduler.request(path, true, false);
    QTRY_COMPARE(delivered.count(), 2);
  }

  void resetDiscardsUnconsumedResults() {
    QTemporaryDir directory;
    heiran::ui::DecodeScheduler scheduler(nullptr, true);
    QSignalSpy buffered(&scheduler, &heiran::ui::DecodeScheduler::completionBuffered);
    scheduler.request(directory.filePath("missing.png"), false, false);
    QTRY_COMPARE(buffered.count(), 1);
    scheduler.reset();
    QVERIFY(scheduler.activate().isEmpty());
  }

  void keepsDifferentExifPoliciesSeparate() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("missing.png");
    heiran::ui::DecodeScheduler scheduler(nullptr, true);
    QSignalSpy buffered(&scheduler, &heiran::ui::DecodeScheduler::completionBuffered);
    scheduler.request(path, true, false);
    scheduler.request(path, false, false);
    QTRY_COMPARE(buffered.count(), 2);
    const auto completed = scheduler.activate();
    QCOMPARE(completed.size(), 2);
    QVERIFY(completed.first().cacheKey != completed.last().cacheKey);
    QVERIFY(completed.first().honorExifOrientation != completed.last().honorExifOrientation);
  }
};

QTEST_GUILESS_MAIN(DecodeSchedulerTests)
#include "DecodeSchedulerTests.moc"
