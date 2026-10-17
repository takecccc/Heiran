#pragma once

#include "heiran/infra/HrnConfigLoader.hpp"

#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace heiran::ui {

// エラー表示の統一書式。何が起きたか、どの対象か、次に何ができるかを揃えて示す。
struct SessionIssueReport {
  // 失敗内容。何が起きたかを 1 文で示す。
  QString failure;
  // 対象。ファイルパスなど、どれに対する失敗かを示す。
  QString target;
  // 次の操作。利用者が取れる行動を示す。
  QString nextStep;
  // 詳細一覧。設定項目単位の問題。
  QVector<heiran::infra::HrnIssue> issues;
  // 上限で打ち切られたか。
  bool truncated = false;
  // 読込を続行できなかったか。別ファイルを開く導線を出すかの判断に使う。
  bool fatal = false;
};

enum class SessionIssueChoice {
  Close,
  OpenAnother,
};

// 要約の文面。失敗内容、対象、次の操作をこの順に並べる。
QString buildSessionIssueSummary(const SessionIssueReport &report);

// 詳細一覧の文面。打ち切られた場合はその旨を末尾に付ける。
QString buildSessionIssueDetails(const SessionIssueReport &report);

// 要約、詳細一覧、固定操作の 3 部構成で表示する。
SessionIssueChoice showSessionIssueDialog(QWidget *parent, const SessionIssueReport &report);

}  // namespace heiran::ui
