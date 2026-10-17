#pragma once

#include <QString>

namespace heiran::ui {

enum class CellStatus {
  // 画像を表示している。
  Ready,
  // 表示する画像が無い。空欄セルとして中立に表示する。
  Empty,
  // 読込中。
  Loading,
  // 読込に失敗した。空欄セルと見た目を分け、再読込の導線を出す。
  Failed,
};

// セルの状態と、その理由。
struct CellStatusInfo {
  CellStatus status = CellStatus::Empty;
  // Empty と Failed の簡潔な理由。
  QString reason;
  // Failed のときの対象ファイル名。
  QString fileName;

  static CellStatusInfo ready() { return {.status = CellStatus::Ready}; }
  static CellStatusInfo empty(const QString &reason) {
    return {.status = CellStatus::Empty, .reason = reason};
  }
  static CellStatusInfo loading() { return {.status = CellStatus::Loading}; }
  static CellStatusInfo failed(const QString &fileName, const QString &reason) {
    return {.status = CellStatus::Failed, .reason = reason, .fileName = fileName};
  }
};

// セル内に表示する見出し。読込失敗と空欄で語を分ける。
QString cellStatusHeadline(const CellStatusInfo &statusInfo);

}  // namespace heiran::ui
