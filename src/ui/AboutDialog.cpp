#include "heiran/ui/AboutDialog.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace heiran::ui {

namespace {

#ifndef HEIRAN_VERSION_STRING
#define HEIRAN_VERSION_STRING "0"
#endif

QPlainTextEdit *makeTextView(const QString &text, QWidget *parent) {
  auto *view = new QPlainTextEdit(parent);
  view->setReadOnly(true);
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  view->setPlainText(text);
  view->moveCursor(QTextCursor::Start);
  return view;
}

}  // namespace

QString licenseText(const QString &name) {
  QFile file(QStringLiteral(":/licenses/") + name);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("About Heiran"));
  resize(760, 560);

  auto *layout = new QVBoxLayout(this);
  auto *title = new QLabel(
      tr("<h2>Heiran %1</h2>"
         "<p>An image viewer that shows several images in sync.<br>"
         "Copyright 2026 Atelier Takec. Licensed under the Apache License, Version 2.0.</p>"
         "<p>Heiran uses Qt (LGPL-3.0) and other open source libraries. Their licenses "
         "are in the tabs below.</p>")
          .arg(QStringLiteral(HEIRAN_VERSION_STRING)),
      this);
  title->setWordWrap(true);
  title->setTextFormat(Qt::RichText);
  layout->addWidget(title);

  auto *tabs = new QTabWidget(this);
  tabs->setObjectName("licenseTabs");
  tabs->addTab(makeTextView(licenseText("LICENSE"), tabs), tr("License"));
  tabs->addTab(makeTextView(licenseText("NOTICE"), tabs), tr("Notice"));
  tabs->addTab(makeTextView(licenseText("THIRD_PARTY_NOTICES.txt"), tabs),
               tr("Third-Party Licenses"));
  layout->addWidget(tabs, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

}  // namespace heiran::ui
