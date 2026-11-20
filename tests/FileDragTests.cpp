#include "heiran/ui/FileDragController.hpp"
#include "heiran/ui/ImageViewWidget.hpp"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>
#include <QtTest>

namespace {

class RecordingFileDragController final : public heiran::ui::FileDragController {
public:
  using FileDragController::FileDragController;
  QList<QUrl> urls;
  Qt::DropActions actions;
  int count = 0;

protected:
  void executeDrag(QDrag &drag, Qt::DropActions supported) override {
    urls = drag.mimeData()->urls();
    actions = supported;
    ++count;
  }
};

void sendMouse(QWidget &widget, QEvent::Type type, QPoint position, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::ControlModifier) {
  QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons, modifiers);
  QApplication::sendEvent(&widget, &event);
}

}  // namespace

class FileDragTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void exportsOneOriginalFileAfterThreshold() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QString::fromUtf8("画像 #1.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original file contents");
    file.close();
    QWidget widget;
    RecordingFileDragController controller(&widget);
    controller.setSourceFile(path);
    sendMouse(widget, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, {11, 10}, Qt::NoButton, Qt::LeftButton);
    QCOMPARE(controller.count, 0);
    const QPoint end(10 + QApplication::startDragDistance(), 10);
    sendMouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    QCOMPARE(controller.count, 1);
    QCOMPARE(controller.urls, QList<QUrl>{QUrl::fromLocalFile(path)});
    QCOMPARE(controller.urls.first().toLocalFile(), path);
    QCOMPARE(controller.actions, Qt::DropActions(Qt::CopyAction));
    sendMouse(widget, QEvent::MouseMove, end + QPoint(10, 0), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(controller.count, 1);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original file contents"));
  }

  void cancelsPendingDrag() {
    QTemporaryDir directory;
    const QString path = directory.filePath("image.png");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QWidget widget;
    RecordingFileDragController controller(&widget);
    controller.setSourceFile(path);
    const QPoint end(20 + QApplication::startDragDistance(), 20);
    for (const auto type : {QEvent::MouseButtonRelease, QEvent::UngrabMouse, QEvent::Hide}) {
      sendMouse(widget, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton);
      if (type == QEvent::MouseButtonRelease) {
        sendMouse(widget, type, {10, 10}, Qt::LeftButton, Qt::NoButton);
      } else {
        QEvent event(type);
        QApplication::sendEvent(&widget, &event);
      }
      sendMouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
      QCOMPARE(controller.count, 0);
    }
    sendMouse(widget, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton);
    controller.setSourceFile(directory.filePath("next.png"));
    sendMouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    QCOMPARE(controller.count, 0);
    controller.setSourceFile(path);
    sendMouse(widget, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(file.remove());
    sendMouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    QCOMPARE(controller.count, 0);
    for (const auto &invalid : {QString(), path, directory.path()}) {
      controller.setSourceFile(invalid);
      sendMouse(widget, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton);
      sendMouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
      QCOMPARE(controller.count, 0);
    }
  }

  void preservesPanAndSelection() {
    heiran::ui::ImageViewWidget view;
    heiran::infra::DecodedImage image;
    image.displayImage = QImage(32, 32, QImage::Format_RGBA8888);
    image.displayImage.fill(Qt::red);
    view.setDecodedImage(image, QString(), heiran::ui::CellStatusInfo::ready());
    auto *canvas = view.findChild<QWidget *>("imageCanvas");
    QVERIFY(canvas);
    QSignalSpy pan(&view, &heiran::ui::ImageViewWidget::panRequested);
    QSignalSpy selection(&view, &heiran::ui::ImageViewWidget::selectionRequested);
    for (const auto modifiers : {Qt::NoModifier, Qt::ShiftModifier, Qt::ControlModifier}) {
      sendMouse(*canvas, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton,
                modifiers);
      sendMouse(*canvas, QEvent::MouseMove, {30, 20}, Qt::NoButton, Qt::LeftButton, modifiers);
      sendMouse(*canvas, QEvent::MouseButtonRelease, {30, 20}, Qt::LeftButton, Qt::NoButton,
                modifiers);
      if (modifiers == Qt::NoModifier) {
        QCOMPARE(pan.count(), 1);
        QCOMPARE(selection.count(), 0);
      } else if (modifiers == Qt::ShiftModifier) {
        QCOMPARE(pan.count(), 1);
        QCOMPARE(selection.count(), 2);
        QVERIFY(!selection.first().at(3).toBool());
        QVERIFY(selection.last().at(3).toBool());
      } else {
        QCOMPARE(pan.count(), 1);
        QCOMPARE(selection.count(), 2);
      }
    }
  }
};

QTEST_MAIN(FileDragTests)
#include "FileDragTests.moc"
