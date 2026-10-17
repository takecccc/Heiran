#include "heiran/ui/ImageSetManagerDialog.hpp"

#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

using heiran::domain::CellConfig;
using heiran::domain::ChannelMode;
using heiran::domain::ImageSet;
using heiran::domain::SessionModel;
using heiran::ui::ImageSetManagerDialog;

namespace {

// 表の列の番号 (ダイアログの内部の並びと同じ)。
constexpr int kName = 0;
constexpr int kLabel = 1;
constexpr int kShow = 2;
constexpr int kPath = 3;
constexpr int kOffset = 5;
constexpr int kDefaultScale = 6;
constexpr int kExif = 7;
constexpr int kUsedBy = 8;

constexpr int kImageSet = 1;
constexpr int kScale = 2;
constexpr int kGamma = 3;
constexpr int kRotation = 4;
constexpr int kFlipX = 5;
constexpr int kChannel = 8;
constexpr int kSync = 9;

ImageSet makeImageSet(const QString &name, double scale = 1.0) {
  ImageSet imageSet;
  imageSet.name = name;
  imageSet.label = name;
  imageSet.path = "no-such-dir/*.png";
  imageSet.scale = scale;
  return imageSet;
}

CellConfig makeCell(int imageIndex, double scale = 1.0) {
  CellConfig cell;
  cell.imageIndex = imageIndex;
  cell.scale = scale;
  return cell;
}

SessionModel makeSession() {
  SessionModel session;
  session.images = {makeImageSet("A"), makeImageSet("B", 0.5)};
  session.cells = {makeCell(0), makeCell(1, 0.5), makeCell(1, 2.0)};
  return session;
}

QTableWidget *imageTable(ImageSetManagerDialog &dialog) {
  return dialog.findChild<QTableWidget *>("imageSetTable");
}
QTableWidget *cellTable(ImageSetManagerDialog &dialog) {
  return dialog.findChild<QTableWidget *>("cellTable");
}
void click(ImageSetManagerDialog &dialog, const char *name) {
  auto *button = dialog.findChild<QPushButton *>(name);
  QVERIFY(button != nullptr);
  QVERIFY(button->isEnabled());
  button->click();
}

}  // namespace

class ImageSetManagerDialogTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 画像セットの 1 行に、名前、ラベル、ラベルの表示、パス、ずれ、既定の倍率、EXIF が並ぶ。
  void listsTheImageSetsInATable() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    auto *table = imageTable(dialog);
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(0, kName)->text(), QString("A"));
    QCOMPARE(table->item(0, kLabel)->text(), QString("A"));
    QCOMPARE(table->item(0, kShow)->checkState(), Qt::Unchecked);
    QCOMPARE(table->item(1, kDefaultScale)->data(Qt::EditRole).toDouble(), 0.5);
    QCOMPARE(table->item(0, kExif)->checkState(), Qt::Checked);
    // 使っているセル。
    QCOMPARE(table->item(0, kUsedBy)->text(), QString("Cell 1"));
    QCOMPARE(table->item(1, kUsedBy)->text(), QString("Cell 2, 3"));
  }

  // 表の編集が、結果へ戻る。
  void writesTheEditsBackToTheResult() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    auto *table = imageTable(dialog);
    table->item(0, kName)->setText("Renamed");
    table->item(0, kLabel)->setText("Shown as {width}");
    table->item(0, kShow)->setCheckState(Qt::Checked);
    table->item(0, kPath)->setText("other/*.png");
    table->item(0, kOffset)->setData(Qt::EditRole, 3);
    table->item(0, kExif)->setCheckState(Qt::Unchecked);

    const auto result = dialog.result();
    QCOMPARE(result.images.at(0).name, QString("Renamed"));
    QCOMPARE(result.images.at(0).label, QString("Shown as {width}"));
    QVERIFY(result.images.at(0).labelVisible);
    QCOMPARE(result.images.at(0).path, QString("other/*.png"));
    QCOMPARE(result.images.at(0).pageOffset, 3);
    QVERIFY(!result.images.at(0).honorExifOrientation);
    // 触っていない行は、そのまま。
    QCOMPARE(result.images.at(1).name, QString("B"));
  }

  // 既定の倍率を変えると、その画像セットを使うセルの倍率も、同じ値になる (表にも出る)。
  void changingTheDefaultScaleUpdatesTheCellsThatUseIt() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    imageTable(dialog)->item(1, kDefaultScale)->setData(Qt::EditRole, 3.0);

    const auto result = dialog.result();
    QCOMPARE(result.images.at(1).scale, 3.0);
    QCOMPARE(result.cells.at(0).scale, 1.0);  // 別の画像セットのセル
    QCOMPARE(result.cells.at(1).scale, 3.0);
    QCOMPARE(result.cells.at(2).scale, 3.0);
    QCOMPARE(cellTable(dialog)->item(1, kScale)->data(Qt::EditRole).toDouble(), 3.0);
  }

  // セルごとの倍率や、ガンマ、回転、反転、チャネル、同期を、セルの表で変えられる。
  void editsEachCellInTheCellTable() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    auto *table = cellTable(dialog);
    QCOMPARE(table->rowCount(), 3);

    table->item(2, kScale)->setData(Qt::EditRole, 1.5);
    table->item(2, kGamma)->setData(Qt::EditRole, 2.2);
    table->item(2, kRotation)->setData(Qt::UserRole, 3);
    table->item(2, kFlipX)->setCheckState(Qt::Checked);
    table->item(2, kChannel)->setData(Qt::UserRole, static_cast<int>(ChannelMode::Green));
    table->item(2, kSync)->setCheckState(Qt::Unchecked);

    const auto cell = dialog.result().cells.at(2);
    QCOMPARE(cell.scale, 1.5);
    QCOMPARE(cell.gamma, 2.2);
    QCOMPARE(cell.rotation, 3);
    QVERIFY(cell.flipX);
    QCOMPARE(cell.channel, ChannelMode::Green);
    QVERIFY(!cell.sync);
    // 表示の文字も付け直される。
    QCOMPARE(table->item(2, kRotation)->text(), QString("270°"));
    QCOMPARE(table->item(2, kChannel)->text(), QString("Green"));
    // ほかのセルは、そのまま。
    QCOMPARE(dialog.result().cells.at(1).scale, 0.5);
  }

  // セルへ別の画像セットを割り当てると、倍率は、その画像セットの既定の倍率になる。使用中の表示も変わる。
  void assigningAnImageSetTakesItsDefaultScale() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    // Cell 1 (A、倍率 1.0) へ、B (既定 0.5) を割り当てる。
    cellTable(dialog)->item(0, kImageSet)->setData(Qt::UserRole, 1);

    const auto result = dialog.result();
    QCOMPARE(result.cells.at(0).imageIndex, 1);
    QCOMPARE(result.cells.at(0).scale, 0.5);
    QCOMPARE(cellTable(dialog)->item(0, kImageSet)->text(), QString("2: B"));
    QCOMPARE(imageTable(dialog)->item(0, kUsedBy)->text(), QString("-"));
    QCOMPARE(imageTable(dialog)->item(1, kUsedBy)->text(), QString("Cell 1, 2, 3"));

    // 空へ戻すと、倍率は触らない。
    cellTable(dialog)->item(0, kImageSet)->setData(Qt::UserRole, -1);
    QCOMPARE(dialog.result().cells.at(0).imageIndex, -1);
    QCOMPARE(dialog.result().cells.at(0).scale, 0.5);
    QCOMPARE(cellTable(dialog)->item(0, kImageSet)->text(), QString("(empty)"));
  }

  // 名前を変えると、セルの表の画像セットの表示も変わる。
  void renamingAnImageSetUpdatesTheCellTable() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    imageTable(dialog)->item(1, kName)->setText("Second");
    QCOMPARE(cellTable(dialog)->item(1, kImageSet)->text(), QString("2: Second"));
  }

  // 範囲外の値は、範囲へ収める。
  void clampsOutOfRangeValues() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    cellTable(dialog)->item(0, kScale)->setData(Qt::EditRole, 1000.0);
    cellTable(dialog)->item(0, kGamma)->setData(Qt::EditRole, 0.0);
    QCOMPARE(dialog.result().cells.at(0).scale, 100.0);
    QCOMPARE(dialog.result().cells.at(0).gamma, 0.01);
  }

  // 追加、複製、削除。削除では、使っていたセルが空になり、後ろの画像セットを指すセルは、番号を詰める。
  void addsDuplicatesAndRemovesImageSets() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    click(dialog, "addImageSetButton");
    QCOMPARE(dialog.result().images.size(), 3);
    QCOMPARE(imageTable(dialog)->rowCount(), 3);
    QCOMPARE(imageTable(dialog)->currentRow(), 2);

    // A を複製 (A の直後へ入る)。B を指すセルは、番号が 1 つ後ろへずれて、同じ B を指し続ける。
    imageTable(dialog)->setCurrentCell(0, kName);
    click(dialog, "duplicateImageSetButton");
    auto result = dialog.result();
    QCOMPARE(result.images.at(1).name, QString("A Copy"));
    QCOMPARE(result.images.at(2).name, QString("B"));
    QCOMPARE(result.cells.at(1).imageIndex, 2);

    // A Copy を削除。B を指すセルは、番号が戻る。
    imageTable(dialog)->setCurrentCell(1, kName);
    click(dialog, "removeImageSetButton");
    result = dialog.result();
    QCOMPARE(result.images.size(), 3);
    QCOMPARE(result.cells.at(1).imageIndex, 1);

    // A を削除。A を使っていた Cell 1 は空になり、B を指すセルは、番号が詰まる。
    imageTable(dialog)->setCurrentCell(0, kName);
    click(dialog, "removeImageSetButton");
    result = dialog.result();
    QCOMPARE(result.cells.at(0).imageIndex, -1);
    QCOMPARE(result.cells.at(1).imageIndex, 0);
    QCOMPARE(result.images.at(0).name, QString("B"));
  }

  // 並べ替えても、セルは同じ画像セットを指し続ける。
  void movingAnImageSetKeepsTheCellsOnTheSameSet() {
    const SessionModel session = makeSession();
    ImageSetManagerDialog dialog(session, session.cells);
    imageTable(dialog)->setCurrentCell(0, kName);
    click(dialog, "moveImageSetDownButton");

    const auto result = dialog.result();
    QCOMPARE(result.images.at(0).name, QString("B"));
    QCOMPARE(result.images.at(1).name, QString("A"));
    // Cell 1 は A、Cell 2、3 は B のまま。
    QCOMPARE(result.cells.at(0).imageIndex, 1);
    QCOMPARE(result.cells.at(1).imageIndex, 0);
    QCOMPARE(result.cells.at(2).imageIndex, 0);
    QCOMPARE(imageTable(dialog)->currentRow(), 1);
  }

  // 差分のセルは、表で触らせない。値は、そのまま結果へ残る。
  void keepsDifferenceCellsUntouched() {
    SessionModel session = makeSession();
    CellConfig diffCell;
    diffCell.diff = heiran::domain::CellDiff{.baseCell = 0, .targetCell = 1};
    session.cells.push_back(diffCell);
    ImageSetManagerDialog dialog(session, session.cells);
    auto *table = cellTable(dialog);
    QCOMPARE(table->rowCount(), 4);
    QVERIFY(!(table->item(3, kScale)->flags() & Qt::ItemIsEditable));
    QCOMPARE(table->item(3, kImageSet)->text(), QString("Diff of Cell 1"));
    // 差分のセルの行を、データの経路で書き換えようとしても、変わらない。
    table->item(3, kScale)->setData(Qt::EditRole, 9.0);
    QVERIFY(dialog.result().cells.at(3).diff.has_value());
    QCOMPARE(dialog.result().cells.at(3).scale, 1.0);
    // 差分のセルは、「使っているセル」に数えない。
    QCOMPARE(imageTable(dialog)->item(0, kUsedBy)->text(), QString("Cell 1"));
  }
};

QTEST_MAIN(ImageSetManagerDialogTests)
#include "ImageSetManagerDialogTests.moc"
