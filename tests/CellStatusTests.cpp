#include "heiran/ui/CellStatus.hpp"

#include <QtTest>

using heiran::ui::CellStatus;
using heiran::ui::CellStatusInfo;

class CellStatusTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void distinguishesEmptyFromFailed() {
    // 空欄セルと読込失敗セルは見出しを分ける。見た目を分ける前提になる。
    QCOMPARE(heiran::ui::cellStatusHeadline(CellStatusInfo::empty("No image set is assigned.")),
             QString("No image"));
    QCOMPARE(heiran::ui::cellStatusHeadline(CellStatusInfo::failed("a.png", "Unsupported format.")),
             QString("Load failed"));
  }

  void reportsLoadingSeparately() {
    QCOMPARE(heiran::ui::cellStatusHeadline(CellStatusInfo::loading()), QString("Loading..."));
  }

  void hasNoHeadlineWhenReady() {
    QCOMPARE(heiran::ui::cellStatusHeadline(CellStatusInfo::ready()), QString());
  }

  void failedKeepsFileNameAndReason() {
    const auto statusInfo = CellStatusInfo::failed("a.png", "Unsupported format.");
    QCOMPARE(statusInfo.status, CellStatus::Failed);
    QCOMPARE(statusInfo.fileName, QString("a.png"));
    QCOMPARE(statusInfo.reason, QString("Unsupported format."));
  }

  void emptyCarriesReasonWithoutFileName() {
    const auto statusInfo = CellStatusInfo::empty("This image set has no image on this page.");
    QCOMPARE(statusInfo.status, CellStatus::Empty);
    QVERIFY(statusInfo.fileName.isEmpty());
    QCOMPARE(statusInfo.reason, QString("This image set has no image on this page."));
  }

  void defaultsToEmpty() { QCOMPARE(CellStatusInfo{}.status, CellStatus::Empty); }
};

QTEST_MAIN(CellStatusTests)

#include "CellStatusTests.moc"
