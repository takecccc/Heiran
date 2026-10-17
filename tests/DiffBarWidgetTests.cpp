#include "heiran/ui/DiffBarWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using heiran::domain::CellDiff;
using heiran::domain::DiffMetrics;
using heiran::ui::DiffBarWidget;

namespace {

CellDiff sampleDiff() {
  CellDiff diff;
  diff.baseCell = 0;
  diff.targetCell = -1;
  return diff;
}

DiffMetrics longMetrics() {
  DiffMetrics metrics;
  metrics.pixelCount = 1000;
  metrics.maxAbsDiff = 40.0;
  metrics.meanAbsDiff = 12.5;
  metrics.rmse = 15.25;
  metrics.psnr = 24.3;
  metrics.exceededPixelCount = 500;
  return metrics;
}

QFrame *settingsPopup(DiffBarWidget &bar) { return bar.findChild<QFrame *>("diffSettingsPopup"); }

}  // namespace

class DiffBarWidgetTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 狭いセルでも、帯の求め方の選択肢が、全部読めること (欠けない)。
  void showsTheWholeModeNameInANarrowCell() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2", "Cell 3"}, 2, true);
    bar.resize(260, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    auto *mode = bar.findChildren<QComboBox *>().first();
    QCOMPARE(mode->currentText(), QString("absolute"));
    // 項目の名前が、コンボボックスの幅に収まっている。
    int widest = 0;
    for (int row = 0; row < mode->count(); ++row) {
      widest = std::max(widest, mode->fontMetrics().horizontalAdvance(mode->itemText(row)));
    }
    QVERIFY2(mode->width() >= widest, qPrintable(QString("%1 < %2").arg(mode->width()).arg(widest)));
    // 帯の幅の希望は、セルの幅を押し広げない。
    QCOMPARE(bar.sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  }

  // 指標が帯の幅より長いときは、省略記号で切る。全文は、ツールチップと resultText() で読める。
  void keepsTheWholeResultWhenItIsElided() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2"}, 1, true);
    bar.setResult(longMetrics(), QString());
    bar.resize(200, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    const QString text = bar.resultText();
    QVERIFY(text.contains("PSNR"));
    auto *label = bar.findChild<QLabel *>("diffBarResult");
    QVERIFY(label != nullptr);
    QCOMPARE(label->toolTip(), text);
    QVERIFY(label->width() <= bar.width());
  }

  // 失敗の理由も、同じ場所へ出る。
  void showsTheReasonWhenTheDifferenceCannotBeMade() {
    DiffBarWidget bar;
    bar.setResult(std::nullopt, "Base or target image is not available.");
    QCOMPARE(bar.resultText(), QString("Base or target image is not available."));
  }

  // 歯車のボタンで、設定のポップアップが開く。開いている間は、入力の途中として扱う (帯を隠さない)。
  void opensTheSettingsInAPopup() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2", "Cell 3"}, 2, false);
    bar.resize(260, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QFrame *popup = settingsPopup(bar);
    QVERIFY(popup != nullptr);
    QVERIFY(!popup->isVisible());
    // 単独の窓として出すと、先頭の部品へフォーカスが入る。実際は、セルの上へ重ねて出る。
    if (QWidget *focus = QApplication::focusWidget()) {
      focus->clearFocus();
    }
    QVERIFY(!bar.isInteracting());

    auto *gear = bar.findChildren<QToolButton *>().first();
    gear->click();
    QVERIFY(popup->isVisible());
    QVERIFY(bar.isInteracting());
    // 比べる側は、自身の画像を持たないセルだけが選べる。
    const auto combos = popup->findChildren<QComboBox *>();
    QVERIFY(combos.size() >= 4);
    popup->hide();
    QVERIFY(!bar.isInteracting());
  }

  // ポップアップで変えた設定が、diffChanged で返る。
  void reportsChangesMadeInThePopup() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2"}, 1, true);
    QSignalSpy spy(&bar, &DiffBarWidget::diffChanged);

    QFrame *popup = settingsPopup(bar);
    auto *gain = popup->findChild<QDoubleSpinBox *>();
    QVERIFY(gain != nullptr);
    gain->setValue(3.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(bar.currentDiff().settings.gain, 3.0);

    auto *autoRange = popup->findChild<QCheckBox *>();
    autoRange->setChecked(false);
    QVERIFY(!bar.currentDiff().settings.autoRange);

    // 大きさの合わせ方 (名前つきの選択肢)。
    const auto combos = popup->findChildren<QComboBox *>();
    QComboBox *size = nullptr;
    for (auto *combo : combos) {
      if (combo->itemText(0) == "Overlap only") {
        size = combo;
      }
    }
    QVERIFY(size != nullptr);
    size->setCurrentIndex(3);
    QCOMPARE(bar.currentDiff().settings.sizeMatch, heiran::domain::DiffSizeMatch::ToLarger);
  }

  // ワイプへ替えると、切り方と位置の設定が出て、見せ方などは隠れる。値は、設定へ反映される。
  void wipeShowsItsOwnSettings() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2"}, 1, true);
    auto *mode = bar.findChildren<QComboBox *>().first();
    mode->setCurrentText("wipe");
    QCOMPARE(bar.currentDiff().settings.mode, heiran::domain::DiffMode::Wipe);

    QFrame *popup = settingsPopup(bar);
    QComboBox *direction = nullptr;
    for (auto *combo : popup->findChildren<QComboBox *>()) {
      if (combo->itemText(0) == "Left | Right") {
        direction = combo;
      }
    }
    QVERIFY(direction != nullptr);
    QSignalSpy spy(&bar, &DiffBarWidget::diffChanged);
    direction->setCurrentIndex(1);
    QCOMPARE(bar.currentDiff().settings.wipeOrientation, heiran::domain::WipeOrientation::TopBottom);
    // 位置は、% で入力し、割合で保つ。
    const auto spins = popup->findChildren<QDoubleSpinBox *>();
    QDoubleSpinBox *position = nullptr;
    for (auto *spin : spins) {
      if (spin->suffix() == " %") {
        position = spin;
      }
    }
    QVERIFY(position != nullptr);
    position->setValue(25.0);
    QCOMPARE(bar.currentDiff().settings.wipePosition, 0.25);
    QVERIFY(spy.count() >= 2);

    // 帯の指標の欄には、ワイプの案内が出る。
    DiffMetrics metrics;
    metrics.wipe = true;
    bar.setResult(metrics, QString());
    QVERIFY(bar.resultText().contains("Drag"));
  }

  // SSIM へ替えると、既定の閾値が、類似度の尺度 (0.9) になる。モードを行き来しても、尺度が合う。
  void switchingToSsimSuitsTheThreshold() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2"}, 1, true);
    auto *mode = bar.findChildren<QComboBox *>().first();
    mode->setCurrentText("ssim");
    QCOMPARE(bar.currentDiff().settings.mode, heiran::domain::DiffMode::Ssim);
    QCOMPARE(bar.currentDiff().settings.threshold, 0.9);
    mode->setCurrentText("delta-e");
    QCOMPARE(bar.currentDiff().settings.threshold, 2.3);
    mode->setCurrentText("absolute");
    QCOMPARE(bar.currentDiff().settings.threshold, 8.0);
    // 利用者が値を替えていたら、モードを替えても、触らない。
    mode->setCurrentText("ssim");
    auto *popup = bar.findChild<QFrame *>("diffSettingsPopup");
    for (auto *spin : popup->findChildren<QDoubleSpinBox *>()) {
      if (spin->prefix().isEmpty() && spin->suffix().isEmpty() && spin->maximum() == 255.0) {
        QVERIFY2(spin->decimals() == 3, qPrintable(QString::number(spin->decimals())));
        spin->setValue(0.75);
      }
    }
    QCOMPARE(bar.currentDiff().settings.threshold, 0.75);
    mode->setCurrentText("absolute");
    QCOMPARE(bar.currentDiff().settings.threshold, 0.75);

    // 指標の欄には、SSIM の平均と最小が出る。
    DiffMetrics metrics;
    metrics.ssim = true;
    metrics.ssimMean = 0.9731;
    metrics.ssimMin = 0.412;
    metrics.pixelCount = 100;
    metrics.exceededPixelCount = 12;
    bar.setResult(metrics, QString());
    QVERIFY(bar.resultText().contains("0.9731"));
    QVERIFY(bar.resultText().contains("0.412"));
    QVERIFY(bar.resultText().contains("12.0%"));
  }

  // 帯の求め方を delta-e へ替えると、既定の閾値が ΔE の尺度になる。
  void switchingToDeltaESuitsTheThreshold() {
    DiffBarWidget bar;
    bar.setDiff(sampleDiff(), {"Cell 1", "Cell 2"}, 1, true);
    auto *mode = bar.findChildren<QComboBox *>().first();
    mode->setCurrentText("delta-e");
    QCOMPARE(bar.currentDiff().settings.mode, heiran::domain::DiffMode::DeltaE);
    QCOMPARE(bar.currentDiff().settings.threshold, 2.3);
    mode->setCurrentText("absolute");
    QCOMPARE(bar.currentDiff().settings.threshold, 8.0);
  }
};

QTEST_MAIN(DiffBarWidgetTests)
#include "DiffBarWidgetTests.moc"
