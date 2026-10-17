#include "heiran/ui/ImageSetManagerDialog.hpp"

#include "heiran/app/SessionPathResolver.hpp"
#include "heiran/domain/ChannelMode.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <optional>

namespace heiran::ui {

namespace {

constexpr double kMinScale = 0.01;
constexpr double kMaxScale = 100.0;
constexpr double kMinGamma = 0.01;
constexpr double kMaxGamma = 10.0;

QString toDroppedImageSetName(const QString &localPath) {
  const QFileInfo fileInfo(localPath);
  return fileInfo.isDir() ? fileInfo.fileName() : fileInfo.completeBaseName();
}

QString toDroppedImageSetPath(const heiran::domain::SessionModel &session,
                              const QString &localPath) {
  const QFileInfo fileInfo(localPath);
  const QString absolutePath =
      fileInfo.isDir()
          ? QDir(fileInfo.absoluteFilePath()).filePath("*")
          : QDir(fileInfo.absolutePath()).filePath(QString("*.%1").arg(fileInfo.suffix()));
  if (session.sessionDirectory.isEmpty()) {
    return absolutePath;
  }
  return QDir(session.sessionDirectory).relativeFilePath(absolutePath);
}

// 数値の列を、範囲つきの入力欄で編集する。
class IntDelegate : public QStyledItemDelegate {
public:
  IntDelegate(int minimum, int maximum, QObject *parent)
      : QStyledItemDelegate(parent), minimum_(minimum), maximum_(maximum) {}

  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                        const QModelIndex &) const override {
    auto *editor = new QSpinBox(parent);
    editor->setRange(minimum_, maximum_);
    editor->setFrame(false);
    return editor;
  }
  void setEditorData(QWidget *editor, const QModelIndex &index) const override {
    static_cast<QSpinBox *>(editor)->setValue(index.data(Qt::EditRole).toInt());
  }
  void setModelData(QWidget *editor, QAbstractItemModel *model,
                    const QModelIndex &index) const override {
    model->setData(index, static_cast<QSpinBox *>(editor)->value(), Qt::EditRole);
  }

private:
  int minimum_;
  int maximum_;
};

class DoubleDelegate : public QStyledItemDelegate {
public:
  DoubleDelegate(double minimum, double maximum, QObject *parent)
      : QStyledItemDelegate(parent), minimum_(minimum), maximum_(maximum) {}

  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                        const QModelIndex &) const override {
    auto *editor = new QDoubleSpinBox(parent);
    editor->setRange(minimum_, maximum_);
    editor->setDecimals(2);
    editor->setFrame(false);
    return editor;
  }
  void setEditorData(QWidget *editor, const QModelIndex &index) const override {
    static_cast<QDoubleSpinBox *>(editor)->setValue(index.data(Qt::EditRole).toDouble());
  }
  void setModelData(QWidget *editor, QAbstractItemModel *model,
                    const QModelIndex &index) const override {
    model->setData(index, static_cast<QDoubleSpinBox *>(editor)->value(), Qt::EditRole);
  }
  QString displayText(const QVariant &value, const QLocale &) const override {
    return QString::number(value.toDouble(), 'f', 2);
  }

private:
  double minimum_;
  double maximum_;
};

// 選択式の列。選んだ値は Qt::UserRole へ入れる (表示の文字は、受け取った側が付け直す)。
class ChoiceDelegate : public QStyledItemDelegate {
public:
  using Choices = QList<QPair<QString, QVariant>>;
  ChoiceDelegate(std::function<Choices()> choices, QObject *parent)
      : QStyledItemDelegate(parent), choices_(std::move(choices)) {}

  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                        const QModelIndex &) const override {
    auto *editor = new QComboBox(parent);
    for (const auto &choice : choices_()) {
      editor->addItem(choice.first, choice.second);
    }
    editor->setFrame(false);
    return editor;
  }
  void setEditorData(QWidget *editor, const QModelIndex &index) const override {
    auto *combo = static_cast<QComboBox *>(editor);
    combo->setCurrentIndex(std::max(0, combo->findData(index.data(Qt::UserRole))));
  }
  void setModelData(QWidget *editor, QAbstractItemModel *model,
                    const QModelIndex &index) const override {
    model->setData(index, static_cast<QComboBox *>(editor)->currentData(), Qt::UserRole);
  }

private:
  std::function<Choices()> choices_;
};

}  // namespace

class ImageSetManagerDialog::Impl {
public:
  // 画像セットの表の列。
  enum ImageColumn {
    kName,
    kLabel,
    kShow,
    kPath,
    kFiles,
    kOffset,
    kDefaultScale,
    kExif,
    kUsedBy,
    kImageColumnCount
  };
  // セルの表の列。
  enum CellColumn {
    kCell,
    kImageSet,
    kScale,
    kGamma,
    kRotation,
    kFlipX,
    kFlipY,
    kInvert,
    kChannel,
    kSync,
    kCellColumnCount
  };

  Impl(ImageSetManagerDialog *owner, const heiran::domain::SessionModel &session,
       const QVector<heiran::domain::CellConfig> &cells)
      : owner_(owner), session_(session), editedImages_(session.images), editedCells_(cells) {}

  void buildUi() {
    owner_->setWindowTitle(
        QCoreApplication::translate("ImageSetManagerDialog", "Manage Image Sets"));
    owner_->resize(1040, 520);

    auto *root = new QVBoxLayout(owner_);
    tabs_ = new QTabWidget(owner_);
    tabs_->setObjectName("tabs");
    root->addWidget(tabs_, 1);

    buildImageSetTab();
    buildCellTab();

    // 選んだ行は、行全体を、はっきり塗る (どの行を選んでいるかが、遠くからも分かるように)。
    // 欄ごとの細い印 (Windows 11 の選択の印) は、そのスタイルの描き方で、変えない。
    const QColor highlight = owner_->palette().color(QPalette::Highlight);
    const QString tableStyle = QString("QTableView::item:selected { background-color: rgba(%1, %2, "
                                       "%3, 110); color: palette(text); }"
                                       "QTableView { outline: 0; }")
                                   .arg(highlight.red())
                                   .arg(highlight.green())
                                   .arg(highlight.blue());
    imageTable_->setStyleSheet(tableStyle);
    cellTable_->setStyleSheet(tableStyle);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, owner_);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, owner_, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, owner_, &QDialog::reject);

    refreshImageTable();
    refreshCellTable();
    if (!editedImages_.isEmpty()) {
      imageTable_->selectRow(0);
    }
    updateButtons();
  }

  Result result() const { return {.images = editedImages_, .cells = editedCells_}; }

private:
  // ---- 画面 ----

  QPushButton *makeButton(const QString &text, const QString &name, QWidget *parent) {
    auto *button = new QPushButton(text, parent);
    button->setObjectName(name);
    return button;
  }

  void buildImageSetTab() {
    auto *page = new QWidget(tabs_);
    auto *layout = new QVBoxLayout(page);

    imageTable_ = new QTableWidget(page);
    imageTable_->setObjectName("imageSetTable");
    imageTable_->setColumnCount(kImageColumnCount);
    imageTable_->setHorizontalHeaderLabels(
        {QCoreApplication::translate("ImageSetManagerDialog", "Name"),
         QCoreApplication::translate("ImageSetManagerDialog", "Label"),
         QCoreApplication::translate("ImageSetManagerDialog", "Show"),
         QCoreApplication::translate("ImageSetManagerDialog", "Path"),
         QCoreApplication::translate("ImageSetManagerDialog", "Files"),
         QCoreApplication::translate("ImageSetManagerDialog", "Offset"),
         QCoreApplication::translate("ImageSetManagerDialog", "Default scale"), "EXIF",
         QCoreApplication::translate("ImageSetManagerDialog", "Used by")});
    imageTable_->horizontalHeaderItem(kShow)->setToolTip(
        QCoreApplication::translate("ImageSetManagerDialog", "Show the label on the image"));
    imageTable_->horizontalHeaderItem(kFiles)->setToolTip(
        QCoreApplication::translate("ImageSetManagerDialog", "Number of files the path finds"));
    imageTable_->horizontalHeaderItem(kOffset)->setToolTip(QCoreApplication::translate(
        "ImageSetManagerDialog", "Which page the first file belongs to (page offset)"));
    imageTable_->horizontalHeaderItem(kDefaultScale)
        ->setToolTip(QCoreApplication::translate(
            "ImageSetManagerDialog",
            "The scale given to a cell when it takes this image set. Changing it here sets the "
            "scale of every cell that uses the set; each cell's scale can then be edited on the "
            "Cells tab."));
    imageTable_->horizontalHeaderItem(kExif)->setToolTip(
        QCoreApplication::translate("ImageSetManagerDialog", "Honor the EXIF orientation"));
    imageTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    imageTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    imageTable_->verticalHeader()->setVisible(false);
    auto *header = imageTable_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kPath, QHeaderView::Stretch);
    header->setSectionResizeMode(kLabel, QHeaderView::Interactive);
    imageTable_->setColumnWidth(kLabel, 160);
    imageTable_->setItemDelegateForColumn(kOffset, new IntDelegate(-99999, 99999, imageTable_));
    imageTable_->setItemDelegateForColumn(kDefaultScale,
                                          new DoubleDelegate(kMinScale, kMaxScale, imageTable_));
    layout->addWidget(imageTable_, 1);

    resolvedLabel_ = new QLabel(page);
    resolvedLabel_->setObjectName("resolvedLabel");
    resolvedLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(resolvedLabel_);

    auto *row = new QHBoxLayout;
    addButton_ = makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Add"),
                            "addImageSetButton", page);
    duplicateButton_ = makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Duplicate"),
                                  "duplicateImageSetButton", page);
    removeButton_ = makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Remove"),
                               "removeImageSetButton", page);
    moveUpButton_ = makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Up"),
                               "moveImageSetUpButton", page);
    moveDownButton_ = makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Down"),
                                 "moveImageSetDownButton", page);
    browseFileButton_ =
        makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Browse File..."),
                   "browseFileButton", page);
    browseFolderButton_ =
        makeButton(QCoreApplication::translate("ImageSetManagerDialog", "Browse Folder..."),
                   "browseFolderButton", page);
    for (auto *button :
         {addButton_, duplicateButton_, removeButton_, moveUpButton_, moveDownButton_}) {
      row->addWidget(button);
    }
    row->addStretch(1);
    row->addWidget(browseFileButton_);
    row->addWidget(browseFolderButton_);
    layout->addLayout(row);
    tabs_->addTab(page, QCoreApplication::translate("ImageSetManagerDialog", "Image sets"));

    QObject::connect(imageTable_, &QTableWidget::itemChanged, owner_,
                     [this](QTableWidgetItem *item) { onImageItemChanged(item); });
    QObject::connect(imageTable_, &QTableWidget::currentCellChanged, owner_,
                     [this](int, int, int, int) {
                       updateResolvedLabel();
                       updateButtons();
                     });
    QObject::connect(addButton_, &QPushButton::clicked, owner_, [this]() { addImage(); });
    QObject::connect(duplicateButton_, &QPushButton::clicked, owner_,
                     [this]() { duplicateImage(); });
    QObject::connect(removeButton_, &QPushButton::clicked, owner_, [this]() { removeImage(); });
    QObject::connect(moveUpButton_, &QPushButton::clicked, owner_, [this]() { moveImage(-1); });
    QObject::connect(moveDownButton_, &QPushButton::clicked, owner_, [this]() { moveImage(1); });
    QObject::connect(browseFileButton_, &QPushButton::clicked, owner_, [this]() { browseFile(); });
    QObject::connect(browseFolderButton_, &QPushButton::clicked, owner_,
                     [this]() { browseFolder(); });
  }

  void buildCellTab() {
    auto *page = new QWidget(tabs_);
    auto *layout = new QVBoxLayout(page);

    cellTable_ = new QTableWidget(page);
    cellTable_->setObjectName("cellTable");
    cellTable_->setColumnCount(kCellColumnCount);
    cellTable_->setHorizontalHeaderLabels(
        {QCoreApplication::translate("ImageSetManagerDialog", "Cell"),
         QCoreApplication::translate("ImageSetManagerDialog", "Image set"),
         QCoreApplication::translate("ImageSetManagerDialog", "Scale"),
         QCoreApplication::translate("ImageSetManagerDialog", "Gamma"),
         QCoreApplication::translate("ImageSetManagerDialog", "Rotation"),
         QCoreApplication::translate("ImageSetManagerDialog", "Flip X"),
         QCoreApplication::translate("ImageSetManagerDialog", "Flip Y"),
         QCoreApplication::translate("ImageSetManagerDialog", "Invert"),
         QCoreApplication::translate("ImageSetManagerDialog", "Channel"),
         QCoreApplication::translate("ImageSetManagerDialog", "Sync")});
    cellTable_->horizontalHeaderItem(kScale)->setToolTip(QCoreApplication::translate(
        "ImageSetManagerDialog", "Scale of this cell (also set by the image set's default scale)"));
    cellTable_->horizontalHeaderItem(kSync)->setToolTip(QCoreApplication::translate(
        "ImageSetManagerDialog", "Pan and zoom follow the other cells"));
    cellTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cellTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    cellTable_->verticalHeader()->setVisible(false);
    cellTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cellTable_->horizontalHeader()->setSectionResizeMode(kImageSet, QHeaderView::Stretch);
    cellTable_->setItemDelegateForColumn(
        kImageSet, new ChoiceDelegate([this]() { return imageSetChoices(); }, cellTable_));
    cellTable_->setItemDelegateForColumn(kScale,
                                         new DoubleDelegate(kMinScale, kMaxScale, cellTable_));
    cellTable_->setItemDelegateForColumn(kGamma,
                                         new DoubleDelegate(kMinGamma, kMaxGamma, cellTable_));
    cellTable_->setItemDelegateForColumn(
        kRotation, new ChoiceDelegate([]() { return rotationChoices(); }, cellTable_));
    cellTable_->setItemDelegateForColumn(
        kChannel, new ChoiceDelegate([]() { return channelChoices(); }, cellTable_));
    layout->addWidget(cellTable_, 1);

    auto *note = new QLabel(
        QCoreApplication::translate(
            "ImageSetManagerDialog",
            "A cell that shows a difference is set up from its own bar, so it is greyed out here."),
        page);
    note->setWordWrap(true);
    layout->addWidget(note);
    tabs_->addTab(page, QCoreApplication::translate("ImageSetManagerDialog", "Cells"));

    QObject::connect(cellTable_, &QTableWidget::itemChanged, owner_,
                     [this](QTableWidgetItem *item) { onCellItemChanged(item); });
  }

  // ---- 選択肢 ----

  ChoiceDelegate::Choices imageSetChoices() const {
    ChoiceDelegate::Choices choices;
    choices.push_back({QCoreApplication::translate("ImageSetManagerDialog", "(empty)"), -1});
    for (int index = 0; index < editedImages_.size(); ++index) {
      choices.push_back({imageSetText(index), index});
    }
    return choices;
  }

  static ChoiceDelegate::Choices rotationChoices() {
    ChoiceDelegate::Choices choices;
    for (int quarterTurns = 0; quarterTurns < 4; ++quarterTurns) {
      choices.push_back({QString("%1°").arg(quarterTurns * 90), quarterTurns});
    }
    return choices;
  }

  static ChoiceDelegate::Choices channelChoices() {
    using heiran::domain::ChannelMode;
    ChoiceDelegate::Choices choices;
    for (const auto mode : {ChannelMode::Color, ChannelMode::Gray, ChannelMode::Red,
                            ChannelMode::Green, ChannelMode::Blue, ChannelMode::Alpha}) {
      choices.push_back({channelText(mode), static_cast<int>(mode)});
    }
    return choices;
  }

  static QString channelText(heiran::domain::ChannelMode mode) {
    switch (mode) {
      case heiran::domain::ChannelMode::Gray:
        return QCoreApplication::translate("ImageSetManagerDialog", "Gray");
      case heiran::domain::ChannelMode::Red:
        return QCoreApplication::translate("ImageSetManagerDialog", "Red");
      case heiran::domain::ChannelMode::Green:
        return QCoreApplication::translate("ImageSetManagerDialog", "Green");
      case heiran::domain::ChannelMode::Blue:
        return QCoreApplication::translate("ImageSetManagerDialog", "Blue");
      case heiran::domain::ChannelMode::Alpha:
        return QCoreApplication::translate("ImageSetManagerDialog", "Alpha");
      case heiran::domain::ChannelMode::Color:
        break;
    }
    return QCoreApplication::translate("ImageSetManagerDialog", "Color");
  }

  QString imageSetText(int index) const {
    return QString("%1: %2").arg(index + 1).arg(editedImages_.at(index).name);
  }

  // ---- 表の組み立て ----

  static QTableWidgetItem *textItem(const QString &text, bool editable) {
    auto *item = new QTableWidgetItem(text);
    if (!editable) {
      item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
    return item;
  }

  static QTableWidgetItem *checkItem(bool checked, bool enabled = true) {
    auto *item = new QTableWidgetItem;
    Qt::ItemFlags flags = Qt::ItemIsSelectable;
    if (enabled) {
      flags |= Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    }
    item->setFlags(flags);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
  }

  static QTableWidgetItem *numberItem(const QVariant &value) {
    auto *item = new QTableWidgetItem;
    item->setData(Qt::EditRole, value);
    return item;
  }

  void refreshImageTable() {
    const int keepRow = imageTable_->currentRow();
    const QSignalBlocker blocker(imageTable_);
    imageTable_->clearContents();
    imageTable_->setRowCount(editedImages_.size());
    for (int row = 0; row < editedImages_.size(); ++row) {
      const auto &imageSet = editedImages_.at(row);
      imageTable_->setItem(row, kName, textItem(imageSet.name, true));
      imageTable_->setItem(row, kLabel, textItem(imageSet.label, true));
      imageTable_->setItem(row, kShow, checkItem(imageSet.labelVisible));
      imageTable_->setItem(row, kPath, textItem(imageSet.path, true));
      imageTable_->setItem(row, kFiles, textItem(filesText(imageSet.path), false));
      imageTable_->setItem(row, kOffset, numberItem(imageSet.pageOffset));
      imageTable_->setItem(row, kDefaultScale, numberItem(imageSet.scale));
      imageTable_->setItem(row, kExif, checkItem(imageSet.honorExifOrientation));
      imageTable_->setItem(row, kUsedBy, textItem(usedByText(row), false));
    }
    if (keepRow >= 0 && keepRow < editedImages_.size()) {
      imageTable_->setCurrentCell(keepRow, kName);
    }
    updateResolvedLabel();
  }

  void refreshCellTable() {
    const QSignalBlocker blocker(cellTable_);
    cellTable_->clearContents();
    cellTable_->setRowCount(editedCells_.size());
    for (int row = 0; row < editedCells_.size(); ++row) {
      fillCellRow(row);
    }
  }

  void fillCellRow(int row) {
    const auto &cell = editedCells_.at(row);
    const bool isDiff = cell.diff.has_value();
    cellTable_->setItem(
        row, kCell,
        textItem(QCoreApplication::translate("ImageSetManagerDialog", "Cell %1").arg(row + 1),
                 false));

    auto *imageItem = new QTableWidgetItem;
    imageItem->setData(Qt::UserRole, cell.imageIndex);
    imageItem->setText(isDiff
                           ? QCoreApplication::translate("ImageSetManagerDialog", "Diff of Cell %1")
                                 .arg(cell.diff->baseCell + 1)
                           : cellImageSetText(cell.imageIndex));
    cellTable_->setItem(row, kImageSet, imageItem);
    cellTable_->setItem(row, kScale, numberItem(cell.scale));
    cellTable_->setItem(row, kGamma, numberItem(cell.gamma));
    auto *rotationItem = new QTableWidgetItem(QString("%1°").arg(cell.rotation * 90));
    rotationItem->setData(Qt::UserRole, cell.rotation);
    cellTable_->setItem(row, kRotation, rotationItem);
    cellTable_->setItem(row, kFlipX, checkItem(cell.flipX));
    cellTable_->setItem(row, kFlipY, checkItem(cell.flipY));
    cellTable_->setItem(row, kInvert, checkItem(cell.invert));
    auto *channelItem = new QTableWidgetItem(channelText(cell.channel));
    channelItem->setData(Qt::UserRole, static_cast<int>(cell.channel));
    cellTable_->setItem(row, kChannel, channelItem);
    cellTable_->setItem(row, kSync, checkItem(cell.sync));

    if (isDiff) {
      // 差分のセルは、帯から設定する。この表では触らせない。
      for (int column = kImageSet; column < kCellColumnCount; ++column) {
        if (auto *item = cellTable_->item(row, column)) {
          item->setFlags(Qt::ItemIsSelectable);
        }
      }
    }
  }

  QString cellImageSetText(int imageIndex) const {
    if (imageIndex < 0 || imageIndex >= editedImages_.size()) {
      return QCoreApplication::translate("ImageSetManagerDialog", "(empty)");
    }
    return imageSetText(imageIndex);
  }

  QString filesText(const QString &path) const {
    if (path.isEmpty()) {
      return "0";
    }
    return QString::number(
        heiran::app::SessionPathResolver::resolveGlobbedPaths(session_.sessionDirectory, path)
            .size());
  }

  QString usedByText(int imageIndex) const {
    QStringList cells;
    for (int cellIndex = 0; cellIndex < editedCells_.size(); ++cellIndex) {
      if (editedCells_.at(cellIndex).imageIndex == imageIndex && !editedCells_.at(cellIndex).diff) {
        cells.push_back(QString::number(cellIndex + 1));
      }
    }
    return cells.isEmpty()
               ? QString("-")
               : QCoreApplication::translate("ImageSetManagerDialog", "Cell ") + cells.join(", ");
  }

  void updateResolvedLabel() {
    const int row = imageTable_->currentRow();
    if (row < 0 || row >= editedImages_.size()) {
      resolvedLabel_->clear();
      return;
    }
    const QString path = editedImages_.at(row).path;
    const QVector<QString> resolved = path.isEmpty()
                                          ? QVector<QString>{}
                                          : heiran::app::SessionPathResolver::resolveGlobbedPaths(
                                                session_.sessionDirectory, path);
    if (resolved.isEmpty()) {
      resolvedLabel_->setText(
          QCoreApplication::translate("ImageSetManagerDialog", "Resolved: 0 files"));
    } else if (resolved.size() == 1) {
      resolvedLabel_->setText(
          QCoreApplication::translate("ImageSetManagerDialog", "Resolved: 1 file  %1")
              .arg(resolved.first()));
    } else {
      resolvedLabel_->setText(
          QCoreApplication::translate("ImageSetManagerDialog", "Resolved: %1 files  %2 ... %3")
              .arg(resolved.size())
              .arg(resolved.first())
              .arg(resolved.last()));
    }
  }

  void updateButtons() {
    const int row = imageTable_->currentRow();
    const bool hasSelection = row >= 0 && row < editedImages_.size();
    duplicateButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
    browseFileButton_->setEnabled(hasSelection);
    browseFolderButton_->setEnabled(hasSelection);
    moveUpButton_->setEnabled(hasSelection && row > 0);
    moveDownButton_->setEnabled(hasSelection && row < editedImages_.size() - 1);
  }

  // ---- 表の編集を、データへ戻す ----

  void onImageItemChanged(QTableWidgetItem *item) {
    const int row = item->row();
    if (row < 0 || row >= editedImages_.size()) {
      return;
    }
    auto &imageSet = editedImages_[row];
    const QSignalBlocker blocker(imageTable_);
    switch (item->column()) {
      case kName:
        imageSet.name = item->text();
        refreshCellImageSetTexts();
        break;
      case kLabel:
        imageSet.label = item->text();
        break;
      case kShow:
        imageSet.labelVisible = item->checkState() == Qt::Checked;
        break;
      case kPath:
        imageSet.path = item->text();
        imageTable_->item(row, kFiles)->setText(filesText(imageSet.path));
        updateResolvedLabel();
        break;
      case kOffset:
        imageSet.pageOffset = item->data(Qt::EditRole).toInt();
        break;
      case kDefaultScale:
        imageSet.scale = std::clamp(item->data(Qt::EditRole).toDouble(), kMinScale, kMaxScale);
        applyDefaultScaleToCells(row);
        break;
      case kExif:
        imageSet.honorExifOrientation = item->checkState() == Qt::Checked;
        break;
      default:
        break;
    }
  }

  // 画像セットの既定の倍率を変えたら、それを使うセルの倍率も、同じ値にする。
  void applyDefaultScaleToCells(int imageIndex) {
    const QSignalBlocker blocker(cellTable_);
    for (int cellIndex = 0; cellIndex < editedCells_.size(); ++cellIndex) {
      auto &cell = editedCells_[cellIndex];
      if (cell.imageIndex == imageIndex && !cell.diff) {
        cell.scale = editedImages_.at(imageIndex).scale;
        cellTable_->item(cellIndex, kScale)->setData(Qt::EditRole, cell.scale);
      }
    }
  }

  void refreshCellImageSetTexts() {
    const QSignalBlocker blocker(cellTable_);
    for (int cellIndex = 0; cellIndex < editedCells_.size(); ++cellIndex) {
      if (!editedCells_.at(cellIndex).diff) {
        cellTable_->item(cellIndex, kImageSet)
            ->setText(cellImageSetText(editedCells_.at(cellIndex).imageIndex));
      }
    }
  }

  void onCellItemChanged(QTableWidgetItem *item) {
    const int row = item->row();
    if (row < 0 || row >= editedCells_.size() || editedCells_.at(row).diff) {
      return;
    }
    auto &cell = editedCells_[row];
    const QSignalBlocker cellBlocker(cellTable_);
    switch (item->column()) {
      case kImageSet: {
        int imageIndex = item->data(Qt::UserRole).toInt();
        if (imageIndex < 0 || imageIndex >= editedImages_.size()) {
          imageIndex = -1;
        }
        const bool changed = cell.imageIndex != imageIndex;
        cell.imageIndex = imageIndex;
        item->setData(Qt::UserRole, imageIndex);
        item->setText(cellImageSetText(imageIndex));
        if (changed && imageIndex >= 0) {
          // 割り当てるときは、画像セットの既定の倍率が、セルの倍率になる。
          cell.scale = editedImages_.at(imageIndex).scale;
          cellTable_->item(row, kScale)->setData(Qt::EditRole, cell.scale);
        }
        refreshUsedBy();
        break;
      }
      case kScale:
        cell.scale = std::clamp(item->data(Qt::EditRole).toDouble(), kMinScale, kMaxScale);
        break;
      case kGamma:
        cell.gamma = std::clamp(item->data(Qt::EditRole).toDouble(), kMinGamma, kMaxGamma);
        break;
      case kRotation:
        cell.rotation = ((item->data(Qt::UserRole).toInt() % 4) + 4) % 4;
        item->setText(QString("%1°").arg(cell.rotation * 90));
        break;
      case kFlipX:
        cell.flipX = item->checkState() == Qt::Checked;
        break;
      case kFlipY:
        cell.flipY = item->checkState() == Qt::Checked;
        break;
      case kInvert:
        cell.invert = item->checkState() == Qt::Checked;
        break;
      case kChannel: {
        const int value = item->data(Qt::UserRole).toInt();
        cell.channel = static_cast<heiran::domain::ChannelMode>(
            std::clamp(value, 0, static_cast<int>(heiran::domain::ChannelMode::Alpha)));
        item->setText(channelText(cell.channel));
        break;
      }
      case kSync:
        cell.sync = item->checkState() == Qt::Checked;
        break;
      default:
        break;
    }
  }

  void refreshUsedBy() {
    const QSignalBlocker blocker(imageTable_);
    for (int row = 0; row < editedImages_.size(); ++row) {
      imageTable_->item(row, kUsedBy)->setText(usedByText(row));
    }
  }

  // ---- 画像セットの追加、複製、削除、並べ替え ----

  void addImage() {
    const int imageNumber = editedImages_.size() + 1;
    editedImages_.push_back(
        {.name = QCoreApplication::translate("ImageSetManagerDialog", "Image %1").arg(imageNumber),
         .label = QCoreApplication::translate("ImageSetManagerDialog", "Image %1").arg(imageNumber),
         .path = QString(),
         .scale = 1.0,
         .pageOffset = 0,
         .labelVisible = true,
         .honorExifOrientation = true});
    refreshImageTable();
    imageTable_->selectRow(editedImages_.size() - 1);
    imageTable_->setCurrentCell(editedImages_.size() - 1, kName);
    updateButtons();
  }

  void duplicateImage() {
    const int row = imageTable_->currentRow();
    if (row < 0 || row >= editedImages_.size()) {
      return;
    }
    auto duplicatedImage = editedImages_.at(row);
    const QString baseName = duplicatedImage.name.isEmpty()
                                 ? QCoreApplication::translate("ImageSetManagerDialog", "Image")
                                 : duplicatedImage.name;
    QString candidateName =
        QCoreApplication::translate("ImageSetManagerDialog", "%1 Copy").arg(baseName);
    int suffix = 2;
    auto containsName = [&](const QString &name) {
      return std::any_of(editedImages_.begin(), editedImages_.end(),
                         [&](const auto &imageSet) { return imageSet.name == name; });
    };
    while (containsName(candidateName)) {
      candidateName = QCoreApplication::translate("ImageSetManagerDialog", "%1 Copy %2")
                          .arg(baseName)
                          .arg(suffix);
      ++suffix;
    }
    duplicatedImage.name = candidateName;
    if (duplicatedImage.label == editedImages_.at(row).name) {
      duplicatedImage.label = candidateName;
    }

    editedImages_.insert(row + 1, duplicatedImage);
    // 後ろの画像セットの番号が 1 つずれる。セルは、同じ画像セットを指し続ける。
    for (auto &cell : editedCells_) {
      if (cell.imageIndex > row) {
        ++cell.imageIndex;
      }
    }
    refreshImageTable();
    refreshCellTable();
    imageTable_->setCurrentCell(row + 1, kName);
    imageTable_->selectRow(row + 1);
    updateButtons();
  }

  void removeImage() {
    const int row = imageTable_->currentRow();
    if (row < 0 || row >= editedImages_.size()) {
      return;
    }
    editedImages_.removeAt(row);
    for (auto &cell : editedCells_) {
      if (cell.imageIndex == row) {
        cell.imageIndex = -1;
      } else if (cell.imageIndex > row) {
        --cell.imageIndex;
      }
    }
    refreshImageTable();
    refreshCellTable();
    if (!editedImages_.isEmpty()) {
      const int next = std::min(row, static_cast<int>(editedImages_.size()) - 1);
      imageTable_->setCurrentCell(next, kName);
      imageTable_->selectRow(next);
    }
    updateButtons();
  }

  void moveImage(int direction) {
    const int row = imageTable_->currentRow();
    const int target = row + direction;
    if (row < 0 || row >= editedImages_.size() || target < 0 || target >= editedImages_.size()) {
      return;
    }
    editedImages_.swapItemsAt(row, target);
    // セルは、並べ替えても、同じ画像セットを指し続ける (見えている画像が変わらない)。
    for (auto &cell : editedCells_) {
      if (cell.imageIndex == row) {
        cell.imageIndex = target;
      } else if (cell.imageIndex == target) {
        cell.imageIndex = row;
      }
    }
    refreshImageTable();
    refreshCellTable();
    imageTable_->setCurrentCell(target, kName);
    imageTable_->selectRow(target);
    updateButtons();
  }

  // ---- パスの選択 ----

  void applyDroppedPathToCurrent(const QString &localPath) {
    const int row = imageTable_->currentRow();
    if (row < 0 || row >= editedImages_.size()) {
      return;
    }
    heiran::domain::SessionModel tempSession = session_;
    tempSession.images = editedImages_;
    const QString path = toDroppedImageSetPath(tempSession, localPath);
    const QString name = toDroppedImageSetName(localPath);
    // 表の編集と同じ道 (itemChanged) を通して、データへ戻す。
    imageTable_->item(row, kPath)->setText(path);
    if (editedImages_.at(row).name.isEmpty()) {
      imageTable_->item(row, kName)->setText(name);
    }
    if (editedImages_.at(row).label.isEmpty()) {
      imageTable_->item(row, kLabel)->setText(name);
    }
  }

  void browseFile() {
    const QString filePath = QFileDialog::getOpenFileName(
        owner_, QCoreApplication::translate("ImageSetManagerDialog", "Select Image File"),
        QString(), QCoreApplication::translate("ImageSetManagerDialog", "Images (*.*)"));
    if (!filePath.isEmpty()) {
      applyDroppedPathToCurrent(filePath);
    }
  }

  void browseFolder() {
    const QString directoryPath = QFileDialog::getExistingDirectory(
        owner_, QCoreApplication::translate("ImageSetManagerDialog", "Select Image Folder"),
        QString());
    if (!directoryPath.isEmpty()) {
      applyDroppedPathToCurrent(directoryPath);
    }
  }

  ImageSetManagerDialog *owner_ = nullptr;
  heiran::domain::SessionModel session_;
  QVector<heiran::domain::ImageSet> editedImages_;
  QVector<heiran::domain::CellConfig> editedCells_;
  QTabWidget *tabs_ = nullptr;
  QTableWidget *imageTable_ = nullptr;
  QTableWidget *cellTable_ = nullptr;
  QLabel *resolvedLabel_ = nullptr;
  QPushButton *addButton_ = nullptr;
  QPushButton *duplicateButton_ = nullptr;
  QPushButton *removeButton_ = nullptr;
  QPushButton *moveUpButton_ = nullptr;
  QPushButton *moveDownButton_ = nullptr;
  QPushButton *browseFileButton_ = nullptr;
  QPushButton *browseFolderButton_ = nullptr;
};

ImageSetManagerDialog::ImageSetManagerDialog(const heiran::domain::SessionModel &session,
                                             const QVector<heiran::domain::CellConfig> &cells,
                                             QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(this, session, cells)) {
  impl_->buildUi();
}

ImageSetManagerDialog::~ImageSetManagerDialog() = default;

ImageSetManagerDialog::Result ImageSetManagerDialog::result() const {
  return impl_->result();
}

}  // namespace heiran::ui
