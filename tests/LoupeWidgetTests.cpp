#include "heiran/ui/LoupeWidget.hpp"
#include "heiran/ui/PixelInfoText.hpp"
#include "heiran/ui/PixelInfoWidget.hpp"

#include <QImage>
#include <QtTest>

namespace {

heiran::ui::LoupePreview makePreview(const QImage &image, QPointF position,
                                     heiran::ui::ChannelMode channelMode) {
  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();

  return {.image = image,
          .imagePosition = position,
          .channelMode = channelMode,
          .valid = true,
          .sampler = sampler};
}

QImage renderWidget(QWidget *widget) {
  QImage rendered(widget->size(), QImage::Format_ARGB32);
  rendered.fill(Qt::black);
  widget->render(&rendered);
  return rendered;
}

}  // namespace

class LoupeWidgetTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  // 何も渡していない間は案内を出すこと。落ちないことも兼ねる。
  void drawsAPlaceholderWithoutAPreview() {
    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    const QImage rendered = renderWidget(&widget);
    QVERIFY(!rendered.isNull());
  }

  void compositesTransparentPixelsOnTheConfiguredBackground() {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    widget.setPreview(makePreview(image, QPointF(8.5, 8.5), heiran::ui::ChannelMode::Color));
    heiran::ui::TransparencyBackground background{.solidColor = QColor(23, 67, 101),
                                                  .checkerColor1 = QColor(40, 90, 130),
                                                  .checkerColor2 = QColor(170, 200, 230)};
    for (const auto pattern :
         {heiran::ui::TransparencyPattern::Solid, heiran::ui::TransparencyPattern::Checkerboard}) {
      background.pattern = pattern;
      widget.setTransparencyBackground(background);
      const QImage rendered = renderWidget(&widget);
      int firstCount = 0;
      int secondCount = 0;
      for (int y = 20; y < 280; ++y) {
        for (int x = 20; x < 280; ++x) {
          const QColor pixel = rendered.pixelColor(x, y);
          firstCount += pixel == (pattern == heiran::ui::TransparencyPattern::Solid
                                      ? background.solidColor
                                      : background.checkerColor1);
          secondCount += pixel == background.checkerColor2;
        }
      }
      QVERIFY(firstCount > 100);
      if (pattern == heiran::ui::TransparencyPattern::Checkerboard) {
        QVERIFY(secondCount > 100);
      }
      QCOMPARE(rendered.pixelColor(0, 0), QColor(24, 26, 30));
    }
  }

  // 中心の升目に、指した画素の色が塗られること。
  // 塗りは表示用画像から取るため、渡した色がそのまま出る。
  void fillsTheCenterCellWithTheHoveredColor() {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(QColor(20, 30, 40));
    image.setPixelColor(8, 8, QColor(200, 60, 90));

    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    widget.setPreview(makePreview(image, QPointF(8.5, 8.5), heiran::ui::ChannelMode::Color));

    const QImage rendered = renderWidget(&widget);

    // 升目の中には値のテキストと白い枠線も描かれる。中心の 1 点だけを見ると
    // そちらを拾うため、中心の升目を走査して塗りの色が現れることを確かめる。
    const int cellSize = std::max(16, std::min(rendered.width(), rendered.height()) / 7);
    const QRect centerCell(rendered.width() / 2 - cellSize / 2 + 4,
                           rendered.height() / 2 - cellSize / 2 + 4, cellSize - 8, cellSize - 8);
    int matchingPixels = 0;
    for (int y = centerCell.top(); y <= centerCell.bottom(); ++y) {
      for (int x = centerCell.left(); x <= centerCell.right(); ++x) {
        const QColor color = rendered.pixelColor(x, y);
        if (std::abs(color.red() - 200) <= 2 && std::abs(color.green() - 60) <= 2 &&
            std::abs(color.blue() - 90) <= 2) {
          ++matchingPixels;
        }
      }
    }
    QVERIFY2(matchingPixels > 0, "the hovered colour was not painted into the centre cell");

    // 周りの升目は下地の色になる。中心だけが違う色であること。
    const QColor neighbour =
        rendered.pixelColor(rendered.width() / 2 - cellSize, rendered.height() / 2 - cellSize);
    QVERIFY2(std::abs(neighbour.red() - 200) > 2 || std::abs(neighbour.green() - 60) > 2,
             qPrintable(QString("neighbour=(%1,%2,%3)")
                            .arg(neighbour.red())
                            .arg(neighbour.green())
                            .arg(neighbour.blue())));
  }

  // 画像の外を指しても落ちないこと。端をなぞると必ず外へ出る。
  void handlesPositionsOutsideTheImage() {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::green);

    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    for (const QPointF position :
         {QPointF(0.5, 0.5), QPointF(7.5, 7.5), QPointF(-4.0, -4.0), QPointF(20.0, 20.0)}) {
      widget.setPreview(makePreview(image, position, heiran::ui::ChannelMode::Color));
      QVERIFY(!renderWidget(&widget).isNull());
    }
  }

  // どの表示チャネルでも描けること。輝度は色空間の重みを通る経路になる。
  void drawsEveryChannelMode() {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(QColor(200, 120, 40));

    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    for (const auto channelMode : {heiran::ui::ChannelMode::Color, heiran::ui::ChannelMode::Gray,
                                   heiran::ui::ChannelMode::Red, heiran::ui::ChannelMode::Green,
                                   heiran::ui::ChannelMode::Blue, heiran::ui::ChannelMode::Alpha}) {
      widget.setPreview(makePreview(image, QPointF(8.5, 8.5), channelMode));
      QVERIFY(!renderWidget(&widget).isNull());
    }
  }

  // 単位を変えても描けること。表示は数字だけが変わる。
  void acceptsBothPixelValueFormats() {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(QColor(128, 128, 128));

    heiran::ui::LoupeWidget widget;
    widget.resize(300, 300);
    widget.setPreview(makePreview(image, QPointF(8.5, 8.5), heiran::ui::ChannelMode::Color));

    widget.setPixelValueFormat(heiran::ui::PixelValueFormat::Normalized255);
    const QImage asBytes = renderWidget(&widget);
    widget.setPixelValueFormat(heiran::ui::PixelValueFormat::Normalized01);
    const QImage asUnit = renderWidget(&widget);

    QVERIFY(!asBytes.isNull() && !asUnit.isNull());
    // 数字の描き方が変わるため、同じにはならない。
    QVERIFY(asBytes != asUnit);
  }

  // 画素値の行を並べられること。
  void drawsPixelInfoLines() {
    heiran::ui::PixelInfoWidget widget;
    widget.resize(400, 120);

    const QImage empty = renderWidget(&widget);
    widget.setLines({"[ 1] (   0,    0): R= 1 G= 2 B= 3"});
    const QImage filled = renderWidget(&widget);

    QVERIFY(!empty.isNull() && !filled.isNull());
    QVERIFY(empty != filled);
  }
};

QTEST_MAIN(LoupeWidgetTests)

#include "LoupeWidgetTests.moc"
