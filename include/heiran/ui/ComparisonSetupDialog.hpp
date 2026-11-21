#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;

namespace heiran::ui {

class ComparisonSetupDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ComparisonSetupDialog(QWidget *parent = nullptr);
  QStringList paths() const { return paths_; }
  bool comparesFolders() const { return comparesFolders_; }

public Q_SLOTS:
  void accept() override;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void browse(QLineEdit *field, const QLineEdit *other, bool folder);
  void showError(const QString &message, QLineEdit *field);

  QLineEdit *left_;
  QLineEdit *right_;
  QLabel *error_;
  QStringList paths_;
  bool comparesFolders_ = false;
};

}  // namespace heiran::ui
