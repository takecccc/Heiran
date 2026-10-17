// アニメーション PNG (APNG) の復号。
//
// Qt も libspng も 2 枚目以降を読めないため、自前で組む。フレームごとに単独の PNG を
// 組み直して復号し、前のフレームとの合成 (disposal と blend) を順に行う。

#include "MultiFrameFixtures.hpp"
#include "heiran/infra/ApngDecoder.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using heiran::infra::ApngResult;
using heiran::infra::decodeApng;
using heiran::infra::isAnimatedPng;
using heiran::infra::SubImageKind;
using heiran::test::ApngFixtureFrame;
using heiran::test::makeApng;
using heiran::test::writeFile;

namespace {

constexpr qint64 kNoLimit = 1LL << 40;

QImage solid(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  return image;
}

// 見比べやすいよう、画素を RGBA の 4 つ組へ。
QVector<int> rgba(const QImage &image, int x, int y) {
  const QColor color = image.pixelColor(x, y);
  return {color.red(), color.green(), color.blue(), color.alpha()};
}

}  // namespace

class ApngDecodeTests : public QObject {
  Q_OBJECT

private:
  QString apng(const QString &name, const QByteArray &bytes) {
    return writeFile(QDir(directory_.path()), name, bytes);
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(directory_.isValid()); }

  // ---- 見分け ----

  void recognizesAnAnimatedPng() {
    const QString path =
        apng("anim.png", makeApng(4, 4, {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}}));
    QVERIFY(isAnimatedPng(path));
  }

  // 通常の PNG は APNG ではないこと。通常の PNG を開くたびにこの判定を通る。
  void doesNotTakeAnOrdinaryPngForAnimated() {
    const QString path = QDir(directory_.path()).filePath("plain.png");
    QVERIFY(solid(4, 4, Qt::red).save(path));
    QVERIFY(!isAnimatedPng(path));
  }

  void doesNotTakeGarbageOrMissingFilesForAnimated() {
    QVERIFY(!isAnimatedPng(QDir(directory_.path()).filePath("missing.png")));
    const QString garbage = apng("garbage.png", QByteArray("this is not a png at all"));
    QVERIFY(!isAnimatedPng(garbage));
    QVERIFY(!decodeApng(garbage, kNoLimit).ok);
  }

  // ---- フレーム ----

  void decodesEveryFullFrame() {
    const QString path =
        apng("full.png",
             makeApng(4, 3,
                      {{solid(4, 3, Qt::red)}, {solid(4, 3, Qt::green)}, {solid(4, 3, Qt::blue)}}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.frames.size(), 3);
    QCOMPARE(result.frames.at(0).image.size(), QSize(4, 3));
    QCOMPARE(rgba(result.frames.at(0).image, 0, 0), QVector<int>({255, 0, 0, 255}));
    QCOMPARE(rgba(result.frames.at(1).image, 3, 2), QVector<int>({0, 255, 0, 255}));
    QCOMPARE(rgba(result.frames.at(2).image, 1, 1), QVector<int>({0, 0, 255, 255}));
    QVERIFY(!result.truncated);
  }

  // 表示時間は分子 / 分母の秒。分母が 0 なら 100 として扱う (仕様)。
  void readsTheFrameDelays() {
    ApngFixtureFrame first{solid(2, 2, Qt::red)};
    first.delayNumerator = 1;
    first.delayDenominator = 10;  // 100 ms
    ApngFixtureFrame second{solid(2, 2, Qt::green)};
    second.delayNumerator = 25;
    second.delayDenominator = 100;  // 250 ms
    ApngFixtureFrame third{solid(2, 2, Qt::blue)};
    third.delayNumerator = 5;
    third.delayDenominator = 0;  // 5/100 秒 = 50 ms
    const QString path = apng("delay.png", makeApng(2, 2, {first, second, third}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    QCOMPARE(result.frames.at(0).delayMs, 100);
    QCOMPARE(result.frames.at(1).delayMs, 250);
    QCOMPARE(result.frames.at(2).delayMs, 50);
  }

  // ---- 合成 ----

  // 一部だけのフレームを、位置を合わせてキャンバスへ重ねること。
  void placesASubRectangleFrameAtItsOffset() {
    ApngFixtureFrame background{solid(4, 4, Qt::red)};
    ApngFixtureFrame patch{solid(2, 2, Qt::green)};
    patch.x = 1;
    patch.y = 2;
    const QString path = apng("offset.png", makeApng(4, 4, {background, patch}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    const QImage &second = result.frames.at(1).image;
    QCOMPARE(second.size(), QSize(4, 4));
    QCOMPARE(rgba(second, 0, 0), QVector<int>({255, 0, 0, 255}));  // 外は前のまま
    QCOMPARE(rgba(second, 1, 2), QVector<int>({0, 255, 0, 255}));  // 左上
    QCOMPARE(rgba(second, 2, 3), QVector<int>({0, 255, 0, 255}));  // 右下
    QCOMPARE(rgba(second, 3, 3), QVector<int>({255, 0, 0, 255}));  // 外
  }

  // disposal の 3 種類を、1 つの並びで確かめること。
  //   0: 何もしない   1: 背景 (透明) へ戻す   2: 描く前の状態へ戻す
  void appliesTheDisposalOperations() {
    // キャンバス 4x4。全面の赤 → 緑 (1,1)-(2,2) を描いて背景へ戻す → 青 (0,0)-(1,1) を描いて
    // 前の状態へ戻す → 白 (3,3) を描く。
    ApngFixtureFrame red{solid(4, 4, Qt::red)};
    red.disposeOp = 0;
    ApngFixtureFrame green{solid(2, 2, Qt::green)};
    green.x = 1;
    green.y = 1;
    green.disposeOp = 1;
    ApngFixtureFrame blue{solid(2, 2, Qt::blue)};
    blue.disposeOp = 2;
    ApngFixtureFrame white{solid(1, 1, Qt::white)};
    white.x = 3;
    white.y = 3;
    const QString path = apng("dispose.png", makeApng(4, 4, {red, green, blue, white}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.frames.size(), 4);
    // 1 コマ目は赤一色。
    QCOMPARE(rgba(result.frames.at(0).image, 2, 2), QVector<int>({255, 0, 0, 255}));
    // 2 コマ目は赤の上に緑。
    QCOMPARE(rgba(result.frames.at(1).image, 1, 1), QVector<int>({0, 255, 0, 255}));
    // 3 コマ目。緑を背景へ戻したため、緑の跡は透明の穴になる。青は (0,0)-(1,1) を覆う。
    const QImage &third = result.frames.at(2).image;
    QCOMPARE(rgba(third, 2, 2), QVector<int>({0, 0, 0, 0}));      // 穴
    QCOMPARE(rgba(third, 1, 1), QVector<int>({0, 0, 255, 255}));  // 青が穴の一部を覆う
    QCOMPARE(rgba(third, 0, 0), QVector<int>({0, 0, 255, 255}));  // 青
    QCOMPARE(rgba(third, 3, 0), QVector<int>({255, 0, 0, 255}));  // 赤のまま
    // 4 コマ目。青は「前の状態へ戻す」ため消え、赤に戻る。穴は残る。白が (3,3) に載る。
    const QImage &fourth = result.frames.at(3).image;
    QCOMPARE(rgba(fourth, 0, 0), QVector<int>({255, 0, 0, 255}));  // 青が消えて赤
    QCOMPARE(rgba(fourth, 2, 2), QVector<int>({0, 0, 0, 0}));      // 穴は残る
    QCOMPARE(rgba(fourth, 3, 3), QVector<int>({255, 255, 255, 255}));
  }

  // 最初のフレームの「前の状態へ戻す」は、戻す先が無い。背景へ戻す扱いにすること (仕様)。
  void treatsPreviousDisposalOnTheFirstFrameAsBackground() {
    ApngFixtureFrame first{solid(2, 2, Qt::red)};
    first.disposeOp = 2;
    ApngFixtureFrame second{solid(1, 1, Qt::green)};
    const QString path = apng("first-previous.png", makeApng(2, 2, {first, second}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    // 赤は背景へ戻される。2 コマ目の (1,1) は透明のまま (緑は (0,0) にだけ載る)。
    QCOMPARE(rgba(result.frames.at(1).image, 1, 1), QVector<int>({0, 0, 0, 0}));
    QCOMPARE(rgba(result.frames.at(1).image, 0, 0), QVector<int>({0, 255, 0, 255}));
  }

  // blend の「置き換え」は、半透明の画素も下を無視して置き換えること。
  void replacesThePixelsWithBlendSource() {
    ApngFixtureFrame base{solid(2, 2, Qt::red)};
    ApngFixtureFrame overlay{solid(2, 2, QColor(0, 255, 0, 128))};
    overlay.blendOp = 0;
    const QString path = apng("source.png", makeApng(2, 2, {base, overlay}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    QCOMPARE(rgba(result.frames.at(1).image, 0, 0), QVector<int>({0, 255, 0, 128}));
  }

  // blend の「重ねる」は、半透明の画素を下と混ぜること。アルファを掛けない値で混ぜる。
  void blendsTranslucentPixelsOverTheCanvas() {
    ApngFixtureFrame base{solid(2, 2, Qt::red)};
    ApngFixtureFrame overlay{solid(2, 2, QColor(0, 255, 0, 128))};
    overlay.blendOp = 1;
    const QString path = apng("over.png", makeApng(2, 2, {base, overlay}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    const QVector<int> pixel = rgba(result.frames.at(1).image, 0, 0);
    // 不透明の赤の上に 50% の緑。赤と緑がほぼ半分ずつ。アルファは不透明のまま。
    QVERIFY2(std::abs(pixel.at(0) - 127) <= 2 && std::abs(pixel.at(1) - 128) <= 2 &&
                 pixel.at(2) == 0 && pixel.at(3) == 255,
             qPrintable(QString("%1,%2,%3,%4")
                            .arg(pixel.at(0))
                            .arg(pixel.at(1))
                            .arg(pixel.at(2))
                            .arg(pixel.at(3))));
  }

  // 透明な下地の上へ重ねたら、そのままの色になること。0 割りの近くの計算を通る。
  void blendsOverATransparentCanvasWithoutChangingTheColor() {
    ApngFixtureFrame first{solid(2, 2, QColor(0, 0, 0, 0))};
    ApngFixtureFrame overlay{solid(2, 2, QColor(10, 200, 30, 100))};
    overlay.blendOp = 1;
    const QString path = apng("over-transparent.png", makeApng(2, 2, {first, overlay}));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(result.ok);
    QCOMPARE(rgba(result.frames.at(1).image, 0, 0), QVector<int>({10, 200, 30, 100}));
  }

  // ---- 構造 ----

  // 最初の画像データがアニメーションに含まれない場合、それはフレームに数えないこと。
  // 対応していない環境向けの代わりの画像で、2 枚目以降が本当のフレーム。
  void ignoresADefaultImageThatIsNotPartOfTheAnimation() {
    const QImage fallback = solid(4, 4, Qt::yellow);
    const QString path =
        apng("default.png",
             makeApng(4, 4, {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}}, false, fallback));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.frames.size(), 2);
    QCOMPARE(rgba(result.frames.at(0).image, 0, 0), QVector<int>({255, 0, 0, 255}));
    QCOMPARE(rgba(result.frames.at(1).image, 0, 0), QVector<int>({0, 255, 0, 255}));
  }

  // 1 枚だけを求めたら 1 枚だけを読むこと。サムネイルのために全部を読まない。
  void readsOnlyTheFirstFrameWhenAsked() {
    const QString path =
        apng("first-only.png",
             makeApng(2, 2,
                      {{solid(2, 2, Qt::red)}, {solid(2, 2, Qt::green)}, {solid(2, 2, Qt::blue)}}));

    const ApngResult result = decodeApng(path, kNoLimit, true);

    QVERIFY(result.ok);
    QCOMPARE(result.frames.size(), 1);
    QCOMPARE(rgba(result.frames.at(0).image, 0, 0), QVector<int>({255, 0, 0, 255}));
  }

  // メモリの上限を超えるフレームは読まず、打ち切りを知らせること。
  void stopsAtTheMemoryLimit() {
    // 4x4 の RGBA は 1 コマ 64 バイト。130 バイトなら 2 コマ。
    const QString path = apng("limit.png", makeApng(4, 4,
                                                    {{solid(4, 4, Qt::red)},
                                                     {solid(4, 4, Qt::green)},
                                                     {solid(4, 4, Qt::blue)},
                                                     {solid(4, 4, Qt::white)}}));

    const ApngResult result = decodeApng(path, 130);

    QVERIFY(result.ok);
    QCOMPARE(result.frames.size(), 2);
    QVERIFY(result.truncated);
  }

  // 上限が 1 コマにも足りなくても、最初の 1 コマは読むこと。何も出ないより良い。
  void alwaysReadsAtLeastOneFrame() {
    const QString path =
        apng("tiny-limit.png", makeApng(4, 4, {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}}));

    const ApngResult result = decodeApng(path, 1);

    QVERIFY(result.ok);
    QCOMPARE(result.frames.size(), 1);
  }

  // 壊れたファイルで落ちずに、失敗を返すこと。
  void failsForATruncatedFile() {
    QByteArray bytes = makeApng(4, 4, {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}});
    const QString path = apng("truncated.png", bytes.left(bytes.size() / 3));

    const ApngResult result = decodeApng(path, kNoLimit);

    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
  }

  // ---- ImageDecoder を通した結果 ----

  void decodesThroughTheImageDecoderAsFrames() {
    const QString path =
        apng("decoder.png",
             makeApng(4, 4,
                      {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}, {solid(4, 4, Qt::blue)}}));

    const auto result = heiran::infra::ImageDecoder().decode(path, true);

    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const auto &decoded = result.decodedImage;
    QCOMPARE(decoded.subImageKind, SubImageKind::Frames);
    QCOMPARE(decoded.subImageCount, 3);
    QCOMPARE(decoded.decoderBackend, QString("apng"));
    QCOMPARE(heiran::infra::frameOf(decoded, 1).displayImage.pixelColor(0, 0), QColor(0, 255, 0));
    QVERIFY(decoded.colorSpace.isValid());
    // EXIF の向きは適用していないため、適用したと記録しない。
    QCOMPARE(decoded.appliedExifOrientation, heiran::infra::ExifOrientation::Normal);
  }

  // 表示時間が 0 のフレームは 100 ms として扱うこと (GIF / WebP と同じ)。
  void normalizesAZeroDelayLikeTheOtherFormats() {
    ApngFixtureFrame first{solid(2, 2, Qt::red)};
    first.delayNumerator = 0;
    first.delayDenominator = 1;
    ApngFixtureFrame second{solid(2, 2, Qt::green)};
    second.delayNumerator = 1;
    second.delayDenominator = 1000;  // 1 ms
    const QString path = apng("zero.png", makeApng(2, 2, {first, second}));

    const auto result = heiran::infra::ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 0).frameDelayMs, 100);
    QCOMPARE(heiran::infra::frameOf(result.decodedImage, 1).frameDelayMs, 100);
  }

  // 先頭のフレームだけを求めたら、普通の画像として返ること。サムネイルのため。
  // GIF と WebP も同じ。数百フレームを 1 枚のために全部読まない。
  void returnsAPlainImageWhenOnlyTheFirstFrameIsWanted() {
    const QString path =
        apng("thumb.png",
             makeApng(4, 4,
                      {{solid(4, 4, Qt::red)}, {solid(4, 4, Qt::green)}, {solid(4, 4, Qt::blue)}}));
    const QString gif = writeFile(QDir(directory_.path()), "thumb.gif",
                                  heiran::test::makeGif(4, 4, {0, 1, 2}, {20, 20, 20}));

    for (const QString &file : {path, gif}) {
      const auto result = heiran::infra::ImageDecoder().decode(file, true, 0, true);
      QVERIFY2(result.ok, qPrintable(result.errorMessage));
      QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
      QVERIFY(result.decodedImage.frameSequence == nullptr);
      QCOMPARE(result.decodedImage.displayImage.pixelColor(0, 0), QColor(255, 0, 0));
    }
  }

  // 通常の PNG は今までどおり読めること。判定を足しても経路を変えない。
  void leavesAnOrdinaryPngAlone() {
    const QString path = QDir(directory_.path()).filePath("ordinary.png");
    QVERIFY(solid(6, 4, Qt::magenta).save(path));

    const auto result = heiran::infra::ImageDecoder().decode(path, true);

    QVERIFY(result.ok);
    QCOMPARE(result.decodedImage.subImageKind, SubImageKind::None);
    QCOMPARE(result.decodedImage.displayImage.size(), QSize(6, 4));
    QVERIFY(result.decodedImage.decoderBackend != "apng");
  }

private:
  QTemporaryDir directory_;
};

QTEST_APPLESS_MAIN(ApngDecodeTests)

#include "ApngDecodeTests.moc"
