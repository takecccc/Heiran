#include "heiran/ui/ImagePrinter.hpp"

#include <QFile>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTemporaryDir>
#include <QtTest>

using heiran::ui::ImagePrinter;

class ImagePrinterTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 縦横比を保って収まり、中央に置かれること。
  void fitsAndCentersTheImage() {
    const QRect page(0, 0, 1000, 1000);

    // 横長。幅で決まり、上下に余白ができる。
    const QRect wide = ImagePrinter::targetRect(QSize(400, 200), page);
    QCOMPARE(wide.size(), QSize(1000, 500));
    QCOMPARE(wide.center().y(), page.center().y());

    // 縦長。高さで決まり、左右に余白ができる。
    const QRect tall = ImagePrinter::targetRect(QSize(200, 400), page);
    QCOMPARE(tall.size(), QSize(500, 1000));
    QCOMPARE(tall.center().x(), page.center().x());
  }

  // 用紙より小さい画像も拡大すること。小さいまま隅に置くと印刷した意味が薄い。
  void enlargesSmallImages() {
    const QRect rect = ImagePrinter::targetRect(QSize(10, 10), QRect(0, 0, 800, 600));
    QCOMPARE(rect.size(), QSize(600, 600));
  }

  void keepsTheAspectRatio() {
    const QSize imageSize(1920, 1080);
    const QRect rect = ImagePrinter::targetRect(imageSize, QRect(10, 20, 700, 900));
    QVERIFY(std::abs(static_cast<double>(rect.width()) / rect.height() -
                     static_cast<double>(imageSize.width()) / imageSize.height()) < 0.01);
    QVERIFY(QRect(10, 20, 700, 900).contains(rect));
  }

  void returnsNothingForEmptyInput() {
    QVERIFY(ImagePrinter::targetRect(QSize(), QRect(0, 0, 100, 100)).isNull());
    QVERIFY(ImagePrinter::targetRect(QSize(10, 10), QRect()).isNull());
  }

  // PDF へ出力できること。実際のプリンタを使わずに、描画の経路を確かめる。
  void printsToAPdfFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdfPath = QDir(directory.path()).filePath("out.pdf");

    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::red);
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdfPath);
    printer.setPageSize(QPageSize(QPageSize::A4));

    QVERIFY(ImagePrinter::print(&printer, image));

    QVERIFY(QFile::exists(pdfPath));
    QVERIFY(QFileInfo(pdfPath).size() > 0);
  }

  // 横長の画像は横向きの用紙になること。縦のままでは用紙の幅で大きさが決まる。
  void choosesTheOrientationFromTheImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QPrinter landscapePrinter(QPrinter::HighResolution);
    landscapePrinter.setOutputFormat(QPrinter::PdfFormat);
    landscapePrinter.setOutputFileName(QDir(directory.path()).filePath("wide.pdf"));
    QImage wide(400, 200, QImage::Format_RGB32);
    wide.fill(Qt::blue);
    QVERIFY(ImagePrinter::print(&landscapePrinter, wide));
    QCOMPARE(landscapePrinter.pageLayout().orientation(), QPageLayout::Landscape);

    QPrinter portraitPrinter(QPrinter::HighResolution);
    portraitPrinter.setOutputFormat(QPrinter::PdfFormat);
    portraitPrinter.setOutputFileName(QDir(directory.path()).filePath("tall.pdf"));
    QImage tall(200, 400, QImage::Format_RGB32);
    tall.fill(Qt::blue);
    QVERIFY(ImagePrinter::print(&portraitPrinter, tall));
    QCOMPARE(portraitPrinter.pageLayout().orientation(), QPageLayout::Portrait);
  }

  void refusesToPrintNothing() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    QVERIFY(!ImagePrinter::print(&printer, QImage()));
    QVERIFY(!ImagePrinter::print(nullptr, QImage(10, 10, QImage::Format_RGB32)));
  }
};

QTEST_MAIN(ImagePrinterTests)

#include "ImagePrinterTests.moc"
