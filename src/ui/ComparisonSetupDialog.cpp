#include "heiran/ui/ComparisonSetupDialog.hpp"

#include "heiran/app/FolderComparison.hpp"
#include "heiran/ui/ActionIcons.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace heiran::ui {
namespace {

QString targetPath(const QLineEdit *field) {
  QString path = field->text().trimmed();
  if (path.size() >= 2 && path.startsWith(u'"') && path.endsWith(u'"')) {
    path = path.sliced(1, path.size() - 2);
  }
  return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString browseDirectory(const QLineEdit *field) {
  const QFileInfo entry(targetPath(field));
  if (entry.isDir()) {
    return entry.absoluteFilePath();
  }
  return entry.isFile() ? entry.absolutePath() : QString();
}

bool isImage(const QString &path) {
  return heiran::app::hasComparisonImageSuffix(path) || QImageReader(path).canRead();
}

}  // namespace

ComparisonSetupDialog::ComparisonSetupDialog(QWidget *parent)
    : QDialog(parent), left_(new QLineEdit(this)), right_(new QLineEdit(this)),
      error_(new QLabel(this)) {
  setObjectName("comparisonSetup");
  setWindowTitle(tr("Compare Files or Folders"));
  auto *layout = new QVBoxLayout(this);
  const auto addTarget = [this, layout](QLineEdit *field, const QLineEdit *other,
                                        const QString &label) {
    auto *group = new QGroupBox(label, this);
    auto *groupLayout = new QVBoxLayout(group);
    field->setClearButtonEnabled(true);
    field->setAccessibleName(label);
    field->setPlaceholderText(tr("Enter a path or drop a file or folder"));
    groupLayout->addWidget(field);
    for (auto *target : {static_cast<QWidget *>(group), static_cast<QWidget *>(field)}) {
      target->setAcceptDrops(true);
      target->installEventFilter(this);
    }
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *file = new QPushButton(themedIcon("image"), tr("Choose File..."), group);
    auto *folder = new QPushButton(themedIcon("folder-open"), tr("Choose Folder..."), group);
    file->setAutoDefault(false);
    folder->setAutoDefault(false);
    row->addWidget(file);
    row->addWidget(folder);
    groupLayout->addLayout(row);
    layout->addWidget(group);
    connect(file, &QPushButton::clicked, this,
            [this, field, other]() { browse(field, other, false); });
    connect(folder, &QPushButton::clicked, this,
            [this, field, other]() { browse(field, other, true); });
  };
  left_->setObjectName("comparisonFirstPath");
  right_->setObjectName("comparisonSecondPath");
  addTarget(left_, right_, tr("First file or folder"));
  addTarget(right_, left_, tr("Second file or folder"));

  error_->setObjectName("comparisonSetupError");
  error_->setTextFormat(Qt::PlainText);
  error_->setWordWrap(true);
  error_->hide();
  layout->addWidget(error_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto *compare = buttons->addButton(tr("Compare"), QDialogButtonBox::AcceptRole);
  compare->setObjectName("startComparison");
  compare->setDefault(true);
  compare->setEnabled(false);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &ComparisonSetupDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  const auto update = [this, compare]() {
    error_->hide();
    compare->setEnabled(!left_->text().trimmed().isEmpty() && !right_->text().trimmed().isEmpty());
  };
  connect(left_, &QLineEdit::textChanged, this, update);
  connect(right_, &QLineEdit::textChanged, this, update);
  resize(720, sizeHint().height());
  left_->setFocus();
}

bool ComparisonSetupDialog::eventFilter(QObject *watched, QEvent *event) {
  const auto type = event->type();
  if (type != QEvent::DragEnter && type != QEvent::DragMove && type != QEvent::Drop) {
    return QDialog::eventFilter(watched, event);
  }
  auto *drop = static_cast<QDropEvent *>(event);
  if (!drop->mimeData()->hasUrls()) {
    return QDialog::eventFilter(watched, event);
  }
  const auto urls = drop->mimeData()->urls();
  if (urls.isEmpty() || urls.size() > 2) {
    drop->ignore();
    return true;
  }
  QStringList paths;
  for (const auto &url : urls) {
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
      drop->ignore();
      return true;
    }
    paths.append(QDir::toNativeSeparators(url.toLocalFile()));
  }
  const auto actions = drop->possibleActions();
  if (actions.testFlag(Qt::CopyAction)) {
    drop->setDropAction(Qt::CopyAction);
  } else if (actions.testFlag(Qt::LinkAction)) {
    drop->setDropAction(Qt::LinkAction);
  } else {
    drop->ignore();
    return true;
  }
  if (type == QEvent::Drop) {
    if (paths.size() == 2) {
      left_->setText(paths.at(0));
      right_->setText(paths.at(1));
    } else {
      auto *field = watched == left_ || watched == left_->parentWidget() ? left_ : right_;
      field->setText(paths.first());
    }
  }
  drop->accept();
  return true;
}

void ComparisonSetupDialog::accept() {
  const QFileInfo first(targetPath(left_));
  const QFileInfo second(targetPath(right_));
  if (!first.isFile() && !first.isDir()) {
    showError(tr("The first file or folder does not exist."), left_);
    return;
  }
  if (!second.isFile() && !second.isDir()) {
    showError(tr("The second file or folder does not exist."), right_);
    return;
  }
  if (first.isDir() != second.isDir()) {
    showError(tr("Choose two image files or two folders."), right_);
    return;
  }
  if (first.isFile() && (!isImage(first.filePath()) || !isImage(second.filePath()))) {
    showError(tr("Choose two supported image files."), right_);
    return;
  }
  paths_ = {first.filePath(), second.filePath()};
  comparesFolders_ = first.isDir();
  QDialog::accept();
}

void ComparisonSetupDialog::showError(const QString &message, QLineEdit *field) {
  error_->setText(message);
  error_->show();
  field->setFocus();
  field->selectAll();
}

void ComparisonSetupDialog::browse(QLineEdit *field, const QLineEdit *other, bool folder) {
  QString start = browseDirectory(field);
  if (start.isEmpty()) {
    start = browseDirectory(other);
  }
  const QString selected = folder
                               ? QFileDialog::getExistingDirectory(this, tr("Choose Folder"), start)
                               : QFileDialog::getOpenFileName(this, tr("Choose Image File"), start);
  if (!selected.isEmpty()) {
    field->setText(QDir::toNativeSeparators(selected));
  }
}

}  // namespace heiran::ui
