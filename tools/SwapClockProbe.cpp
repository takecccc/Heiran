// `frameSwapped` が画面の更新に同期するかを、窓の種類ごとに測る。
//
// `QOpenGLWidget` は FBO へ描いて Qt が合成するため `SwapBuffers` を呼ばない。
// `QOpenGLWindow` は自前の swapchain を持つため呼ぶ。`swapInterval=1` が効くのは
// 後者だけのはずだが、DWM の合成下でどうなるかは測らないと分からない。
//
// `frameSwapped` が来たらすぐ次を要求する。何かが待たせていれば、その間隔が出る。

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QOpenGLWindow>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// 集めた間隔をまとめて出す。
void report(const char *label, const std::vector<qint64> &intervalsNs, double refreshRate,
            int paintCount) {
  if (intervalsNs.empty()) {
    std::printf("%-18s no swaps\n", label);
    return;
  }

  std::vector<qint64> sorted = intervalsNs;
  std::sort(sorted.begin(), sorted.end());
  const double toMs = 1.0 / 1'000'000.0;
  const double medianMs = static_cast<double>(sorted.at(sorted.size() / 2)) * toMs;
  const double minMs = static_cast<double>(sorted.front()) * toMs;
  const double maxMs = static_cast<double>(sorted.back()) * toMs;
  const double vblankMs = refreshRate > 1.0 ? 1000.0 / refreshRate : 0.0;

  std::printf(
      "%-18s swaps=%zu paints=%d  median=%.3f ms  min=%.3f ms  max=%.3f ms  (vblank=%.3f ms)  %s\n",
      label, sorted.size(), paintCount, medianMs, minMs, maxMs, vblankMs,
      // 更新間隔の 8 割を超えていれば待たされている。
      medianMs > vblankMs * 0.8 ? "SYNCED" : "free-running");
}

// 窓が出るまで待つ。出る前に測ると立ち上がりが混ざる。
void waitUntilShown(int settleMs = 500) {
  QEventLoop loop;
  QTimer::singleShot(settleMs, &loop, &QEventLoop::quit);
  loop.exec();
}

// `frameSwapped` が来たら次を要求し続け、間隔を集める。
template <typename Surface> std::vector<qint64> measure(Surface *surface, int durationMs) {
  std::vector<qint64> intervalsNs;
  intervalsNs.reserve(4096);
  QElapsedTimer intervalTimer;

  QObject::connect(surface, &Surface::frameSwapped, surface, [&]() {
    if (intervalTimer.isValid()) {
      intervalsNs.push_back(intervalTimer.nsecsElapsed());
    }
    intervalTimer.restart();
    surface->update();
  });

  surface->update();
  QEventLoop loop;
  QTimer::singleShot(durationMs, &loop, &QEventLoop::quit);
  loop.exec();
  return intervalsNs;
}

// 中身は問わない。毎回描き直すことだけが要る。
class ProbeWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  int paintCount() const { return frame_; }

protected:
  void initializeGL() override { initializeOpenGLFunctions(); }
  void paintGL() override {
    ++frame_;
    glClearColor((frame_ % 60) / 60.0F, 0.2F, 0.4F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
  }

private:
  int frame_ = 0;
};

class ProbeWindow final : public QOpenGLWindow, protected QOpenGLFunctions {
public:
  int paintCount() const { return frame_; }

protected:
  void initializeGL() override { initializeOpenGLFunctions(); }
  void paintGL() override {
    ++frame_;
    glClearColor((frame_ % 60) / 60.0F, 0.4F, 0.2F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
  }

private:
  int frame_ = 0;
};

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  const int durationMs = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt() : 3000;

  const QScreen *primaryScreen = QGuiApplication::primaryScreen();
  const double refreshRate = primaryScreen != nullptr ? primaryScreen->refreshRate() : 0.0;
  std::printf("screen refresh = %.3f Hz\n", refreshRate);

  {
    // 実アプリと同じ形。他の widget を伴う窓の中へ置く。
    QWidget host;
    host.resize(800, 600);
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *probe = new ProbeWidget;
    layout->addWidget(probe);
    host.show();
    waitUntilShown();
    std::printf("  (QOpenGLWidget swapInterval=%d)\n", probe->format().swapInterval());
    const int paintsBefore = probe->paintCount();
    const auto intervals = measure(probe, durationMs);
    report("QOpenGLWidget", intervals, refreshRate, probe->paintCount() - paintsBefore);
  }

  {
    ProbeWindow probe;
    probe.resize(800, 600);
    probe.show();
    waitUntilShown();
    std::printf("  (QOpenGLWindow swapInterval=%d)\n", probe.format().swapInterval());
    const int paintsBefore = probe.paintCount();
    const auto intervals = measure(&probe, durationMs);
    report("QOpenGLWindow", intervals, refreshRate, probe.paintCount() - paintsBefore);
  }

  {
    // 全画面なら DWM を通さず直接出す経路 (independent flip) に乗れることがある。
    // 乗れていれば、ここだけ画面の更新間隔に張り付く。
    ProbeWindow probe;
    QSurfaceFormat requestedFormat = probe.format();
    requestedFormat.setSwapInterval(1);
    probe.setFormat(requestedFormat);
    probe.showFullScreen();
    waitUntilShown();
    const int paintsBefore = probe.paintCount();
    const auto intervals = measure(&probe, durationMs);
    report("QOpenGLWindow(full)", intervals, refreshRate, probe.paintCount() - paintsBefore);
  }

  return 0;
}
