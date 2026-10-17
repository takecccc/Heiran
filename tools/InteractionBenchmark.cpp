// パン、ズーム、hover の所要時間を測る。
//
// これらは GUI 操作が要るため手作業でしか測れなかったが、テストで使っている
// イベント合成を持ち込めば自動で測れる。
//
// 描画まで含めるため、イベントを送ったあとに processEvents() で溜まった
// paint を捌いてから時間を止める。QOpenGLWidget の repaint() では paintGL が
// 走らず、イベント処理だけを測ることになる。
//
// 測るのは UI スレッドが使う時間。GL の描画命令は非同期に積まれるため、GPU 側
// の完了時間は含まない。操作の引っ掛かりを見るにはこれで足りる。
//
// ctest には登録しない。必要なときだけ実行する。

#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QTextStream>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <numeric>

namespace {

double toMilliseconds(qint64 nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

void reportDurations(QTextStream &out, const QString &label, QVector<qint64> durations) {
  if (durations.isEmpty()) {
    out << label << " : (no samples)" << Qt::endl;
    return;
  }

  std::sort(durations.begin(), durations.end());
  const qint64 totalNs = std::accumulate(durations.begin(), durations.end(), qint64{0});
  out << Qt::fixed << qSetRealNumberPrecision(3);
  out << QString("%1 : median %2 ms  mean %3 ms  min %4 ms  max %5 ms  samples %6")
             .arg(label, -6)
             .arg(toMilliseconds(durations.at(durations.size() / 2)))
             .arg(toMilliseconds(totalNs / durations.size()))
             .arg(toMilliseconds(durations.first()))
             .arg(toMilliseconds(durations.last()))
             .arg(durations.size())
      << Qt::endl;
}

// セル内の描画面。マウス操作はヘッダーではなくこちらへ送る。
QWidget *cellCanvas(heiran::ui::ImageViewWidget *cell) {
  return cell->findChild<QWidget *>("imageCanvas");
}

void sendMouseMove(QWidget *canvas, const QPoint &position, Qt::MouseButtons buttons) {
  QMouseEvent moveEvent(QEvent::MouseMove, QPointF(position), canvas->mapToGlobal(position),
                        Qt::NoButton, buttons, Qt::NoModifier);
  QApplication::sendEvent(canvas, &moveEvent);
}

void sendMouseButton(QWidget *canvas, QEvent::Type type, const QPoint &position) {
  QMouseEvent buttonEvent(type, QPointF(position), canvas->mapToGlobal(position), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
  QApplication::sendEvent(canvas, &buttonEvent);
}

void sendWheel(QWidget *canvas, const QPoint &position, int angleDelta) {
  QWheelEvent wheelEvent(QPointF(position), canvas->mapToGlobal(position), QPoint(),
                         QPoint(0, angleDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                         false);
  QApplication::sendEvent(canvas, &wheelEvent);
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QTextStream out(stdout);

  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.size() < 2) {
    out << "usage: heiran_interaction_benchmark <image-file> [iterations] [cells]\n";
    out << "  The image file's directory is used as the image set, the same way a\n";
    out << "  dropped file is. cells is the number of cells in one row.\n";
    return 1;
  }

  const QString imagePath = arguments.at(1);
  const QFileInfo imageInfo(imagePath);
  if (!imageInfo.exists()) {
    out << "file not found: " << imagePath << "\n";
    return 1;
  }

  const int iterations = arguments.size() >= 3 ? std::max(1, arguments.at(2).toInt()) : 60;
  const int cellCount = arguments.size() >= 4 ? std::max(1, arguments.at(3).toInt()) : 1;

  heiran::domain::SessionModel session;
  session.viewer.layout.rows = 1;
  session.viewer.layout.cols = cellCount;
  heiran::domain::ImageSet imageSet;
  imageSet.name = imageInfo.completeBaseName();
  imageSet.label = imageSet.name;
  imageSet.path = QDir(imageInfo.absolutePath()).filePath(QString("*.%1").arg(imageInfo.suffix()));
  const QDir imageDirectory(imageInfo.absolutePath());
  for (const auto &entry : imageDirectory.entryInfoList({QString("*.%1").arg(imageInfo.suffix())},
                                                        QDir::Files, QDir::Name)) {
    imageSet.resolvedPaths.push_back(entry.absoluteFilePath());
  }
  session.images.push_back(imageSet);
  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    session.cells.push_back({.imageIndex = 0});
  }

  heiran::ui::ViewerGridWidget grid;
  // 描画の費用は塗る画素数で決まる。実際の使い方に合わせて変えられるようにする。
  const int canvasWidth = arguments.size() >= 5 ? std::max(200, arguments.at(4).toInt()) : 1200;
  const int canvasHeight = arguments.size() >= 6 ? std::max(200, arguments.at(5).toInt()) : 800;
  grid.resize(canvasWidth, canvasHeight);
  // 最初の 1 枚が出るまでの時間を測る。開いた直後の体感はここで決まる。
  QElapsedTimer firstLoadTimer;
  firstLoadTimer.start();
  grid.setSession(session);
  grid.show();

  // ウィンドウの生成と最初の読込を 1 つのループで待つ。別々に待つと、窓が出る
  // までの時間が読込の時間に混ざる。窓の生成と GL の初期化は読込とは別の費用。
  const auto cells = grid.findChildren<heiran::ui::ImageViewWidget *>();
  QElapsedTimer waitTimer;
  waitTimer.start();
  qint64 exposedNs = 0;
  qint64 firstLoadNs = 0;
  while (waitTimer.elapsed() < 15000) {
    QApplication::processEvents();
    if (exposedNs == 0 && grid.windowHandle() != nullptr && grid.windowHandle()->isExposed()) {
      exposedNs = firstLoadTimer.nsecsElapsed();
    }

    bool loading = false;
    for (const auto *cell : cells) {
      loading = loading || cell->cellStatus() == heiran::ui::CellStatus::Loading;
    }
    if (firstLoadNs == 0 && !loading) {
      firstLoadNs = firstLoadTimer.nsecsElapsed();
    }
    if (exposedNs != 0 && firstLoadNs != 0) {
      break;
    }
  }

  if (grid.windowHandle() == nullptr || !grid.windowHandle()->isExposed()) {
    out << "the window was not exposed. The measurement would not include drawing." << Qt::endl;
    return 1;
  }

  // 先読みが落ち着くまで待つ。使用量が一定の時間だけ動かなくなったら終わりとみなす。
  QElapsedTimer settleTimer;
  settleTimer.start();
  QElapsedTimer stableTimer;
  stableTimer.start();
  qint64 previousUsedBytes = grid.decodeCacheUsedBytes();
  qint64 settledNs = 0;
  while (settleTimer.elapsed() < 30000) {
    QApplication::processEvents();
    const qint64 usedBytes = grid.decodeCacheUsedBytes();
    if (usedBytes != previousUsedBytes) {
      previousUsedBytes = usedBytes;
      stableTimer.restart();
      continue;
    }
    if (stableTimer.elapsed() > 1500) {
      settledNs = firstLoadTimer.nsecsElapsed();
      break;
    }
  }

  if (cells.isEmpty()) {
    out << "no cells were created" << Qt::endl;
    return 1;
  }
  auto *canvas = cellCanvas(cells.at(0));
  if (canvas == nullptr) {
    out << "the cell canvas was not found" << Qt::endl;
    return 1;
  }

  out << "file       : " << imageInfo.fileName() << "\n";
  out << "pages      : " << imageSet.resolvedPaths.size() << "\n";
  out << "cells      : " << cellCount << "\n";
  out << "canvas     : " << canvas->width() << "x" << canvas->height() << "\n";
  out << "iterations : " << iterations << Qt::endl;

  const QPoint center(canvas->width() / 2, canvas->height() / 2);

  // 計測が描画を含んでいることを確かめる。repaint() が空振りしていると
  // イベント処理だけを測ることになり、数値の意味が変わる。
  auto *openGlCanvas = qobject_cast<QOpenGLWidget *>(canvas);
  if (openGlCanvas == nullptr) {
    out << "the canvas is not a QOpenGLWidget" << Qt::endl;
    return 1;
  }
  const QImage beforeDrag = openGlCanvas->grabFramebuffer();
  sendMouseButton(canvas, QEvent::MouseButtonPress, center);
  sendMouseMove(canvas, center + QPoint(37, 23), Qt::LeftButton);
  sendMouseButton(canvas, QEvent::MouseButtonRelease, center + QPoint(37, 23));
  const QImage afterDrag = openGlCanvas->grabFramebuffer();
  if (beforeDrag.isNull() || afterDrag == beforeDrag) {
    out << "drawing did not change after a pan. The measurement would not include drawing."
        << Qt::endl;
    return 1;
  }

  // hover: 画素値表示とルーペの更新まで含む。
  QVector<qint64> hoverDurations;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const QPoint position = center + QPoint(iteration % 40 - 20, iteration % 30 - 15);
    QElapsedTimer timer;
    timer.start();
    sendMouseMove(canvas, position, Qt::NoButton);
    QApplication::processEvents();
    hoverDurations.push_back(timer.nsecsElapsed());
  }

  // pan: ドラッグ中の 1 ステップ分。
  QVector<qint64> panDurations;
  sendMouseButton(canvas, QEvent::MouseButtonPress, center);
  QPoint panPosition = center;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    panPosition += QPoint(iteration % 2 == 0 ? 3 : -3, iteration % 2 == 0 ? 2 : -2);
    QElapsedTimer timer;
    timer.start();
    sendMouseMove(canvas, panPosition, Qt::LeftButton);
    QApplication::processEvents();
    panDurations.push_back(timer.nsecsElapsed());
  }
  sendMouseButton(canvas, QEvent::MouseButtonRelease, panPosition);

  // zoom: 拡大と縮小を交互に送り、倍率が発散しないようにする。
  QVector<qint64> zoomDurations;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    QElapsedTimer timer;
    timer.start();
    sendWheel(canvas, center, iteration % 2 == 0 ? 120 : -120);
    QApplication::processEvents();
    zoomDurations.push_back(timer.nsecsElapsed());
  }

  // page: ページ送り。往復させて同じページへ戻るため、2 周目からはすべて
  // キャッシュから引けるはず。引けているかを回数で見る。
  QVector<qint64> pageDurations;
  QVector<qint64> pageSwitchDurations;
  QVector<qint64> pageIdleDurations;
  const int totalPages = grid.totalPages();
  if (totalPages > 1) {
    // 1 周目は初回訪問。テクスチャがまだ載っていない状態にあたる。
    // 前へ送り続ける使い方はこちらになるため、別に測る。
    // 画像が実際に出るまでを測る。復号が済んでいないページでは Loading の
    // ままいったん戻るため、processEvents を 1 度呼ぶだけでは待ったことに
    // ならない。体感はこちらで決まる。
    const auto waitForReady = [&cells]() {
      QElapsedTimer waitTimer;
      waitTimer.start();
      while (waitTimer.elapsed() < 10000) {
        QApplication::processEvents();
        bool loading = false;
        for (const auto *cell : cells) {
          loading = loading || cell->cellStatus() == heiran::ui::CellStatus::Loading;
        }
        if (!loading) {
          return;
        }
      }
    };

    QVector<qint64> firstVisitDurations;
    for (int page = 0; page < totalPages; ++page) {
      QElapsedTimer timer;
      timer.start();
      grid.setCurrentPage(page);
      waitForReady();
      firstVisitDurations.push_back(timer.nsecsElapsed());
    }
    reportDurations(out, "page-first", firstVisitDurations);

    grid.resetDecodeCacheCounters();
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const int page = iteration % totalPages;
      QElapsedTimer timer;
      timer.start();
      grid.setCurrentPage(page);
      const qint64 switchNs = timer.nsecsElapsed();
      // 描画はテクスチャの転送を含む。ページ送りの体感はここまでで決まる。
      waitForReady();
      pageDurations.push_back(timer.nsecsElapsed());
      pageSwitchDurations.push_back(switchNs);
      // 何も起きていない状態の processEvents。上との差が描画にあたる。
      QElapsedTimer idleTimer;
      idleTimer.start();
      QApplication::processEvents();
      pageIdleDurations.push_back(idleTimer.nsecsElapsed());
    }
  }

  out << Qt::fixed << qSetRealNumberPrecision(2);
  out << QString("startup: %1 ms to the window, %2 ms to the first image, %3 ms until prefetch "
                 "settled")
             .arg(static_cast<double>(exposedNs) / 1'000'000.0)
             .arg(static_cast<double>(firstLoadNs) / 1'000'000.0)
             .arg(static_cast<double>(settledNs) / 1'000'000.0)
      << Qt::endl;

  // pan-fps: 一定時間パンし続けて、実際に画面へ出たフレームを数える。
  //
  // 1 回あたりの所要時間は「送った処理が返るまで」しか見ていない。表示の
  // なめらかさは、待ち時間や画面への出し方まで含めて実際に何枚出たかで決まる。
  {
    int frameCount = 0;
    QList<QMetaObject::Connection> connections;
    for (auto *cell : cells) {
      auto *cellSurface = qobject_cast<QOpenGLWidget *>(cellCanvas(cell));
      if (cellSurface == nullptr) {
        continue;
      }
      connections.push_back(QObject::connect(cellSurface, &QOpenGLWidget::frameSwapped, cellSurface,
                                             [&frameCount]() { ++frameCount; }));
    }

    QElapsedTimer fpsTimer;
    fpsTimer.start();
    QPoint fpsPosition = center;
    sendMouseButton(canvas, QEvent::MouseButtonPress, fpsPosition);
    int step = 0;
    while (fpsTimer.elapsed() < 2000) {
      // 往復させて画像が枠から出ていかないようにする。
      fpsPosition += QPoint(step % 2 == 0 ? 4 : -4, step % 2 == 0 ? 3 : -3);
      ++step;
      sendMouseMove(canvas, fpsPosition, Qt::LeftButton);
      QApplication::processEvents();
    }
    sendMouseButton(canvas, QEvent::MouseButtonRelease, fpsPosition);
    const double seconds = static_cast<double>(fpsTimer.elapsed()) / 1000.0;

    for (const auto &connection : connections) {
      QObject::disconnect(connection);
    }

    out << Qt::fixed << qSetRealNumberPrecision(1);
    out << QString("pan-fps: %1 frames in %2 s = %3 fps across %4 cell(s)")
               .arg(frameCount)
               .arg(seconds)
               .arg(seconds > 0.0 ? frameCount / seconds : 0.0)
               .arg(cells.size())
        << Qt::endl;
  }

  reportDurations(out, "hover", hoverDurations);
  reportDurations(out, "pan", panDurations);
  reportDurations(out, "zoom", zoomDurations);
  if (!pageDurations.isEmpty()) {
    reportDurations(out, "page", pageDurations);
    reportDurations(out, "page-model", pageSwitchDurations);
    reportDurations(out, "page-idle", pageIdleDurations);
    out << QString("cache  : %1 hit / %2 miss over %3 page changes")
               .arg(grid.decodeCacheHitCount())
               .arg(grid.decodeCacheMissCount())
               .arg(pageDurations.size())
        << Qt::endl;
  }

  // 使用量。decode キャッシュは CPU 側、テクスチャは VRAM 側で別枠になる。
  const auto toMebibytes = [](qint64 bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
  };
  out << Qt::fixed << qSetRealNumberPrecision(1);
  out << QString("cache  : %1 MiB used / %2 MiB budget")
             .arg(toMebibytes(grid.decodeCacheUsedBytes()))
             .arg(toMebibytes(grid.decodeCacheBudgetBytes()))
      << Qt::endl;
  out << QString("texture: %1 MiB drawn / %2 MiB cached across %3 cell(s), mipmaps included")
             .arg(toMebibytes(grid.gpuTextureBytes()))
             .arg(toMebibytes(grid.gpuCachedTextureBytes()))
             .arg(cells.size())
      << Qt::endl;

  return 0;
}
