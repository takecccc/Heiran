#include "heiran/ui/SessionIssueDialog.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace heiran::ui {

QString buildSessionIssueSummary(const SessionIssueReport &report) {
  QStringList lines;
  lines << report.failure;
  if (!report.target.isEmpty()) {
    lines << QCoreApplication::translate("SessionIssueDialog", "Target: %1").arg(report.target);
  }
  if (!report.nextStep.isEmpty()) {
    lines << QCoreApplication::translate("SessionIssueDialog", "Next: %1").arg(report.nextStep);
  }
  return lines.join("\n");
}

QString buildSessionIssueDetails(const SessionIssueReport &report) {
  QStringList lines;
  lines.reserve(report.issues.size() + 1);
  for (const auto &issue : report.issues) {
    lines << issue.toDisplayText();
  }
  if (report.truncated) {
    lines << QCoreApplication::translate("SessionIssueDialog",
                                         "... only the first %1 issues are listed.")
                 .arg(heiran::infra::kMaxHrnIssues);
  }
  return lines.join("\n");
}

SessionIssueChoice showSessionIssueDialog(QWidget *parent, const SessionIssueReport &report) {
  QDialog dialog(parent);
  dialog.setWindowTitle(
      report.fatal ? QCoreApplication::translate("SessionIssueDialog", "Could not open the session")
                   : QCoreApplication::translate("SessionIssueDialog", "Session warnings"));
  dialog.setMinimumWidth(560);

  auto *layout = new QVBoxLayout(&dialog);

  auto *summaryLabel = new QLabel(buildSessionIssueSummary(report), &dialog);
  summaryLabel->setWordWrap(true);
  summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(summaryLabel);

  if (!report.issues.isEmpty()) {
    auto *detailView = new QPlainTextEdit(buildSessionIssueDetails(report), &dialog);
    detailView->setReadOnly(true);
    detailView->setMinimumHeight(180);
    layout->addWidget(detailView);
  }

  auto *buttonBox = new QDialogButtonBox(&dialog);
  QPushButton *openAnotherButton = nullptr;
  if (report.fatal) {
    openAnotherButton =
        buttonBox->addButton(QCoreApplication::translate("SessionIssueDialog", "Open Another..."),
                             QDialogButtonBox::ActionRole);
  }
  auto *closeButton = buttonBox->addButton(QDialogButtonBox::Close);
  closeButton->setDefault(true);
  layout->addWidget(buttonBox);

  SessionIssueChoice choice = SessionIssueChoice::Close;
  QObject::connect(buttonBox, &QDialogButtonBox::clicked, &dialog,
                   [&choice, openAnotherButton, &dialog](QAbstractButton *button) {
                     if (openAnotherButton != nullptr && button == openAnotherButton) {
                       choice = SessionIssueChoice::OpenAnother;
                     }
                     dialog.accept();
                   });

  dialog.exec();
  return choice;
}

}  // namespace heiran::ui
