#include "heiran/ui/SessionIssueDialog.hpp"

#include <QtTest>

using heiran::infra::HrnIssue;
using heiran::infra::HrnIssueSeverity;
using heiran::ui::SessionIssueReport;

class SessionIssueDialogTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void summaryStatesFailureTargetAndNextStep() {
    const SessionIssueReport report{
        .failure = "The .hrn file could not be loaded.",
        .target = "C:/work/session.hrn",
        .nextStep = "Fix the listed settings, or open another file.",
    };

    const QStringList lines = heiran::ui::buildSessionIssueSummary(report).split('\n');
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QString("The .hrn file could not be loaded."));
    QCOMPARE(lines.at(1), QString("Target: C:/work/session.hrn"));
    QCOMPARE(lines.at(2), QString("Next: Fix the listed settings, or open another file."));
  }

  void summaryOmitsEmptyParts() {
    const SessionIssueReport report{.failure = "The session could not be saved."};
    QCOMPARE(heiran::ui::buildSessionIssueSummary(report),
             QString("The session could not be saved."));
  }

  void detailsListEveryIssue() {
    SessionIssueReport report;
    report.issues = {
        {HrnIssueSeverity::Error, "format_version", "A number is required."},
        {HrnIssueSeverity::Warning, "cells[0].rotation", "Using 0."},
    };

    const QStringList lines = heiran::ui::buildSessionIssueDetails(report).split('\n');
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.at(0).startsWith("Error  format_version:"));
    QVERIFY(lines.at(1).startsWith("Warning  cells[0].rotation:"));
  }

  void detailsMentionTruncation() {
    SessionIssueReport report;
    report.issues = {{HrnIssueSeverity::Error, "images[0].name", "Required."}};
    report.truncated = true;

    const QString details = heiran::ui::buildSessionIssueDetails(report);
    QVERIFY(details.contains("only the first"));
    QVERIFY(details.contains(QString::number(heiran::infra::kMaxHrnIssues)));
  }

  void detailsAreEmptyWithoutIssues() {
    QCOMPARE(heiran::ui::buildSessionIssueDetails(SessionIssueReport{}), QString());
  }
};

QTEST_MAIN(SessionIssueDialogTests)

#include "SessionIssueDialogTests.moc"
