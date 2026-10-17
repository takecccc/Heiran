#include "heiran/ui/DisplayLink.hpp"

#include "heiran/ui/ProfileAccumulator.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMutex>
#include <QScreen>
#include <QThread>

#include <atomic>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// DXGI のヘッダは windows.h の後に置く必要がある。
#include <dxgi.h>
#include <wrl/client.h>
#endif

namespace heiran::ui {

namespace {

// 待てているかどうかの境目。待てていない場合は即座に戻る。
constexpr qint64 kMinimumWaitNs = 200'000;

// この回数だけ続けて即座に戻ったら、待てていないとみなして間を置く。
// 数回なら画面構成の切り替わりで起こり得るため、少し余裕を持たせる。
constexpr int kImmediateReturnLimit = 60;

// 待てなかったときに置く間。空回りで 1 コアを焼かないための値。
//
// **諦めて抜けてはいけない。** 画面の抜き差しで一時的に待てなくなることがあり、
// 抜けるとその後ずっと合図が出ない。実際にテストで 0 回/秒になった。
constexpr unsigned long kRetryDelayMs = 100;

// 画面の更新を待って戻る間隔。合図を出す側の刻み。
// UI スレッドで受け取る間隔と突き合わせると、取りこぼしの場所が分かる。
ProfileAccumulator &vblankProfiler() {
  static ProfileAccumulator profiler("HEIRAN_PROFILE_PAN", "vblank", {"interval"}, 60);
  return profiler;
}

}  // namespace

class DisplayLink::Worker final : public QThread {
public:
  explicit Worker(DisplayLink *owner) : owner_(owner) {}

  void stop() {
    stopping_ = true;
    // 画面の更新 1 回より長くは待たない。すぐ戻る。
    wait();
  }

  bool isTicking() const { return ticking_; }

  // 合図の元にする画面。窓が別の画面へ移った場合に呼ぶ。
  void setTargetGeometry(const QRect &geometry) {
    const QMutexLocker locker(&targetMutex_);
    if (targetGeometry_ == geometry) {
      return;
    }
    targetGeometry_ = geometry;
    targetChanged_ = true;
  }

protected:
  void run() override {
#if defined(Q_OS_WIN)
    using Microsoft::WRL::ComPtr;

    // DXGI は COM を使う。初期化せずに触ると、畳むときの後始末が不定になる。
    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(comStatus);

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      if (comInitialized) {
        CoUninitialize();
      }
      ticking_ = false;
      return;
    }

    ComPtr<IDXGIOutput> output;
    int immediateReturns = 0;
    QElapsedTimer waitTimer;

    while (!stopping_) {
      if (output == nullptr || takeTargetChanged()) {
        output = selectOutput(factory.Get());
      }
      if (output == nullptr) {
        ticking_ = false;
        msleep(kRetryDelayMs);
        continue;
      }

      waitTimer.restart();
      if (FAILED(output->WaitForVBlank())) {
        // 画面構成が変わると無効になる。選び直して続ける。
        ticking_ = false;
        output.Reset();
        msleep(kRetryDelayMs);
        continue;
      }

      if (waitTimer.nsecsElapsed() < kMinimumWaitNs) {
        // 待てていない。合図を出すと刻みにならないため、出さずに数える。
        ticking_ = false;
        if (++immediateReturns >= kImmediateReturnLimit) {
          immediateReturns = 0;
          output.Reset();
          msleep(kRetryDelayMs);
        }
        continue;
      }

      immediateReturns = 0;
      ticking_ = true;
      if (tickTimer_.isValid()) {
        const qint64 intervalNs = tickTimer_.nsecsElapsed();
        if (intervalNs < 500'000'000) {
          vblankProfiler().record({intervalNs});
        }
      }
      tickTimer_.restart();
      Q_EMIT owner_->ticked();
    }

    // COM の解放より先に手放す。順序を任せると後始末で固まることがある。
    output.Reset();
    factory.Reset();
    if (comInitialized) {
      CoUninitialize();
    }
#endif
    ticking_ = false;
  }

private:
#if defined(Q_OS_WIN)
  bool takeTargetChanged() {
    const QMutexLocker locker(&targetMutex_);
    const bool changed = targetChanged_;
    targetChanged_ = false;
    return changed;
  }

  QRect targetGeometry() const {
    const QMutexLocker locker(&targetMutex_);
    return targetGeometry_;
  }

  // 対象の画面を含む出力を選ぶ。画面ごとに更新レートが違うため、
  // 窓が乗っている画面のものでなければ意味が無い。
  Microsoft::WRL::ComPtr<IDXGIOutput> selectOutput(IDXGIFactory1 *factory) const {
    using Microsoft::WRL::ComPtr;

    const QRect target = targetGeometry();
    const QPoint targetCenter = target.center();

    ComPtr<IDXGIOutput> firstOutput;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0;
         factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++adapterIndex, adapter.Reset()) {
      ComPtr<IDXGIOutput> output;
      for (UINT outputIndex = 0; adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND;
           ++outputIndex, output.Reset()) {
        DXGI_OUTPUT_DESC description{};
        if (FAILED(output->GetDesc(&description))) {
          continue;
        }
        if (firstOutput == nullptr) {
          firstOutput = output;
        }
        const RECT &bounds = description.DesktopCoordinates;
        if (target.isEmpty() ||
            (targetCenter.x() >= bounds.left && targetCenter.x() < bounds.right &&
             targetCenter.y() >= bounds.top && targetCenter.y() < bounds.bottom)) {
          return output;
        }
      }
    }
    // 見つからない場合でも、どれかの更新に合わせておく方が何も出さないよりよい。
    return firstOutput;
  }
#endif

  DisplayLink *owner_;
  QElapsedTimer tickTimer_;
  mutable QMutex targetMutex_;
  QRect targetGeometry_;
  bool targetChanged_ = false;
  std::atomic_bool stopping_{false};
  std::atomic_bool ticking_{false};
};

DisplayLink::DisplayLink(QObject *parent) : QObject(parent), worker_(new Worker(this)) {
  if (const QScreen *primaryScreen = QGuiApplication::primaryScreen()) {
    worker_->setTargetGeometry(primaryScreen->geometry());
  }
  worker_->start();
}

DisplayLink::~DisplayLink() {
  worker_->stop();
}

DisplayLink &DisplayLink::shared() {
  static DisplayLink link;
  // `QCoreApplication` の後始末の中で止める。
  //
  // 関数内 static の後始末は `main()` を抜けた後に走る。そこまで待つと、Qt が
  // 畳まれた後にスレッドを畳むことになり、**実際にテストが 30 分以上固まった。**
  // `aboutToQuit` では足りない。`QTest` のように事象の輪を回さない使い方では
  // 出ないためこちらを使う。
  static const bool stopRegistered = [] {
    qAddPostRoutine([] { shared().worker_->stop(); });
    return true;
  }();
  Q_UNUSED(stopRegistered);
  return link;
}

void DisplayLink::setTargetScreen(const QScreen *screen) {
  worker_->setTargetGeometry(screen != nullptr ? screen->geometry() : QRect());
}

bool DisplayLink::isTicking() const {
  return worker_->isTicking();
}

}  // namespace heiran::ui
