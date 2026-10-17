#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>
#include <QVector>

namespace heiran::infra {

enum class HrnIssueSeverity {
  // 既定値で読み替えて続行できるもの。
  Warning,
  // 読込を失敗として扱うもの。
  Error,
};

// 設定項目単位の問題。location は `cells[1].rotation` のような項目の位置を表す。
struct HrnIssue {
  HrnIssueSeverity severity = HrnIssueSeverity::Error;
  QString location;
  QString detail;

  QString toDisplayText() const;
};

// 集約する問題の上限。これを超えた分は打ち切り、issuesTruncated を立てる。
inline constexpr int kMaxHrnIssues = 20;

struct HrnLoadResult {
  bool ok = false;
  heiran::domain::SessionDocument document;
  // 要約。詳細は issues を参照する。
  QString errorMessage;
  QVector<HrnIssue> issues;
  bool issuesTruncated = false;

  bool hasErrors() const;
};

class HrnConfigLoader {
public:
  HrnLoadResult loadFromFile(const QString &filePath) const;
};

}  // namespace heiran::infra
