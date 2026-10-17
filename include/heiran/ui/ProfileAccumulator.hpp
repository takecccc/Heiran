#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <initializer_list>
#include <optional>

namespace heiran::ui {

// 区間の所要時間をためて、一定数ごとに平均と最大を `qDebug()` へ出す。
//
// 計測を入れた箇所ごとに「合計を持つ構造体 + 平均を出す record()」を書いていたが、
// 5 か所でほぼ同じ形になっていたためまとめた。有効かどうかは環境変数で決まる。
//
// **最大と最小も出す。** 平均だけを見てパンの引っかかりを追ったが、たまに起きる
// ことは平均に埋もれて見えなかった。1 回だけ大きく遅れたかどうかは最大にしか
// 出ない。最小は、平均が本当の周期なのか、周期の 2 倍を拾っているだけなのかを
// 見分けるために要る。
//
// 出力は次の形になる。
//   [HeiranProfile] <label> avg_<f>=<ms> min_<f>=<ms> max_<f>=<ms> ... samples=<n>
class ProfileAccumulator {
public:
  ProfileAccumulator(QString environmentVariable, QString label, QStringList fieldNames,
                     int reportEvery);

  // 無効なら計測そのものを省ける。`QElapsedTimer` を回す前に見る。
  bool enabled() const;

  // `fieldNames` と同じ数、同じ順序で渡す。数が合わない場合は何もしない。
  void record(std::initializer_list<qint64> durationsNs);

private:
  QString environmentVariable_;
  QString label_;
  QStringList fieldNames_;
  int reportEvery_ = 1;
  QVector<qint64> totalsNs_;
  QVector<qint64> minimumsNs_;
  QVector<qint64> maximumsNs_;
  int count_ = 0;
  // 環境変数の読み出しは 1 度だけにする。
  mutable std::optional<bool> enabled_;
};

}  // namespace heiran::ui
