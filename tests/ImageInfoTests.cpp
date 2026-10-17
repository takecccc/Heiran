// 画像の情報 (ImageInfo、ImageMetadata、ImageInfoWindow)。

#include "heiran/infra/ImageMetadata.hpp"
#include "heiran/ui/ImageInfo.hpp"
#include "heiran/ui/ImageInfoWindow.hpp"

#include <QCheckBox>
#include <QColorSpace>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using heiran::infra::MetadataEntry;
using namespace heiran::ui;

namespace {

ImageInfoCell makeCell(const QString &label, const QVector<MetadataEntry> &entries) {
  return {.label = label, .entries = entries};
}

}  // namespace

class ImageInfoTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // ---- メタデータ ----

  void readsNothingFromAMissingFile() {
    QVERIFY(heiran::infra::readFileMetadata("C:/definitely/missing.jpg").isEmpty());
  }

  // ---- 行の組み立て ----

  // 項目はセルをまたいで 1 行になる。値が違う行に印が付く。
  void marksRowsWhoseValuesDiffer() {
    const auto rows = buildImageInfoRows(
        {makeCell("A", {{"Image", "Dimensions", "100 x 80"}, {"Image", "Decoder", "qt"}}),
         makeCell("B", {{"Image", "Dimensions", "200 x 80"}, {"Image", "Decoder", "qt"}})});

    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).key, QString("Dimensions"));
    QCOMPARE(rows.at(0).values, (QStringList{"100 x 80", "200 x 80"}));
    QVERIFY(rows.at(0).differs);
    QCOMPARE(rows.at(1).key, QString("Decoder"));
    QVERIFY(!rows.at(1).differs);
  }

  // 片方にしか無い項目は、もう一方が "-" で、違いとして扱う。
  void treatsAMissingItemAsADifference() {
    const auto rows =
        buildImageInfoRows({makeCell("A", {{"EXIF", "ISO", "200"}}), makeCell("B", {})});

    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).values, (QStringList{"200", "-"}));
    QVERIFY(rows.at(0).differs);
  }

  // 後のセルだけにある項目も、行になる。
  void includesItemsOnlyTheLaterCellHas() {
    const auto rows = buildImageInfoRows({makeCell("A", {{"Image", "Pixel format", "RGB8"}}),
                                          makeCell("B", {{"EXIF", "Model", "X"}})});

    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).values, (QStringList{"-", "X"}));
  }

  // 分類ごとにまとまる。分類の順は、初めて現れた順。
  void groupsRows() {
    const auto rows = buildImageInfoRows(
        {makeCell("A", {{"File", "Name", "a"}, {"EXIF", "ISO", "100"}, {"File", "Folder", "f"}})});

    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(0).group, QString("File"));
    QCOMPARE(rows.at(1).group, QString("File"));
    QCOMPARE(rows.at(2).group, QString("EXIF"));
  }

  void hasNoRowsForNoCells() { QVERIFY(buildImageInfoRows({}).isEmpty()); }

  // 1 つだけなら、違いは無い。
  void neverMarksADifferenceForASingleCell() {
    const auto rows = buildImageInfoRows({makeCell("A", {{"Image", "Decoder", "qt"}})});
    QVERIFY(!rows.at(0).differs);
  }

  // ---- 色空間 ----

  void describesAColorSpace() {
    QCOMPARE(describeColorSpace(QColorSpace(QColorSpace::SRgb)), QString("sRGB / sRGB curve"));
    QCOMPARE(describeColorSpace(QColorSpace(QColorSpace::DisplayP3)),
             QString("Display P3 / sRGB curve"));
    QVERIFY(describeColorSpace(QColorSpace()).isEmpty());
  }

  // ---- 1 枚の説明 ----

  void describesAnImage() {
    heiran::infra::DecodedImage decoded;
    decoded.displayImage = QImage(40, 30, QImage::Format_RGBA8888);
    decoded.sourceSampleType = heiran::infra::PixelSampleType::UInt16;
    decoded.sourceColorModel = heiran::infra::PixelColorModel::RGBA;
    decoded.decoderBackend = "psd";
    decoded.colorSpace = QColorSpace(QColorSpace::SRgb);
    decoded.subImageKind = heiran::infra::SubImageKind::Frames;
    decoded.subImageCount = 12;

    const auto cell =
        describeImage("Cell 1", "C:/no/such/file.png", decoded, {{"EXIF", "ISO", "400"}});

    const auto rows = buildImageInfoRows({cell});
    const auto valueOf = [&rows](const QString &key) {
      for (const auto &row : rows) {
        if (row.key == key) {
          return row.values.at(0);
        }
      }
      return QString("<none>");
    };
    QCOMPARE(valueOf("Name"), QString("file.png"));
    QCOMPARE(valueOf("Dimensions"), QString("40 \u00D7 30"));
    QCOMPARE(valueOf("Pixel format"), QString("RGBA16"));
    QCOMPARE(valueOf("Decoder"), QString("psd"));
    QCOMPARE(valueOf("Frames"), QString("12"));
    QCOMPARE(valueOf("Color space"), QString("sRGB / sRGB curve"));
    QCOMPARE(valueOf("Embedded profile"), QString("none"));
    QCOMPARE(valueOf("ISO"), QString("400"));
    // ファイルが無ければ、大きさと更新日時は出さない。
    QCOMPARE(valueOf("Size"), QString("<none>"));
    QCOMPARE(valueOf("Modified"), QString("<none>"));
  }

  void reportsTheSizeAndModifiedTimeOfAnExistingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("data.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(2048, 'x'));
    file.close();

    const auto cell = describeImage("Cell 1", path, {}, {});
    QString size;
    for (const auto &entry : cell.entries) {
      if (entry.key == "Size") {
        size = entry.value;
      }
    }
    QCOMPARE(size, QString("2.0 KB (2,048 bytes)"));
  }

  // ---- 窓 ----

  // セルを列にして並べ、分類の行を挟み、値の違う行だけに絞れる。
  void showsTheCellsAsColumnsAndFiltersDifferences() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto makeImage = [&directory](const QString &name, const QSize &size) {
      QImage image(size, QImage::Format_RGBA8888);
      image.fill(Qt::red);
      const QString path = QDir(directory.path()).filePath(name);
      image.save(path);
      AnalysisImage analysisImage;
      analysisImage.label = name;
      analysisImage.image = image;
      analysisImage.filePath = path;
      analysisImage.sampler.decodedImage = {.displayImage = image};
      analysisImage.sampler.sourceSize = image.size();
      return analysisImage;
    };

    ImageInfoWindow window;
    window.show();
    window.setAnalysisImages(1,
                             {makeImage("a.png", QSize(10, 8)), makeImage("b.png", QSize(20, 8))});

    const QString text = window.tableAsText();
    QVERIFY2(text.contains("[File]"), qPrintable(text));
    QVERIFY2(text.contains("[Image]"), qPrintable(text));
    QVERIFY2(text.contains("Dimensions\t10 \u00D7 8\t20 \u00D7 8"), qPrintable(text));
    QVERIFY2(text.contains("Name\ta.png\tb.png"), qPrintable(text));

    // 違う行だけにすると、同じ値の行 (Decoder など) は消える。
    auto *differencesOnly = window.findChild<QCheckBox *>();
    QVERIFY(differencesOnly != nullptr);
    differencesOnly->setChecked(true);
    const QString filtered = window.tableAsText();
    QVERIFY2(filtered.contains("Dimensions"), qPrintable(filtered));
    QVERIFY2(!filtered.contains("Embedded profile"), qPrintable(filtered));
  }

  // 空のセルは列にしない。
  void skipsEmptyCells() {
    ImageInfoWindow window;
    window.show();
    window.setAnalysisImages(1, {AnalysisImage{}, AnalysisImage{}});
    QVERIFY(window.tableAsText().isEmpty());
  }
};

QTEST_MAIN(ImageInfoTests)

#include "ImageInfoTests.moc"
