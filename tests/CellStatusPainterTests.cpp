#include "heiran/ui/CellStatusPainter.hpp"

#include <QImage>
#include <QPainter>
#include <QtTest>

namespace {

QImage renderStatus(const heiran::ui::CellStatusInfo &statusInfo, QSize size = QSize(500, 300)) {
  QImage canvas(size, QImage::Format_RGB32);
  canvas.fill(Qt::black);
  QPainter painter(&canvas);
  heiran::ui::paintCellStatus(&painter, QRect(QPoint(0, 0), size), statusInfo);
  painter.end();
  return canvas;
}

int paintedPixelCount(const QImage &image) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y) != QColor(Qt::black)) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

class CellStatusPainterTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 表示できている状態では何も描かないこと。画像の上へ被せない。
  void drawsNothingWhenReady() {
    QCOMPARE(paintedPixelCount(renderStatus(heiran::ui::CellStatusInfo::ready())), 0);
  }

  // 空欄セルは中立色の 1 行だけ出すこと。失敗と見分けが付く必要がある。
  void drawsAPlainLineForAnEmptyCell() {
    const QImage rendered = renderStatus(heiran::ui::CellStatusInfo::empty("Drop an image here."));
    QVERIFY(paintedPixelCount(rendered) > 0);

    // 失敗表示の下地は描かない。
    bool hasPanel = false;
    for (int y = 0; y < rendered.height() && !hasPanel; ++y) {
      for (int x = 0; x < rendered.width() && !hasPanel; ++x) {
        hasPanel = rendered.pixelColor(x, y) == QColor(64, 38, 40);
      }
    }
    QVERIFY2(!hasPanel, "an empty cell should not draw the failure panel");
  }

  // 読込中も 1 行で出すこと。
  void drawsALineWhileLoading() {
    QVERIFY(paintedPixelCount(renderStatus(heiran::ui::CellStatusInfo::loading())) > 0);
  }

  // 読込失敗は下地と再読込ボタンを出すこと。空欄と見た目を分ける。
  void drawsAPanelForAFailedCell() {
    const QImage rendered =
        renderStatus(heiran::ui::CellStatusInfo::failed("missing.png", "file not found"));

    bool hasPanel = false;
    bool hasButton = false;
    for (int y = 0; y < rendered.height(); ++y) {
      for (int x = 0; x < rendered.width(); ++x) {
        const QColor color = rendered.pixelColor(x, y);
        hasPanel = hasPanel || color == QColor(64, 38, 40);
        hasButton = hasButton || color == QColor(104, 62, 64);
      }
    }
    QVERIFY2(hasPanel, "the failure panel was not drawn");
    QVERIFY2(hasButton, "the reload button was not drawn");
  }

  // 再読込ボタンは失敗表示のときだけ返すこと。
  void reportsTheReloadRectOnlyForFailures() {
    const QSize size(500, 300);
    QVERIFY(heiran::ui::cellReloadButtonRect(size, heiran::ui::CellStatus::Ready).isEmpty());
    QVERIFY(heiran::ui::cellReloadButtonRect(size, heiran::ui::CellStatus::Empty).isEmpty());
    QVERIFY(heiran::ui::cellReloadButtonRect(size, heiran::ui::CellStatus::Loading).isEmpty());
    QVERIFY(!heiran::ui::cellReloadButtonRect(size, heiran::ui::CellStatus::Failed).isEmpty());
  }

  // 描いたボタンと当たり判定の矩形が一致すること。
  //
  // 別々に計算していると、片方だけ動かしたときに「見えているのに押せない」
  // 状態になる。押せる範囲の中がボタンの色で塗られ、外は塗られていないことを見る。
  void drawsTheReloadButtonWhereItCanBePressed() {
    const QSize size(500, 300);
    const QImage rendered =
        renderStatus(heiran::ui::CellStatusInfo::failed("missing.png", "file not found"), size);
    const QRect buttonRect = heiran::ui::cellReloadButtonRect(size, heiran::ui::CellStatus::Failed);
    QVERIFY(!buttonRect.isEmpty());

    // 塗りの色が当たり判定の中に現れ、外には出ないこと。
    // 中心は文字と混ざるため、色そのものが出る場所で見る。
    const QColor buttonColor(104, 62, 64);
    int insideCount = 0;
    QPoint outsideAt(-1, -1);
    for (int y = 0; y < rendered.height(); ++y) {
      for (int x = 0; x < rendered.width(); ++x) {
        if (rendered.pixelColor(x, y) != buttonColor) {
          continue;
        }
        if (buttonRect.contains(QPoint(x, y))) {
          ++insideCount;
        } else if (outsideAt.x() < 0) {
          outsideAt = QPoint(x, y);
        }
      }
    }

    QVERIFY2(insideCount > 0, "the reload button was not painted inside its hit rect");
    QVERIFY2(outsideAt.x() < 0, qPrintable(QString("button colour outside the hit rect at (%1, %2)")
                                               .arg(outsideAt.x())
                                               .arg(outsideAt.y())));

    // 当たり判定の中心が塗られていること。押した場所に何も無いと困る。
    QVERIFY2(rendered.pixelColor(buttonRect.center()) != QColor(Qt::black),
             "the centre of the hit rect is empty");
  }

  // 大きさを変えるとボタンも追従すること。
  void movesTheReloadButtonWithTheCanvas() {
    const QRect small =
        heiran::ui::cellReloadButtonRect(QSize(400, 200), heiran::ui::CellStatus::Failed);
    const QRect large =
        heiran::ui::cellReloadButtonRect(QSize(800, 600), heiran::ui::CellStatus::Failed);
    QVERIFY(large.center().x() > small.center().x());
    QVERIFY(large.center().y() > small.center().y());
    QCOMPARE(large.size(), small.size());
  }

  // 描いた後に painter の状態が戻っていること。呼び出し側が続けて描く。
  void restoresThePainterState() {
    QImage canvas(500, 300, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    painter.setPen(QPen(QColor(10, 200, 30), 3));
    const QColor penBefore = painter.pen().color();
    const bool boldBefore = painter.font().bold();

    heiran::ui::paintCellStatus(&painter, canvas.rect(),
                                heiran::ui::CellStatusInfo::failed("a.png", "broken"));

    QCOMPARE(painter.pen().color(), penBefore);
    QCOMPARE(painter.font().bold(), boldBefore);
  }
};

QTEST_MAIN(CellStatusPainterTests)

#include "CellStatusPainterTests.moc"
