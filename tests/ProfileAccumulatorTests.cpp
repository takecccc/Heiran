#include "heiran/ui/ProfileAccumulator.hpp"

#include <QtTest>

namespace {

QStringList g_capturedMessages;

void captureMessage(QtMsgType, const QMessageLogContext &, const QString &message) {
  g_capturedMessages.push_back(message);
}

// qDebug() の出力を横取りして中身を確かめる。
class MessageCapture {
public:
  MessageCapture() {
    g_capturedMessages.clear();
    previousHandler_ = qInstallMessageHandler(captureMessage);
  }
  ~MessageCapture() { qInstallMessageHandler(previousHandler_); }

  QStringList messages() const { return g_capturedMessages; }

private:
  QtMessageHandler previousHandler_ = nullptr;
};

}  // namespace

class ProfileAccumulatorTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() { qputenv("HEIRAN_TEST_PROFILE_ON", "1"); }

  void cleanupTestCase() { qunsetenv("HEIRAN_TEST_PROFILE_ON"); }

  // 環境変数が立っていなければ何も出さない。計測そのものを省くために使う。
  void staysDisabledWithoutTheEnvironmentVariable() {
    MessageCapture capture;
    heiran::ui::ProfileAccumulator profiler("HEIRAN_TEST_PROFILE_OFF", "off", {"total"}, 1);

    QVERIFY(!profiler.enabled());
    profiler.record({1'000'000});
    QVERIFY(capture.messages().isEmpty());
  }

  // 指定した回数に達したときだけ平均を出す。
  void reportsAveragesEveryNthSample() {
    MessageCapture capture;
    heiran::ui::ProfileAccumulator profiler("HEIRAN_TEST_PROFILE_ON", "sample", {"total", "inner"},
                                            3);
    QVERIFY(profiler.enabled());

    profiler.record({1'000'000, 500'000});
    profiler.record({2'000'000, 500'000});
    QVERIFY(capture.messages().isEmpty());

    profiler.record({3'000'000, 500'000});
    QCOMPARE(capture.messages().size(), 1);
    QCOMPARE(capture.messages().at(0),
             QString("[HeiranProfile] sample avg_total=2.000 ms min_total=1.000 ms "
                     "max_total=3.000 ms avg_inner=0.500 ms min_inner=0.500 ms "
                     "max_inner=0.500 ms samples=3"));
  }

  // たまに起きる遅れは平均に埋もれる。最大でしか見えない。
  void reportsTheWorstSampleAlongsideTheAverage() {
    MessageCapture capture;
    heiran::ui::ProfileAccumulator profiler("HEIRAN_TEST_PROFILE_ON", "spike", {"interval"}, 4);

    profiler.record({1'000'000});
    profiler.record({1'000'000});
    profiler.record({25'000'000});
    profiler.record({1'000'000});

    QCOMPARE(capture.messages().size(), 1);
    // 平均は 7 ms で、1 回だけ 25 ms 掛かったことは分からない。
    QVERIFY2(capture.messages().at(0).contains("avg_interval=7.000 ms"),
             qPrintable(capture.messages().at(0)));
    QVERIFY2(capture.messages().at(0).contains("max_interval=25.000 ms"),
             qPrintable(capture.messages().at(0)));
    // 最小があれば、平均が本当の周期なのか外れ値に引かれただけなのかが分かる。
    QVERIFY2(capture.messages().at(0).contains("min_interval=1.000 ms"),
             qPrintable(capture.messages().at(0)));
  }

  // 出力のたびに合計を戻す。前の区間の値が次へ持ち越されてはいけない。
  void resetsTotalsAfterReporting() {
    MessageCapture capture;
    heiran::ui::ProfileAccumulator profiler("HEIRAN_TEST_PROFILE_ON", "reset", {"total"}, 2);

    profiler.record({4'000'000});
    profiler.record({4'000'000});
    profiler.record({1'000'000});
    profiler.record({1'000'000});

    QCOMPARE(capture.messages().size(), 2);
    QVERIFY2(capture.messages().at(1).contains("avg_total=1.000 ms"),
             qPrintable(capture.messages().at(1)));
    // 最大も最小も持ち越さない。前の区間の 4 ms が残っていてはいけない。
    QVERIFY2(capture.messages().at(1).contains("max_total=1.000 ms"),
             qPrintable(capture.messages().at(1)));
    QVERIFY2(capture.messages().at(1).contains("min_total=1.000 ms"),
             qPrintable(capture.messages().at(1)));
  }

  // 個数が合わない呼び出しは無視する。合計が壊れるより落ちない方を選ぶ。
  void ignoresRecordsWithTheWrongFieldCount() {
    MessageCapture capture;
    heiran::ui::ProfileAccumulator profiler("HEIRAN_TEST_PROFILE_ON", "mismatch",
                                            {"total", "inner"}, 1);

    profiler.record({1'000'000});
    QVERIFY(capture.messages().isEmpty());

    profiler.record({1'000'000, 2'000'000});
    QCOMPARE(capture.messages().size(), 1);
  }
};

QTEST_MAIN(ProfileAccumulatorTests)

#include "ProfileAccumulatorTests.moc"
