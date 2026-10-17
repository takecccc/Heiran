#pragma once

#include <QObject>

class QScreen;

#include <memory>

namespace heiran::ui {

// 画面の更新に合わせて合図を出す。
//
// パンのような連続した操作では、描く時刻が画面の更新と揃っていないと、位置の
// 進み方が出るたびにばらつく。**枚数を増やしても消えない。** 実測では 165Hz
// (6.06 ms) に対して約 4 ms 周期で描いており、表示される絵の古さが 0, 2, 4 ms と
// 巡回していた。これが引っかかりとして見える。
//
// Qt に vblank の通知は無い。`swapInterval=1` も DWM の合成下では待たない
// (描き直しの要求から画面へ出るまでが 0.5 ms しかない)。そのため Windows の
// `IDXGIOutput::WaitForVBlank()` を専用のスレッドで待ち、合図だけを UI
// スレッドへ送る。
//
// `DwmFlush()` では駄目だった。**DWM は画面が変わらなければ合成を省くため、
// 戻る間隔が更新レートと一致しない。** 実測では 6.06 ms のところ 8.5-27 ms、
// 最悪 110 ms だった。合成ではなく画面の更新そのものを待つ必要がある。
//
// 合成が止まっている場合など、待てない環境では合図が出ない。呼び側は合図が
// 途絶えても描けるようにしておく。
class DisplayLink final : public QObject {
  Q_OBJECT

public:
  explicit DisplayLink(QObject *parent = nullptr);
  ~DisplayLink() override;

  // アプリで 1 つを共有する。セルの数だけスレッドを立てる必要は無い。
  static DisplayLink &shared();

  // 合図の元にする画面。窓が乗っている画面を渡す。画面ごとに更新レートが
  // 違うため、別の画面のものに合わせても揃わない。
  void setTargetScreen(const QScreen *screen);

  // 合図が出ているか。待てない環境では false のままになる。
  bool isTicking() const;

Q_SIGNALS:
  // 画面の更新 1 回につき 1 度出る。UI スレッドで受ける。
  void ticked();

private:
  class Worker;
  std::unique_ptr<Worker> worker_;
};

}  // namespace heiran::ui
