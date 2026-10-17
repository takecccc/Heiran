#pragma once

#include <QDialog>
#include <QString>

namespace heiran::ui {

// 埋め込んだライセンスの文章 (LICENSE、NOTICE、THIRD_PARTY_NOTICES.txt) を読む。
// `name` は "LICENSE" / "NOTICE" / "THIRD_PARTY_NOTICES.txt"。無ければ空。
QString licenseText(const QString &name);

// Heiran の版、著作権、ライセンス (Apache-2.0)、依存ライブラリのライセンス表記を見せる。
class AboutDialog : public QDialog {
  Q_OBJECT

public:
  explicit AboutDialog(QWidget *parent = nullptr);
};

}  // namespace heiran::ui
