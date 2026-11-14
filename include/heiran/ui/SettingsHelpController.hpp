#pragma once

#include <QHash>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QLabel;
class QPlainTextEdit;
class QWidget;
QT_END_NAMESPACE

namespace heiran::ui {

class SettingsHelpController final : public QObject {
public:
  SettingsHelpController(QWidget *dialog, QLabel *title, QPlainTextEdit *description);
  ~SettingsHelpController() override;
  void setHelp(QWidget *widget, const QString &title, const QString &description);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Help {
    QString title;
    QString description;
  };
  QWidget *dialog_;
  QLabel *title_;
  QPlainTextEdit *description_;
  QHash<QWidget *, Help> entries_;
};

}  // namespace heiran::ui
