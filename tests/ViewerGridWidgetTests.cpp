#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;

namespace {

// 複数セルの組み合わせを扱うテストの土台。
// 実ファイルを用意し、非同期のデコードが終わるまで待ってから状態を確認する。
class SessionFixture {
public:
  bool isValid() const { return tempDir_.isValid(); }

  // 指定した色で pageCount 枚の PNG を作り、1 つの画像セットとして返す。
  ImageSet makeImageSet(const QString &name, QColor color, int pageCount, QSize size = {32, 24}) {
    const QString setDirectory = QDir(tempDir_.path()).filePath(name);
    QDir().mkpath(setDirectory);

    ImageSet imageSet;
    imageSet.name = name;
    imageSet.label = name;
    imageSet.path = QDir(setDirectory).filePath("*.png");
    for (int page = 0; page < pageCount; ++page) {
      QImage image(size, QImage::Format_RGBA8888);
      image.fill(color.darker(100 + page * 20));
      const QString filePath =
          QDir(setDirectory).filePath(QString("%1.png").arg(page, 4, 10, QChar('0')));
      if (!image.save(filePath)) {
        return {};
      }
      imageSet.resolvedPaths.push_back(filePath);
    }
    return imageSet;
  }

  // 16bit の画像セットを作る。解析用バッファを持つ経路を通すために使う。
  // ページごとに 8bit へ丸めると同じ値になる 2 段階を並べ、精度が落ちれば
  // 区別が付かなくなるようにしておく。
  ImageSet make16BitImageSet(const QString &name, int pageCount) {
    const QString setDirectory = QDir(tempDir_.path()).filePath(name);
    QDir().mkpath(setDirectory);

    ImageSet imageSet;
    imageSet.name = name;
    imageSet.label = name;
    imageSet.path = QDir(setDirectory).filePath("*.png");
    for (int page = 0; page < pageCount; ++page) {
      QImage image(8, 8, QImage::Format_RGBA64);
      image.fill(QColor::fromRgba64(0x0101, 0x0101, 0x0101, 0xFFFF));
      // 1 画素だけ 8bit では表せない値にする。
      image.setPixelColor(1, 1, QColor::fromRgba64(0x1234, 0x5678, 0x9ABC, 0xFFFF));
      const QString filePath =
          QDir(setDirectory).filePath(QString("%1.png").arg(page, 4, 10, QChar('0')));
      if (!image.save(filePath)) {
        return {};
      }
      imageSet.resolvedPaths.push_back(filePath);
    }
    return imageSet;
  }

  // 画素ごとに値が違う画像セット。R が x を、G が y を表すため、画素値表示を
  // 見れば「どの画素を参照したか」が分かる。パンやズームの追従確認に使う。
  ImageSet makeGradientImageSet(const QString &name, int pageCount, QSize size = {64, 48}) {
    const QString setDirectory = QDir(tempDir_.path()).filePath(name);
    QDir().mkpath(setDirectory);

    ImageSet imageSet;
    imageSet.name = name;
    imageSet.label = name;
    imageSet.path = QDir(setDirectory).filePath("*.png");
    for (int page = 0; page < pageCount; ++page) {
      QImage image(size, QImage::Format_RGBA8888);
      for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
          image.setPixelColor(x, y, QColor((x * 3) % 256, (y * 5) % 256, (page * 40) % 256));
        }
      }
      const QString filePath =
          QDir(setDirectory).filePath(QString("%1.png").arg(page, 4, 10, QChar('0')));
      if (!image.save(filePath)) {
        return {};
      }
      imageSet.resolvedPaths.push_back(filePath);
    }
    return imageSet;
  }

  // 読み込めないファイルを持つ画像セットを作る。
  ImageSet makeBrokenImageSet(const QString &name) {
    const QString setDirectory = QDir(tempDir_.path()).filePath(name);
    QDir().mkpath(setDirectory);

    ImageSet imageSet;
    imageSet.name = name;
    imageSet.label = name;
    imageSet.path = QDir(setDirectory).filePath("*.png");
    const QString filePath = QDir(setDirectory).filePath("0000.png");
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
      file.write("not a png");
      file.close();
    }
    imageSet.resolvedPaths.push_back(filePath);
    return imageSet;
  }

private:
  QTemporaryDir tempDir_;
};

SessionModel makeSession(const QVector<ImageSet> &images, int rows, int cols,
                         const QVector<int> &cellImageIndexes) {
  SessionModel session;
  session.viewer.layout.rows = rows;
  session.viewer.layout.cols = cols;
  session.images = images;
  for (const int imageIndex : cellImageIndexes) {
    session.cells.push_back({.imageIndex = imageIndex});
  }
  return session;
}

// セル内の描画面。マウス操作はヘッダーではなくこちらへ送る。
// キャンバスの型は ImageViewWidget.cpp の中にしかないため、名前で引く。
QWidget *cellCanvas(heiran::ui::ImageViewWidget *cell) {
  return cell->findChild<QWidget *>("imageCanvas");
}

void hoverAt(QWidget *canvas, const QPoint &position) {
  QTest::mouseMove(canvas, position);
  QMouseEvent moveEvent(QEvent::MouseMove, QPointF(position), canvas->mapToGlobal(position),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &moveEvent);
}

void dragWith(QWidget *canvas, const QPoint &from, const QPoint &to,
              Qt::KeyboardModifiers modifiers, Qt::MouseButton button = Qt::LeftButton) {
  QTest::mousePress(canvas, button, modifiers, from);
  QMouseEvent moveEvent(QEvent::MouseMove, QPointF(to), canvas->mapToGlobal(to), Qt::NoButton,
                        button, modifiers);
  QApplication::sendEvent(canvas, &moveEvent);
  QTest::mouseRelease(canvas, button, modifiers, to);
}

// QTest にホイールの補助関数が無いため、イベントを直接送る。
void wheelAt(QWidget *canvas, const QPoint &position, int angleDelta,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             Qt::ScrollPhase phase = Qt::NoScrollPhase) {
  QWheelEvent wheelEvent(QPointF(position), canvas->mapToGlobal(position), QPoint(),
                         QPoint(0, angleDelta), Qt::NoButton, modifiers, phase, false);
  QApplication::sendEvent(canvas, &wheelEvent);
}

// "[ 1] (   4,    3): R=..." から "(   4,    3)" を取り出す。
QString pixelCoordinateOf(const QString &pixelInfoLine) {
  const int open = pixelInfoLine.indexOf('(');
  const int close = pixelInfoLine.indexOf(')', open);
  if (open < 0 || close <= open) {
    return {};
  }
  return pixelInfoLine.mid(open, close - open + 1);
}

// 現在ページの読込が終わるまで待つ。読込中のセルが無くなった時点で完了とみなす。
bool waitForCellImages(heiran::ui::ViewerGridWidget *grid) {
  const auto cells = grid->findChildren<heiran::ui::ImageViewWidget *>();
  return QTest::qWaitFor(
      [&cells]() {
        for (const auto *cell : cells) {
          if (cell->cellStatus() == heiran::ui::CellStatus::Loading) {
            return false;
          }
        }
        return true;
      },
      5000);
}

// Extract the base scale from a status such as "Page 1 / 1  Zoom 50.00%".
double scaleInStatus(const QString &status) {
  const int index = status.indexOf("Zoom ");
  if (index < 0) {
    return -1.0;
  }
  return status.mid(index + 5).section(' ', 0, 0).chopped(1).toDouble() / 100.0;
}

}  // namespace

class ViewerGridWidgetTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void changesPagesWithAltWheelAndKeepsNormalWheelZoom() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 400);
    grid.setSession(makeSession({fixture.makeImageSet("pages", Qt::red, 5)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().first());
    const QPoint position(5, 5);
    const double scale = grid.globalViewState().baseScale;
    wheelAt(canvas, position, -120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 1);
    wheelAt(canvas, position, -240, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 3);
    wheelAt(canvas, position, 120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 2);
    wheelAt(canvas, position, 1200, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    wheelAt(canvas, position, -1200, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 4);
    QCOMPARE(grid.globalViewState().baseScale, scale);
    QVERIFY(waitForCellImages(&grid));
    wheelAt(canvas, position, 120);
    QCOMPARE(grid.currentPage(), 4);
    QVERIFY(grid.globalViewState().baseScale > scale);

    grid.setLoopPages(true);
    wheelAt(canvas, position, -120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    wheelAt(canvas, position, 120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 4);
    wheelAt(canvas, position, -360, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 2);
  }

  void accumulatesSmallAltWheelInputsAndClearsUnfinishedGestures() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 400);
    grid.setSession(makeSession({fixture.makeImageSet("pages", Qt::red, 5)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().first());
    const QPoint position(5, 5);
    for (int index = 0; index < 3; ++index) {
      wheelAt(canvas, position, -30, Qt::AltModifier);
      QCOMPARE(grid.currentPage(), 0);
    }
    wheelAt(canvas, position, -30, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 1);
    wheelAt(canvas, position, -90, Qt::AltModifier);
    wheelAt(canvas, position, 120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    wheelAt(canvas, position, -90, Qt::AltModifier);
    wheelAt(canvas, position, 0);
    wheelAt(canvas, position, -30, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(canvas, &leave);
    wheelAt(canvas, position, -90, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    wheelAt(canvas, position, 0, Qt::AltModifier, Qt::ScrollEnd);
    wheelAt(canvas, position, -30, Qt::AltModifier, Qt::ScrollBegin);
    QCOMPARE(grid.currentPage(), 0);
    wheelAt(canvas, position, -90, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 1);
  }

  void changesPagesFromEmptyAndFailedCells() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    auto broken = fixture.makeImageSet("broken", Qt::blue, 3);
    QVERIFY(QFile::remove(broken.resolvedPaths.at(0)));
    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 400);
    grid.setSession(
        makeSession({fixture.makeImageSet("pages", Qt::red, 3), broken}, 1, 3, {0, 1, -1}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.at(1)->cellStatus(), heiran::ui::CellStatus::Failed);
    QCOMPARE(cells.at(2)->cellStatus(), heiran::ui::CellStatus::Empty);
    wheelAt(cellCanvas(cells.at(2)), QPoint(5, 5), -120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 1);
    wheelAt(cellCanvas(cells.at(2)), QPoint(5, 5), 120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 0);
    QVERIFY(waitForCellImages(&grid));
    wheelAt(cellCanvas(cells.at(1)), QPoint(5, 5), -120, Qt::AltModifier);
    QCOMPARE(grid.currentPage(), 1);
  }

  void reportsPageCountFromTheLongestImageSet() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("a", Qt::red, 3), fixture.makeImageSet("b", Qt::green, 5)}, 1, 2,
        {0, 1}));
    QVERIFY(waitForCellImages(&grid));

    QCOMPARE(grid.totalPages(), 5);
    QCOMPARE(grid.currentPage(), 0);
  }

  // 解析用バッファは現在ページと前後 1 ページ分しか残さない。範囲外のページへ
  // 行って戻ったときに、8bit へ丸めた値を返すようになっていないことを確かめる。
  // 予算に余裕があれば、保持範囲を離れても読み直しにならないこと。
  //
  // 以前は範囲の外を無条件で捨てていたため、予算が余っていてもページを戻すたびに
  // 復号し直していた。4K で 1 回あたり数 ms かかる。
  void keepsPagesCachedWhileTheBudgetAllows() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.make16BitImageSet("deep", 6)}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 1);

    // 保持範囲 (前後 1 ページ) を越えて動く。
    grid.setCurrentPage(5);
    QVERIFY(waitForCellImages(&grid));

    // 戻っても読み直しにならない。
    grid.setCurrentPage(0);
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Ready);
  }

  void keepsNativePrecisionAfterLeavingTheAnalysisWindow() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.make16BitImageSet("deep", 6)}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 1);
    const QString before = cells.at(0)->pixelInfoAtImagePosition(QPointF(1.5, 1.5));

    // 予算を最小にして、直前に読んだページ以外が残らない状況を作る。
    // 予算に収まっている限りは捨てないため、絞らないと読み直しが起きない。
    // ここで見たいのは「捨てられた後に読み直したとき元の精度が戻るか」で、
    // 捨てる条件そのものは keepsPagesCachedWhileTheBudgetAllows で見ている。
    grid.setDecodeCacheBudgetBytes(1);

    grid.setCurrentPage(5);
    QVERIFY(waitForCellImages(&grid));

    // ページ 0 は捨てられており、読み直しになる。
    grid.setCurrentPage(0);
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Loading);
    QVERIFY(waitForCellImages(&grid));

    const QString after = cells.at(0)->pixelInfoAtImagePosition(QPointF(1.5, 1.5));
    QCOMPARE(after, before);
    // 既定の 0-255 正規化表示で 0x1234 は 18.132 になる。8bit へ丸めた値を
    // 読んでいれば 18.000 になり、小数部で区別が付く。
    QVERIFY2(after.contains("18.132"), qPrintable(after));
  }

  // docs/テスト観点.md 同期 Case 10: 空欄セルを含む状態でズーム
  void keepsWorkingWithEmptyCells() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    // 2 つ目のセルには画像を割り当てない。
    grid.setSession(makeSession({fixture.makeImageSet("a", Qt::red, 2)}, 1, 2, {0, -1}));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 2);
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Ready);
    QCOMPARE(cells.at(1)->cellStatus(), heiran::ui::CellStatus::Empty);

    // 空欄セルがあってもフィットとページ送りが破綻しない。
    grid.requestFitToCells();
    grid.nextPage();
    QVERIFY(waitForCellImages(&grid));
    QCOMPARE(grid.currentPage(), 1);
    QCOMPARE(cells.at(1)->cellStatus(), heiran::ui::CellStatus::Empty);
  }

  // docs/テスト観点.md 同期 Case 11: 読込失敗セルを含む状態
  void keepsWorkingWithFailedCells() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(
        makeSession({fixture.makeImageSet("a", Qt::red, 1), fixture.makeBrokenImageSet("broken")},
                    1, 2, {0, 1}));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Ready);
    // 読込失敗は空欄と区別される。
    QCOMPARE(cells.at(1)->cellStatus(), heiran::ui::CellStatus::Failed);

    // 失敗セルがあっても解析対象の取得が破綻しない。
    const auto analysisImages = grid.currentAnalysisImages();
    QCOMPARE(analysisImages.size(), 2);
    QVERIFY(!analysisImages.at(0).image.isNull());
    QVERIFY(analysisImages.at(1).image.isNull());
  }

  // docs/テスト観点.md 同期 Case 7 と 8: ページ切り替えで表示状態が保たれる
  void keepsViewStateAcrossPageChange() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeImageSet("a", Qt::red, 3)}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    grid.requestFitToCells();
    const auto beforeSession = grid.sessionSnapshot();

    grid.setCurrentPage(2);
    QVERIFY(waitForCellImages(&grid));

    const auto afterSession = grid.sessionSnapshot();
    QCOMPARE(grid.currentPage(), 2);
    // ページ切り替えでセルの設定は変わらない。
    QCOMPARE(afterSession.cells.at(0).scale, beforeSession.cells.at(0).scale);
    QCOMPARE(afterSession.cells.at(0).gamma, beforeSession.cells.at(0).gamma);
  }

  void clampsPageIndexIntoRange() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeImageSet("a", Qt::red, 2)}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    grid.setCurrentPage(99);
    QCOMPARE(grid.currentPage(), 1);
    grid.setCurrentPage(-5);
    QCOMPARE(grid.currentPage(), 0);

    grid.previousPage();
    QCOMPARE(grid.currentPage(), 0);
    grid.nextPage();
    grid.nextPage();
    QCOMPARE(grid.currentPage(), 1);
  }

  // ページ長が異なる画像セットを並べたとき、短いほうは空欄になる。
  void showsEmptyCellForPagesBeyondAnImageSet() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("a", Qt::red, 1), fixture.makeImageSet("b", Qt::green, 3)}, 1, 2,
        {0, 1}));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Ready);

    grid.setCurrentPage(2);
    QVERIFY(waitForCellImages(&grid));
    QCOMPARE(cells.at(0)->cellStatus(), heiran::ui::CellStatus::Empty);
    QCOMPARE(cells.at(1)->cellStatus(), heiran::ui::CellStatus::Ready);
  }

  void appliesChannelModeToEveryCell() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("a", Qt::red, 1), fixture.makeImageSet("b", Qt::green, 1)}, 1, 2,
        {0, 1}));
    QVERIFY(waitForCellImages(&grid));

    grid.setChannelModeForAllCells(heiran::ui::ChannelMode::Gray);
    const auto uniform = grid.uniformChannelMode();
    QVERIFY(uniform.has_value());
    QCOMPARE(*uniform, heiran::ui::ChannelMode::Gray);

    grid.setChannelModeForCell(1, heiran::ui::ChannelMode::Red);
    QVERIFY(!grid.uniformChannelMode().has_value());
  }

  void appliesInvertToEveryCellAndPersistsIt() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeImageSet("a", Qt::red, 1)}, 1, 2, {0, 0}));
    QVERIFY(waitForCellImages(&grid));

    QVERIFY(!grid.allCellsInverted());
    grid.setInvertForAllCells(true);
    QVERIFY(grid.allCellsInverted());

    // 反転はセッションへ保存される値である。
    const auto session = grid.sessionSnapshot();
    QCOMPARE(session.cells.at(0).invert, true);
    QCOMPARE(session.cells.at(1).invert, true);
  }

  // docs/テスト観点.md 画素値 Case 15: 空欄セルと読込失敗セルの画素値表示
  void reportsEmptyAndFailedCellsInPixelInfo() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(
        makeSession({fixture.makeGradientImageSet("ok", 1), fixture.makeBrokenImageSet("broken")},
                    1, 3, {0, -1, 1}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 3);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    auto *canvas = cellCanvas(cells.at(0));
    QVERIFY(canvas != nullptr);
    hoverAt(canvas, canvas->rect().center());
    QVERIFY(pixelInfoSpy.count() > 0);

    // 先頭 2 行は page と座標で、その後がセルごとの 1 行。
    const auto lines = pixelInfoSpy.last().at(0).toStringList();
    QCOMPARE(lines.size(), 5);
    QVERIFY2(!lines.at(2).contains("no image"), qPrintable(lines.at(2)));
    QVERIFY2(lines.at(3).contains("no image"), qPrintable(lines.at(3)));
    QVERIFY2(lines.at(4).contains("load failed"), qPrintable(lines.at(4)));
  }

  void updatesPixelInfoAfterPanning_data() {
    QTest::addColumn<Qt::MouseButton>("button");
    QTest::newRow("left") << Qt::LeftButton;
    QTest::newRow("middle") << Qt::MiddleButton;
  }

  // docs/テスト観点.md 画素値 Case 16: パン後の画素値再計算
  void updatesPixelInfoAfterPanning() {
    QFETCH(Qt::MouseButton, button);
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);
    const QPoint samplePoint(40, 30);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    hoverAt(canvas, samplePoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const QString beforePan = pixelInfoSpy.last().at(0).toStringList().at(1);

    // 画像を右下へ動かすと、同じビュー座標は左上寄りの画素を指すようになる。
    dragWith(canvas, QPoint(60, 50), QPoint(80, 70), Qt::NoModifier, button);

    pixelInfoSpy.clear();
    hoverAt(canvas, samplePoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const QString afterPan = pixelInfoSpy.last().at(0).toStringList().at(1);

    QVERIFY2(beforePan != afterPan, qPrintable(beforePan + " / " + afterPan));
  }

  // パンの描き直しは画面が出せる数までに抑えること。
  //
  // マウスは毎秒 700 回ほど動きを送ってくる。そのたびに描き直していたため、
  // 120Hz の画面に対して 6 倍の枚数を積んでいた。表示されずに捨てられる分で、
  // 後から出す 1 枚を遅らせるだけになる。
  void drawsNoFasterThanTheScreenWhilePanning() {
    // 画面の更新に合わせて描く仕組みの確認のため、実際の画面と GPU が要る。offscreen
    // では、画面の更新が来ない。
    // `-DHEIRAN_TEST_QPA_PLATFORM=` (空) で構成し直すと、実際の窓で動く。
    if (QGuiApplication::platformName() == "offscreen") {
      QSKIP("Needs a native platform: the offscreen platform has no screen refresh.");
    }
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = qobject_cast<QOpenGLWidget *>(
        cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0)));
    QVERIFY(canvas != nullptr);

    int frameCount = 0;
    QObject::connect(canvas, &QOpenGLWidget::frameSwapped, canvas,
                     [&frameCount]() { ++frameCount; });

    // 1 画素ずつ往復させ、届く限りの速さで動きを送る。
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 150));
    int moveCount = 0;
    QElapsedTimer burstTimer;
    burstTimer.start();
    while (burstTimer.elapsed() < 300) {
      const QPoint position(200 + (moveCount % 2), 150);
      QMouseEvent moveEvent(QEvent::MouseMove, QPointF(position), canvas->mapToGlobal(position),
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &moveEvent);
      ++moveCount;
      QApplication::processEvents();
    }

    // 狙いは画面の更新間隔の半分。タイマーの揺れを見込んで、さらに 2 倍まで許す。
    const qreal refreshRate = canvas->screen() != nullptr ? canvas->screen()->refreshRate() : 60.0;
    const int allowedFrames = static_cast<int>(0.3 * refreshRate * 2.0 * 2.0);
    qInfo("%d frames for %d moves (allowed %d at %.1f Hz)", frameCount, moveCount, allowedFrames,
          refreshRate);

    // 送れた数が許容枚数を超えていないと、間引きの有無を区別できない。
    QVERIFY2(moveCount > allowedFrames,
             qPrintable(QString("only %1 moves were sent, need more than %2")
                            .arg(moveCount)
                            .arg(allowedFrames)));
    // 間引いていなければ moveCount と同じ数になる。
    QVERIFY2(frameCount <= allowedFrames,
             qPrintable(QString("%1 frames for %2 moves").arg(frameCount).arg(moveCount)));

    // 間引いた結果、最後の 1 回まで捨ててはいけない。指を止めた位置が出ないと困る。
    const int framesBeforeLastMove = frameCount;
    const QPoint lastPosition(240, 150);
    QMouseEvent lastMove(QEvent::MouseMove, QPointF(lastPosition),
                         canvas->mapToGlobal(lastPosition), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
    QApplication::sendEvent(canvas, &lastMove);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, lastPosition);
    QVERIFY(QTest::qWaitFor([&]() { return frameCount > framesBeforeLastMove; }, 2000));
  }

  // docs/テスト観点.md 画素値 Case 17: ズーム後のルーペ追従
  void keepsTheZoomAnchorPixelUnderTheCursor() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);
    const QPoint anchorPoint(40, 30);

    // 固定点から離れた点。倍率が変われば参照する元画素が変わるため、
    // ホイールが届かず素通りしていないかの確認に使う。
    const QPoint awayPoint(anchorPoint + QPoint(60, 40));

    QSignalSpy previewSpy(&grid, &heiran::ui::ViewerGridWidget::pixelPreviewChanged);
    hoverAt(canvas, anchorPoint);
    QVERIFY(previewSpy.count() > 0);
    const auto beforeZoom = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(beforeZoom.valid);

    previewSpy.clear();
    hoverAt(canvas, awayPoint);
    QVERIFY(previewSpy.count() > 0);
    const auto beforeZoomAway = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(beforeZoomAway.valid);

    wheelAt(canvas, anchorPoint, 120);

    previewSpy.clear();
    hoverAt(canvas, anchorPoint);
    QVERIFY(previewSpy.count() > 0);
    const auto afterZoom = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(afterZoom.valid);

    previewSpy.clear();
    hoverAt(canvas, awayPoint);
    QVERIFY(previewSpy.count() > 0);
    const auto afterZoomAway = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(afterZoomAway.valid);

    // ズームの固定点なので、同じビュー座標には同じ元画素が残る。
    QCOMPARE(std::floor(afterZoom.imagePosition.x()), std::floor(beforeZoom.imagePosition.x()));
    QCOMPARE(std::floor(afterZoom.imagePosition.y()), std::floor(beforeZoom.imagePosition.y()));
    // 固定点から離れた点は倍率の変化分だけずれる。
    QVERIFY2(std::floor(afterZoomAway.imagePosition.x()) !=
                 std::floor(beforeZoomAway.imagePosition.x()),
             qPrintable(QString("%1 / %2")
                            .arg(beforeZoomAway.imagePosition.x())
                            .arg(afterZoomAway.imagePosition.x())));
  }

  // ダブルクリックでフィットと等倍を行き来すること。多くのビューアの定番の操作。
  void togglesBetweenFitAndActualSizeWithDoubleClick() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1, {800, 600})}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    grid.requestFitToCells();

    QSignalSpy statusSpy(&grid, &heiran::ui::ViewerGridWidget::statusChanged);
    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);
    const QPoint center = canvas->rect().center();

    // フィットしているとき、800x600 の画像は 400x300 の窓より小さくなる。
    grid.requestFitToCells();
    QVERIFY(statusSpy.count() > 0);
    const double fitScale = scaleInStatus(statusSpy.last().at(0).toString());
    QVERIFY2(fitScale > 0.0 && fitScale < 0.9, qPrintable(QString::number(fitScale)));

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(scaleInStatus(statusSpy.last().at(0).toString()), 1.0);

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    const double backScale = scaleInStatus(statusSpy.last().at(0).toString());
    QVERIFY2(std::abs(backScale - fitScale) < 0.011,
             qPrintable(QString("%1 vs %2").arg(backScale).arg(fitScale)));
  }

  // 等倍へは押した位置を固定点にすること。拡大したい場所を押すのが自然で、
  // 中央へ寄ってしまうと見たい場所が視界から外れる。
  void zoomsToActualSizeAroundTheDoubleClickedPoint() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1, {800, 600})}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    grid.requestFitToCells();

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);
    const QPoint clicked(120, 90);

    QSignalSpy previewSpy(&grid, &heiran::ui::ViewerGridWidget::pixelPreviewChanged);
    hoverAt(canvas, clicked);
    QVERIFY(previewSpy.count() > 0);
    const auto before = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(before.valid);

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, clicked);

    previewSpy.clear();
    hoverAt(canvas, clicked);
    QVERIFY(previewSpy.count() > 0);
    const auto after = previewSpy.last().at(0).value<heiran::ui::LoupePreview>();
    QVERIFY(after.valid);
    QVERIFY2(std::abs(after.imagePosition.x() - before.imagePosition.x()) < 2.5 &&
                 std::abs(after.imagePosition.y() - before.imagePosition.y()) < 2.5,
             qPrintable(QString("(%1, %2) -> (%3, %4)")
                            .arg(before.imagePosition.x())
                            .arg(before.imagePosition.y())
                            .arg(after.imagePosition.x())
                            .arg(after.imagePosition.y())));
  }

  // 90 度単位で回せて、一周すると元へ戻ること。負の向きも同じ範囲へ収まる。
  void rotatesTheActiveCellByQuarterTurns() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    grid.rotateActiveCellBy(1);
    QCOMPARE(grid.sessionSnapshot().cells.at(0).rotation, 1);
    for (int turn = 0; turn < 3; ++turn) {
      grid.rotateActiveCellBy(1);
    }
    QCOMPARE(grid.sessionSnapshot().cells.at(0).rotation, 0);

    grid.rotateActiveCellBy(-1);
    QCOMPARE(grid.sessionSnapshot().cells.at(0).rotation, 3);
  }

  // 回すと保存の対象になること。回したまま閉じても未保存の警告が出ないと、
  // 回した状態が黙って失われる。
  void notifiesPersistentStateWhenRotating() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    QSignalSpy persistentSpy(&grid, &heiran::ui::ViewerGridWidget::persistentStateChanged);
    grid.rotateActiveCellBy(1);
    QVERIFY(persistentSpy.count() > 0);
  }

  // 見ている画像をクリップボードへ渡せること。
  void copiesTheActiveCellImageToTheClipboard() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1, {64, 48})}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    QGuiApplication::clipboard()->clear();
    grid.copyActiveCellImage();

    const QImage copied = QGuiApplication::clipboard()->image();
    QVERIFY(!copied.isNull());
    QCOMPARE(copied.size(), QSize(64, 48));
  }

  // ページ切り替え時のフィットは、画像を画面の中央へ置く。
  void centersTheImageWhenFittingOnPageChange() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 3, {64, 48})}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    grid.setFitOnPageChange(true);
    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    hoverAt(canvas, canvas->rect().center());
    QVERIFY(pixelInfoSpy.count() > 0);

    // 画面中央に画像の中央が来ていれば、そこで拾う画素は (32, 24) 付近になる。
    const QString coordinate = pixelInfoSpy.last().at(0).toStringList().at(1);
    const auto parts = coordinate.split(' ', Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 2);
    const int imageX = parts.at(0).mid(2).toInt();
    const int imageY = parts.at(1).mid(2).toInt();
    QVERIFY2(std::abs(imageX - 32) <= 1 && std::abs(imageY - 24) <= 1, qPrintable(coordinate));
  }

  // 読み込み済み (先読み済み) のページへ移るときも、フィットする。読み込み待ちが無いと、
  // 読み終えの通知が来ないため、ページを替えた側で合わせる必要がある。
  void fitsWhenMovingToAnAlreadyLoadedPage() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 3, {64, 48})}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    QSignalSpy statusSpy(&grid, &heiran::ui::ViewerGridWidget::statusChanged);

    grid.setFitOnPageChange(true);
    const double fitScale = scaleInStatus(statusSpy.last().at(0).toString());
    QVERIFY(fitScale > 0.0);

    // ズームして、次のページが先読みで読み込まれるのを待つ。
    grid.zoomBy(3.0);
    QVERIFY(scaleInStatus(statusSpy.last().at(0).toString()) > fitScale * 2.0);
    QTest::qWait(800);

    for (const auto change : {0, 1, 2}) {
      grid.zoomBy(3.0);
      QVERIFY(scaleInStatus(statusSpy.last().at(0).toString()) > fitScale * 2.0);
      if (change == 0) {
        grid.nextPage();
      } else if (change == 1) {
        grid.previousPage();
      } else {
        grid.setCurrentPage(2);
      }
      QVERIFY(waitForCellImages(&grid));
      const double scale = scaleInStatus(statusSpy.last().at(0).toString());
      QVERIFY2(
          std::abs(scale - fitScale) < 0.01,
          qPrintable(QString("change %1: scale %2, fit %3").arg(change).arg(scale).arg(fitScale)));
    }
  }

  // 有効にしてあれば、ページ送りだけでなく最初の表示もフィットして中央へ置く。
  // ここで合わせないと、開いた直後だけ等倍の左上表示になり設定と食い違う。
  void fitsTheFirstDisplayWhenFitOnPageChangeIsOn() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setFitOnPageChange(true);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    // 一度フィットを済ませてから別の画像を開く。設定を有効にした直後だけを見ると、
    // その時点の保留フラグで通ってしまい、開き直しの経路を確かめられない。
    grid.setSession(makeSession({fixture.makeGradientImageSet("first", 2, {800, 600})}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    // 画面より大きい画像。等倍のままだと中央には隅の画素が来る。
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 2, {800, 600})}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    hoverAt(canvas, canvas->rect().center());
    QVERIFY(pixelInfoSpy.count() > 0);

    const QString coordinate = pixelInfoSpy.last().at(0).toStringList().at(1);
    const auto parts = coordinate.split(' ', Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 2);
    const int imageX = parts.at(0).mid(2).toInt();
    const int imageY = parts.at(1).mid(2).toInt();
    // 許容は画像座標で 4 画素。フィット後の倍率は約 0.45 なので、画面上では
    // 2 画素に満たない。フィットしていなければ画面中央は (199, 135) 付近になり、
    // この幅では通らない。
    QVERIFY2(std::abs(imageX - 400) <= 4 && std::abs(imageY - 300) <= 4, qPrintable(coordinate));
  }

  // docs/テスト観点.md 同期 Case 9: 画像サイズの異なるセルでズーム
  void appliesZoomToCellsOfDifferentImageSizes() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("big", 1, {64, 48}),
                                 fixture.makeGradientImageSet("small", 1, {16, 12})},
                                1, 2, {0, 1}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 2);
    auto *canvas = cellCanvas(cells.at(0));
    QVERIFY(canvas != nullptr);
    const QPoint anchorPoint(10, 8);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    hoverAt(canvas, anchorPoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const auto before = pixelInfoSpy.last().at(0).toStringList();

    // 固定点から離れた点。ホイールが届いていれば参照先がずれる。
    const QPoint awayPoint(anchorPoint + QPoint(50, 30));
    pixelInfoSpy.clear();
    hoverAt(canvas, awayPoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const QString beforeAway = pixelInfoSpy.last().at(0).toStringList().at(1);

    wheelAt(canvas, anchorPoint, 120);

    pixelInfoSpy.clear();
    hoverAt(canvas, awayPoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const QString afterAway = pixelInfoSpy.last().at(0).toStringList().at(1);
    QVERIFY2(beforeAway != afterAway, qPrintable(beforeAway + " / " + afterAway));

    pixelInfoSpy.clear();
    hoverAt(canvas, anchorPoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const auto after = pixelInfoSpy.last().at(0).toStringList();

    // 固定点は操作対象セル基準で保たれる。
    QCOMPARE(after.at(1), before.at(1));

    // 画像サイズが違っても左上基準で同じ画像座標を参照する。
    const QString bigCellCoordinate = pixelCoordinateOf(after.at(2));
    const QString smallCellCoordinate = pixelCoordinateOf(after.at(3));
    QVERIFY2(!bigCellCoordinate.isEmpty(), qPrintable(after.at(2)));
    QVERIFY2(!smallCellCoordinate.isEmpty(), qPrintable(after.at(3)));
    QCOMPARE(smallCellCoordinate, bigCellCoordinate);
  }

  void clearsTheSelectionWhenShiftDragStaysBelowTheDragThreshold() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 1)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    auto *view = grid.findChild<heiran::ui::ImageViewWidget *>();
    auto *canvas = cellCanvas(view);
    QVERIFY(canvas);
    for (const QPoint delta : {QPoint(), QPoint(1, 1)}) {
      dragWith(canvas, QPoint(10, 10), QPoint(45, 45), Qt::ShiftModifier);
      QVERIFY(grid.currentAnalysisImages().first().selectionRect.has_value());
      QSignalSpy selectionRequests(view, &heiran::ui::ImageViewWidget::selectionRequested);
      dragWith(canvas, QPoint(20, 20), QPoint(20, 20) + delta, Qt::ShiftModifier);
      QVERIFY(!grid.currentAnalysisImages().first().selectionRect.has_value());
      QVERIFY(selectionRequests.isEmpty());
    }
  }

  // docs/テスト観点.md 同期 Case 12: 選択範囲ありでページ切り替え
  void keepsTheSelectionAcrossPageChanges() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 3)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);

    // Shift を押しながらのドラッグが選択範囲になる。
    dragWith(canvas, QPoint(10, 10), QPoint(40, 30), Qt::ShiftModifier);

    const auto selectedImages = grid.currentAnalysisImages();
    QCOMPARE(selectedImages.size(), 1);
    QVERIFY(selectedImages.at(0).selectionRect.has_value());
    const QRect selection = *selectedImages.at(0).selectionRect;
    QVERIFY(!selection.isEmpty());

    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));

    const auto afterPageChange = grid.currentAnalysisImages();
    QCOMPARE(afterPageChange.size(), 1);
    QVERIFY(afterPageChange.at(0).selectionRect.has_value());
    QCOMPARE(*afterPageChange.at(0).selectionRect, selection);
  }

  // docs/テスト観点.md 画素値 Case 18: ページ切り替え時のルーペ更新
  void updatesTheLoupeWhenThePageChanges() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeGradientImageSet("g", 3)}, 1, 1, {0}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));

    auto *canvas = cellCanvas(grid.findChildren<heiran::ui::ImageViewWidget *>().at(0));
    QVERIFY(canvas != nullptr);
    const QPoint samplePoint(40, 30);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    hoverAt(canvas, samplePoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const QString beforePage = pixelInfoSpy.last().at(0).toStringList().at(2);

    grid.setCurrentPage(1);
    QVERIFY(waitForCellImages(&grid));

    pixelInfoSpy.clear();
    hoverAt(canvas, samplePoint);
    QVERIFY(pixelInfoSpy.count() > 0);
    const auto afterLines = pixelInfoSpy.last().at(0).toStringList();
    QCOMPARE(afterLines.at(0), QString("page=2"));
    // 画素の並びはページごとに違うため、同じ位置でも値が変わる。
    QVERIFY2(afterLines.at(2) != beforePage, qPrintable(beforePage + " / " + afterLines.at(2)));
  }

  void notifiesWhenTheViewChanges() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession({fixture.makeImageSet("a", Qt::red, 3)}, 1, 1, {0}));
    QVERIFY(waitForCellImages(&grid));

    QSignalSpy viewChangedSpy(&grid, &heiran::ui::ViewerGridWidget::viewChanged);
    grid.nextPage();
    QVERIFY(viewChangedSpy.count() > 0);

    QSignalSpy resetSpy(&grid, &heiran::ui::ViewerGridWidget::viewChanged);
    grid.resetView();
    QVERIFY(resetSpy.count() > 0);
  }

  // ---- 差分のセル ----

  void stepsBetweenChangedRegionsAndStopsBlinkingWhenThePageChanges() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    auto base = fixture.makeImageSet("base", Qt::black, 2, {96, 64});
    auto target = fixture.makeImageSet("target", Qt::black, 2, {96, 64});
    QImage changed(target.resolvedPaths.first());
    QVERIFY(!changed.isNull());
    changed.setPixelColor(10, 10, Qt::white);
    changed.setPixelColor(82, 42, Qt::white);
    QVERIFY(changed.save(target.resolvedPaths.first()));

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    auto session = makeSession({base, target}, 1, 3, {0, 1, -1});
    session.cells[2].scale = 2.0;
    grid.setSession(session);
    grid.setCellDiff(2, heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1});
    auto *cell = grid.findChildren<heiran::ui::ImageViewWidget *>().at(2);
    auto *previous = cell->findChild<QToolButton *>("previousDiffRegion");
    auto *next = cell->findChild<QToolButton *>("nextDiffRegion");
    auto *blink = cell->findChild<QToolButton *>("blinkDiffImages");
    auto *timer = cell->findChild<QTimer *>("diffBlinkTimer");
    QVERIFY(previous && next && blink && timer);
    QTRY_VERIFY(next->isEnabled());

    next->click();
    QCOMPARE(cell->diffRegionInDisplay(), QRectF(10, 10, 1, 1));
    const auto firstOffset = grid.globalViewState().baseOffsetX;
    next->click();
    QCOMPARE(cell->diffRegionInDisplay(), QRectF(82, 42, 1, 1));
    QCOMPARE(grid.globalViewState().baseOffsetX - firstOffset, 144.0);
    next->click();
    QCOMPARE(cell->diffRegionInDisplay(), QRectF(10, 10, 1, 1));
    previous->click();
    QCOMPARE(cell->diffRegionInDisplay(), QRectF(82, 42, 1, 1));

    QSignalSpy blinkFrames(timer, &QTimer::timeout);
    const auto differenceKey = cell->currentDecodedImage().displayImage.cacheKey();
    blink->click();
    QVERIFY(blink->isChecked() && timer->isActive());
    QTRY_VERIFY(blinkFrames.count() > 0);
    QCOMPARE(cell->currentDecodedImage().displayImage.cacheKey(), differenceKey);
    blink->click();
    QVERIFY(!blink->isChecked() && !timer->isActive());
    blink->click();

    grid.nextPage();
    QTRY_VERIFY(!timer->isActive());
    QVERIFY(!blink->isChecked());
    QTRY_VERIFY(blink->isEnabled());
    QVERIFY(!next->isEnabled());
    QVERIFY(cell->diffRegionInDisplay().isEmpty());
  }

  void blinkDrawsBothOriginalImagesInTheSameCanvas() {
    if (QGuiApplication::platformName() == "offscreen") {
      QSKIP("Blink rendering requires an OpenGL window.");
    }
    SessionFixture fixture;
    QVERIFY(fixture.isValid());
    heiran::ui::ViewerGridWidget grid;
    grid.setAttribute(Qt::WA_ShowWithoutActivating);
    grid.resize(720, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("base", Qt::red, 1), fixture.makeImageSet("target", Qt::green, 1)}, 1,
        3, {0, 1, -1}));
    grid.setCellDiff(2, heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1});
    grid.show();
    auto *cell = grid.findChildren<heiran::ui::ImageViewWidget *>().at(2);
    auto *blink = cell->findChild<QToolButton *>("blinkDiffImages");
    auto *canvas = cell->findChild<QOpenGLWidget *>("imageCanvas");
    QVERIFY(blink && canvas);
    QTRY_VERIFY(blink->isEnabled() && canvas->isValid());
    grid.requestFitToCells();

    const auto canvasCenter = [canvas]() {
      const QImage frame = canvas->grabFramebuffer();
      return frame.isNull() ? QColor() : frame.pixelColor(frame.rect().center());
    };
    const QColor diffColor = canvasCenter();
    QVERIFY(diffColor.isValid());
    blink->click();
    QTRY_COMPARE(canvasCenter(), QColor(Qt::red));
    QTRY_COMPARE(canvasCenter(), QColor(Qt::green));
    blink->click();
    QTRY_COMPARE(canvasCenter(), diffColor);
  }

  // 空きセルへ、他の 2 つのセルの差分を出す。通常のセルとして画像を持ち、設定は保存される。
  void showsTheDifferenceOfTwoCellsInADedicatedCell() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("a", Qt::red, 1), fixture.makeImageSet("b", Qt::green, 1)}, 1, 3,
        {0, 1, -1}));
    QVERIFY(waitForCellImages(&grid));
    QCOMPARE(grid.firstEmptyCellIndex(), 2);

    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    QCOMPARE(cells.size(), 3);
    QSignalSpy changedSpy(&grid, &heiran::ui::ViewerGridWidget::cellDiffChanged);

    heiran::domain::CellDiff diff{.baseCell = 0, .targetCell = 1};
    diff.settings.autoRange = false;
    grid.setCellDiff(2, diff);
    QTRY_COMPARE(cells.at(2)->cellStatus(), heiran::ui::CellStatus::Ready);
    QCOMPARE(cells.at(2)->displayImageSize(), QSize(32, 24));
    QVERIFY(changedSpy.count() > 0);

    // 赤と緑の輝度の差 (0.2126 -> 0.7152 の差 = 約 128 / 255) が、そのまま絵になる。
    const auto metrics = grid.cellDiffMetrics(2);
    QVERIFY(metrics.has_value());
    QCOMPARE(metrics->pixelCount, 32 * 24);
    QVERIFY2(std::abs(metrics->maxAbsDiff - 128.6) < 1.0,
             qPrintable(QString::number(metrics->maxAbsDiff)));
    QVERIFY(grid.cellDiffMessage(2).isEmpty());
    QVERIFY(std::abs(qRed(cells.at(2)->currentImage().pixel(3, 3)) - 129) <= 1);
    // 比べた 2 つのセルは、元の画像のまま。
    QCOMPARE(cells.at(0)->currentImage().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(grid.firstEmptyCellIndex(), -1);

    // 保存される。
    const auto session = grid.sessionSnapshot();
    QVERIFY(session.cells.at(2).diff.has_value());
    QCOMPARE(*session.cells.at(2).diff, diff);

    // やめると、空欄へ戻る。
    grid.setCellDiff(2, std::nullopt);
    QTRY_COMPARE(cells.at(2)->cellStatus(), heiran::ui::CellStatus::Empty);
    QCOMPARE(grid.firstEmptyCellIndex(), 2);
  }

  // 画像のあるセルを、基準のセルとの差分に置き換える。やめると元の画像へ戻る。
  void replacesACellWithItsDifferenceAndRestoresIt() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(400, 300);
    grid.setSession(makeSession(
        {fixture.makeImageSet("a", Qt::red, 1), fixture.makeImageSet("b", Qt::green, 1)}, 1, 2,
        {0, 1}));
    QVERIFY(waitForCellImages(&grid));
    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();

    grid.setCellDiff(1, heiran::domain::CellDiff{.baseCell = 0, .targetCell = -1});
    QTRY_VERIFY(grid.cellDiffMetrics(1).has_value());
    QCOMPARE(cells.at(1)->currentDecodedImage().decoderBackend, QStringLiteral("diff"));

    grid.setCellDiff(1, std::nullopt);
    QTRY_COMPARE(cells.at(1)->currentDecodedImage().decoderBackend.contains("diff"), false);
    QCOMPARE(cells.at(1)->currentImage().pixelColor(0, 0), QColor(Qt::green));
  }

  // 設定を変えると、作り直す。大きさの違う画像は、重なる部分だけ比べるか、指定の大きさへ合わせる。
  void rebuildsOnSettingsAndHandlesDifferentSizes() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(
        makeSession({fixture.makeImageSet("a", Qt::red, 1), fixture.makeImageSet("b", Qt::green, 1),
                     fixture.makeImageSet("c", Qt::blue, 1, QSize(16, 12))},
                    1, 4, {0, 1, 2, -1}));
    QVERIFY(waitForCellImages(&grid));
    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();

    heiran::domain::CellDiff diff{.baseCell = 0, .targetCell = 1};
    diff.settings.autoRange = false;
    grid.setCellDiff(3, diff);
    QTRY_COMPARE(cells.at(3)->cellStatus(), heiran::ui::CellStatus::Ready);
    const int before = qRed(cells.at(3)->currentImage().pixel(2, 2));

    diff.settings.gain = 0.5;
    grid.setCellDiff(3, diff);
    QTRY_VERIFY(std::abs(qRed(cells.at(3)->currentImage().pixel(2, 2)) - before / 2) <= 1);

    // 16x12 の画像とは、重なる部分 (16x12) だけを比べる。
    grid.setCellDiff(3, heiran::domain::CellDiff{.baseCell = 0, .targetCell = 2});
    QTRY_COMPARE(cells.at(3)->displayImageSize(), QSize(16, 12));
    QVERIFY(grid.cellDiffMessage(3).isEmpty());
    QCOMPARE(grid.cellDiffMetrics(3)->pixelCount, 16 * 12);

    // 大きさを合わせる指定があれば、比べられる。差のセルは、基準の大きさ。
    heiran::domain::CellDiff matched{.baseCell = 0, .targetCell = 2};
    matched.settings.sizeMatch = heiran::domain::DiffSizeMatch::ToBase;
    grid.setCellDiff(3, matched);
    QTRY_COMPARE(cells.at(3)->displayImageSize(), QSize(32, 24));
    QVERIFY(grid.cellDiffMessage(3).isEmpty());
    QVERIFY(grid.cellDiffMetrics(3).has_value());
  }

  // 差分のセルも、通常のセルと同じに同期する。カーソルの画素が、全セルで同じ座標を指し、
  // 差分のセルも画素値を答える。
  void readsPixelValuesInTheDifferenceCellAtTheSameCoordinate() {
    SessionFixture fixture;
    QVERIFY(fixture.isValid());

    heiran::ui::ViewerGridWidget grid;
    grid.resize(600, 300);
    grid.setSession(
        makeSession({fixture.makeGradientImageSet("a", 1), fixture.makeGradientImageSet("b", 2)}, 1,
                    3, {0, 1, -1}));
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QVERIFY(waitForCellImages(&grid));
    const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
    grid.setCellDiff(2, heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1});
    QTRY_COMPARE(cells.at(2)->cellStatus(), heiran::ui::CellStatus::Ready);

    QSignalSpy pixelInfoSpy(&grid, &heiran::ui::ViewerGridWidget::pixelInfoChanged);
    auto *canvas = cellCanvas(cells.at(0));
    QVERIFY(canvas != nullptr);
    hoverAt(canvas, canvas->rect().center());
    QVERIFY(pixelInfoSpy.count() > 0);
    const auto lines = pixelInfoSpy.last().at(0).toStringList();
    QCOMPARE(lines.size(), 5);
    QVERIFY2(!lines.at(4).contains("no image"), qPrintable(lines.at(4)));
    QCOMPARE(pixelCoordinateOf(lines.at(4)), pixelCoordinateOf(lines.at(2)));
  }
};

QTEST_MAIN(ViewerGridWidgetTests)

#include "ViewerGridWidgetTests.moc"
