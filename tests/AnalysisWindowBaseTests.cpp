#include "heiran/ui/AnalysisWindowBase.hpp"

#include <QImage>
#include <QtTest>

namespace {

// 呼ばれた回数を数えるだけの解析ウィンドウ。
class CountingAnalysisWindow : public heiran::ui::AnalysisWindowBase {
public:
  CountingAnalysisWindow() : AnalysisWindowBase("Counting") { resize(200, 150); }

  int refreshCount = 0;
  int lastPageNumber = 0;
  int lastImageCount = 0;

protected:
  void analysisImagesChanged() override {
    ++refreshCount;
    lastPageNumber = pageNumber();
    lastImageCount = static_cast<int>(analysisImages().size());
  }
};

QVector<heiran::ui::AnalysisImage> makeImages(int count) {
  QVector<heiran::ui::AnalysisImage> images;
  for (int index = 0; index < count; ++index) {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::gray);

    heiran::ui::AnalysisSampler sampler;
    sampler.decodedImage = {.displayImage = image};
    sampler.sourceSize = image.size();

    images.push_back({.label = QString("cell%1").arg(index),
                      .image = image,
                      .selectionRect = std::nullopt,
                      .sampler = sampler});
  }
  return images;
}

}  // namespace

class AnalysisWindowBaseTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 隠れている間は計算しないこと。解析は全画素を走査するため、見えていない窓の
  // ために走らせるとページ送りが重くなる。
  void doesNotComputeWhileHidden() {
    CountingAnalysisWindow window;
    QVERIFY(!window.isVisible());

    window.setAnalysisImages(3, makeImages(2));
    QCOMPARE(window.refreshCount, 0);

    // 表題はページ番号を含む。隠れていても更新しておく。
    QCOMPARE(window.windowTitle(), QString("Counting  Page 3"));
  }

  // 隠れている間に届いた分は、次に開いたときに 1 度だけ計算すること。
  void computesOncePendingDataWhenShown() {
    CountingAnalysisWindow window;
    window.setAnalysisImages(3, makeImages(2));
    QCOMPARE(window.refreshCount, 0);

    window.showAndRaise();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.refreshCount, 1);
    QCOMPARE(window.lastPageNumber, 3);
    QCOMPARE(window.lastImageCount, 2);

    // 保留が無いときは開き直しても計算しない。
    window.showAndRaise();
    QCOMPARE(window.refreshCount, 1);
  }

  // 見えている間は届くたびに計算すること。
  void computesOnEveryUpdateWhileVisible() {
    CountingAnalysisWindow window;
    window.showAndRaise();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.refreshCount, 0);

    window.setAnalysisImages(1, makeImages(2));
    QCOMPARE(window.refreshCount, 1);
    window.setAnalysisImages(2, makeImages(3));
    QCOMPARE(window.refreshCount, 2);
    QCOMPARE(window.lastPageNumber, 2);
    QCOMPARE(window.lastImageCount, 3);
  }

  // 閉じても破棄せず隠すだけにすること。操作の状態を次に開いたときまで保つ。
  // 閉じた後は計算しないこと。null 判定だけでは一度開いた後ずっと計算し続ける。
  void hidesInsteadOfClosingAndStopsComputing() {
    CountingAnalysisWindow window;
    window.showAndRaise();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setAnalysisImages(1, makeImages(2));
    QCOMPARE(window.refreshCount, 1);

    window.close();
    QVERIFY(!window.isVisible());

    window.setAnalysisImages(2, makeImages(2));
    QCOMPARE(window.refreshCount, 1);

    window.showAndRaise();
    QCOMPARE(window.refreshCount, 2);
    QCOMPARE(window.lastPageNumber, 2);
  }
};

QTEST_MAIN(AnalysisWindowBaseTests)

#include "AnalysisWindowBaseTests.moc"
