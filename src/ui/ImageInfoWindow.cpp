#include "heiran/ui/ImageInfoWindow.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace heiran::ui {

ImageInfoWindow::ImageInfoWindow(QWidget *parent)
    : AnalysisWindowBase(QCoreApplication::translate("ImageInfoWindow", "Image Info"), parent) {
  resize(900, 640);

  auto *rootLayout = new QVBoxLayout(this);
  auto *controlLayout = new QHBoxLayout();

  differencesOnlyCheckBox_ =
      new QCheckBox(QCoreApplication::translate("ImageInfoWindow", "Differences only"), this);
  differencesOnlyCheckBox_->setToolTip(QCoreApplication::translate(
      "ImageInfoWindow", "Show only the rows whose values differ between the cells."));
  controlLayout->addWidget(differencesOnlyCheckBox_);

  auto *copyButton = new QPushButton(QCoreApplication::translate("ImageInfoWindow", "Copy"), this);
  copyButton->setToolTip(
      QCoreApplication::translate("ImageInfoWindow", "Copy the table as tab-separated text."));
  controlLayout->addWidget(copyButton);
  controlLayout->addStretch(1);
  rootLayout->addLayout(controlLayout);

  table_ = new QTableWidget(this);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setShowGrid(false);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->setWordWrap(false);
  table_->setTextElideMode(Qt::ElideMiddle);
  rootLayout->addWidget(table_, 1);

  connect(differencesOnlyCheckBox_, &QCheckBox::toggled, this, [this]() { rebuild(); });
  connect(copyButton, &QPushButton::clicked, this, [this]() { copyToClipboard(); });
}

QVector<heiran::infra::MetadataEntry> ImageInfoWindow::metadataFor(const QString &filePath) {
  const QFileInfo fileInfo(filePath);
  const QString key =
      QString("%1|%2").arg(filePath).arg(fileInfo.lastModified().toMSecsSinceEpoch());
  const auto found = metadataCache_.constFind(key);
  if (found != metadataCache_.constEnd()) {
    return found.value();
  }
  // 古い版が溜まり続けないよう、増えすぎたら捨てる。
  if (metadataCache_.size() > 256) {
    metadataCache_.clear();
  }
  const auto metadata = heiran::infra::readFileMetadata(filePath);
  metadataCache_.insert(key, metadata);
  return metadata;
}

void ImageInfoWindow::analysisImagesChanged() {
  cells_.clear();
  const auto &images = analysisImages();
  for (int index = 0; index < images.size(); ++index) {
    const auto &image = images.at(index);
    if (image.image.isNull() || image.filePath.isEmpty()) {
      continue;  // 空のセルは列にしない。
    }
    cells_.push_back(
        describeImage(QCoreApplication::translate("ImageInfoWindow", "Cell %1").arg(index + 1),
                      image.filePath, image.sampler.decodedImage, metadataFor(image.filePath)));
  }
  rebuild();
}

void ImageInfoWindow::rebuild() {
  table_->clear();
  const QVector<ImageInfoRow> allRows = buildImageInfoRows(cells_);
  const bool differencesOnly = differencesOnlyCheckBox_->isChecked() && cells_.size() > 1;

  QVector<ImageInfoRow> rows;
  for (const auto &row : allRows) {
    if (!differencesOnly || row.differs) {
      rows.push_back(row);
    }
  }

  table_->setColumnCount(1 + static_cast<int>(cells_.size()));
  QStringList headers{QCoreApplication::translate("ImageInfoWindow", "Property")};
  for (const auto &cell : std::as_const(cells_)) {
    headers << cell.label;
  }
  table_->setHorizontalHeaderLabels(headers);

  // 分類の行を、分類が変わるところへ挟む。
  int rowCount = 0;
  QString lastGroup;
  for (const auto &row : std::as_const(rows)) {
    rowCount += row.group != lastGroup ? 2 : 1;
    lastGroup = row.group;
  }
  table_->setRowCount(rowCount);

  const AppStyleTokens tokens = AppStyleTokens::current();
  // 違う行が多いと、全面が染まって読みにくい。薄い塗りにする。
  QColor differenceBackground = tokens.accent;
  differenceBackground.setAlpha(26);
  QFont groupFont = table_->font();
  groupFont.setBold(true);

  int tableRow = 0;
  lastGroup.clear();
  bool first = true;
  for (const auto &row : std::as_const(rows)) {
    if (first || row.group != lastGroup) {
      auto *groupItem = new QTableWidgetItem(row.group);
      groupItem->setFont(groupFont);
      groupItem->setForeground(tokens.dimText);
      table_->setItem(tableRow, 0, groupItem);
      table_->setSpan(tableRow, 0, 1, table_->columnCount());
      ++tableRow;
      lastGroup = row.group;
      first = false;
    }
    table_->setItem(tableRow, 0, new QTableWidgetItem(row.key));
    for (int cell = 0; cell < row.values.size(); ++cell) {
      auto *item = new QTableWidgetItem(row.values.at(cell));
      item->setToolTip(row.values.at(cell));
      if (row.differs) {
        item->setBackground(differenceBackground);
      }
      table_->setItem(tableRow, 1 + cell, item);
    }
    ++tableRow;
  }

  table_->resizeColumnToContents(0);
  for (int column = 1; column < table_->columnCount(); ++column) {
    table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
  }
}

QString ImageInfoWindow::tableAsText() const {
  QString text;
  const int columns = table_->columnCount();
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (table_->columnSpan(row, 0) > 1) {
      text += QString("[%1]\n").arg(table_->item(row, 0) != nullptr ? table_->item(row, 0)->text()
                                                                    : QString());
      continue;
    }
    QStringList fields;
    for (int column = 0; column < columns; ++column) {
      const auto *item = table_->item(row, column);
      fields << (item != nullptr ? item->text() : QString());
    }
    text += fields.join('\t') + '\n';
  }
  return text;
}

void ImageInfoWindow::copyToClipboard() const {
  QApplication::clipboard()->setText(tableAsText());
}

}  // namespace heiran::ui
