#include "heiran/ui/DecodeCache.hpp"

#include <QImage>
#include <QtTest>

namespace {

constexpr int kImageSide = 100;
constexpr qint64 kImageBytes = static_cast<qint64>(kImageSide) * kImageSide * 4;

heiran::infra::DecodeResult makeResult() {
  QImage image(kImageSide, kImageSide, QImage::Format_RGBA8888);
  image.fill(Qt::black);
  return {.ok = true, .decodedImage = {.displayImage = image}};
}

// 16bit 画像のように、表示用画像とは別に解析用バッファを持つ結果。
heiran::infra::DecodeResult makeResultWithAnalysisBuffer() {
  auto result = makeResult();

  heiran::infra::PixelBuffer buffer;
  buffer.width = kImageSide;
  buffer.height = kImageSide;
  buffer.channelCount = 3;
  buffer.sampleType = heiran::infra::PixelSampleType::UInt16;
  buffer.colorModel = heiran::infra::PixelColorModel::RGB;
  buffer.bytesPerRow = static_cast<qsizetype>(kImageSide) * 3 * 2;
  buffer.storage = QSharedPointer<QByteArray>::create(buffer.bytesPerRow * kImageSide, '\0');

  result.decodedImage.analysisBuffer = buffer;
  result.decodedImage.sourceSampleType = heiran::infra::PixelSampleType::UInt16;
  result.decodedImage.sourceColorModel = heiran::infra::PixelColorModel::RGB;
  result.decodedImage.sourceChannelCount = 3;
  return result;
}

// 全フレームを持つアニメーションの結果。
heiran::infra::DecodeResult makeAnimationResult(int frameCount) {
  auto sequence = QSharedPointer<heiran::infra::FrameSequence>::create();
  for (int index = 0; index < frameCount; ++index) {
    sequence->frames.push_back(makeResult().decodedImage);
  }
  auto result = makeResult();
  result.decodedImage.subImageKind = heiran::infra::SubImageKind::Frames;
  result.decodedImage.subImageCount = frameCount;
  result.decodedImage.frameSequence = sequence;
  return result;
}

}  // namespace

class DecodeCacheTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void countsDisplayImageBytes() {
    QCOMPARE(heiran::ui::DecodeCache::entryBytes(makeResult()), kImageBytes);
  }

  // アニメーションは全フレーム分を数えること。先頭のフレームだけ数えると、数百コマの
  // GIF が予算を超えて居座る。先頭のフレームは結果そのものと同じ内容で、二重に数えない。
  void countsEveryFrameOfAnAnimation() {
    QCOMPARE(heiran::ui::DecodeCache::entryBytes(makeAnimationResult(4)), kImageBytes * 4);
    QCOMPARE(heiran::ui::DecodeCache::entryBytes(makeAnimationResult(1)), kImageBytes);
  }

  void evictsAnAnimationByItsFullSize() {
    heiran::ui::DecodeCache cache(kImageBytes * 5);
    cache.insert("gif", makeAnimationResult(4));
    cache.insert("still", makeResult());
    QCOMPARE(cache.usedBytes(), kImageBytes * 5);

    cache.insert("another", makeResult());

    // 予算を超えたため、古い方 (アニメーション) が落ちる。
    QVERIFY(!cache.containsKey("gif"));
  }

  // ページの番号は鍵に入ること。同じファイルの別のページを取り違えない。
  void keepsPagesOfOneFileApart() {
    using heiran::ui::DecodeCache;
    QCOMPARE(DecodeCache::makeKey("a.tif", true), DecodeCache::makeKey("a.tif", true, 0));
    QVERIFY(DecodeCache::makeKey("a.tif", true, 1) != DecodeCache::makeKey("a.tif", true, 0));
    QVERIFY(DecodeCache::makeKey("a.tif", true, 1) != DecodeCache::makeKey("a.tif", true, 2));

    DecodeCache cache(kImageBytes * 4);
    cache.insert(DecodeCache::makeKey("a.tif", true, 0), makeResult());
    cache.insert(DecodeCache::makeKey("a.tif", true, 2), makeResult());
    QVERIFY(cache.contains("a.tif", true, 0));
    QVERIFY(!cache.contains("a.tif", true, 1));
    QVERIFY(cache.contains("a.tif", true, 2));
  }

  // 0 ページ目の鍵は従来のまま。番号を付けない形式の鍵を変えない。
  void leavesTheKeyOfTheFirstPageAlone() {
    QCOMPARE(heiran::ui::DecodeCache::makeKey("a.png", true), QString("a.png|exif=1"));
    QCOMPARE(heiran::ui::DecodeCache::makeKey("a.png", false), QString("a.png|exif=0"));
  }

  // peek は数えず、LRU の順も動かさないこと。表示を組み立て直すたびに引いても、
  // 効いているかの計測を歪めず、使っていないものが新しい扱いにならない。
  void peeksWithoutCountingOrTouching() {
    heiran::ui::DecodeCache cache(kImageBytes * 2);
    cache.insert("a", makeResult());
    cache.insert("b", makeResult());
    cache.resetCounters();

    QVERIFY(cache.peek("a") != nullptr);
    QVERIFY(cache.peek("missing") == nullptr);
    QCOMPARE(cache.hitCount(), 0);
    QCOMPARE(cache.missCount(), 0);

    // a を peek しても新しい扱いにならない。次に入れると a が落ちる。
    cache.insert("c", makeResult());
    QVERIFY(!cache.containsKey("a"));
    QVERIFY(cache.containsKey("b"));
  }

  void keepsEntriesWithinBudget() {
    heiran::ui::DecodeCache cache(kImageBytes * 3);
    cache.insert("a", makeResult());
    cache.insert("b", makeResult());
    cache.insert("c", makeResult());

    QCOMPARE(cache.entryCount(), 3);
    QCOMPARE(cache.usedBytes(), kImageBytes * 3);
    QVERIFY(cache.containsKey("a"));
  }

  void evictsLeastRecentlyUsedEntryWhenBudgetExceeded() {
    heiran::ui::DecodeCache cache(kImageBytes * 2);
    cache.insert("a", makeResult());
    cache.insert("b", makeResult());
    cache.insert("c", makeResult());

    QCOMPARE(cache.entryCount(), 2);
    QCOMPARE(cache.usedBytes(), kImageBytes * 2);
    QVERIFY(!cache.containsKey("a"));
    QVERIFY(cache.containsKey("b"));
    QVERIFY(cache.containsKey("c"));
  }

  void treatsReadAsRecentUse() {
    heiran::ui::DecodeCache cache(kImageBytes * 2);
    cache.insert(heiran::ui::DecodeCache::makeKey("a", true), makeResult());
    cache.insert(heiran::ui::DecodeCache::makeKey("b", true), makeResult());

    QVERIFY(cache.value("a", true).ok);
    cache.insert(heiran::ui::DecodeCache::makeKey("c", true), makeResult());

    QVERIFY(cache.contains("a", true));
    QVERIFY(!cache.contains("b", true));
    QVERIFY(cache.contains("c", true));
  }

  void evictsNonRetainedEntriesFirst() {
    heiran::ui::DecodeCache cache(kImageBytes * 2);
    cache.insert("retained", makeResult());
    cache.insert("prefetch", makeResult());
    cache.setRetainedKeys({"retained"});

    cache.insert("next", makeResult());

    // LRU 順では retained が最古だが、現在ページ分として保持される。
    QVERIFY(cache.containsKey("retained"));
    QVERIFY(!cache.containsKey("prefetch"));
    QVERIFY(cache.containsKey("next"));
  }

  void evictsRetainedEntriesWhenNothingElseRemains() {
    heiran::ui::DecodeCache cache(kImageBytes);
    cache.insert("retainedOld", makeResult());
    cache.insert("retainedNew", makeResult());
    cache.setRetainedKeys({"retainedOld", "retainedNew"});

    cache.insert("next", makeResult());

    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(cache.containsKey("next"));
  }

  // 保持範囲の外でも、予算に収まっている限り捨てないこと。
  //
  // 以前は無条件で捨てていたため、予算が 1024 MiB あっても前後 1 ページ分しか
  // 残らず、ページを戻すたびに復号し直していた。
  void keepsAnalysisBuffersOutsideTheWindowWhileWithinBudget() {
    heiran::ui::DecodeCache cache(kImageBytes * 100);
    cache.insert("inWindow", makeResultWithAnalysisBuffer());
    cache.insert("outOfWindow", makeResultWithAnalysisBuffer());

    cache.setAnalysisWindowKeys({"inWindow"});

    QVERIFY(cache.containsKey("inWindow"));
    QVERIFY2(cache.containsKey("outOfWindow"),
             "entries outside the window must survive while the budget allows");
  }

  // 予算を超えたときは、保持範囲の外にある解析用バッファから先に捨てること。
  // 1 枚が大きく、解析からも表示からも当面使わない。
  void evictsAnalysisBuffersOutsideTheWindowFirst() {
    const qint64 analysisEntryBytes =
        heiran::ui::DecodeCache::entryBytes(makeResultWithAnalysisBuffer());
    // 解析用バッファ付きが 2 つは入らない大きさにする。
    heiran::ui::DecodeCache cache(analysisEntryBytes * 2 + kImageBytes);

    cache.insert("outOfWindow", makeResultWithAnalysisBuffer());
    cache.insert("plain", makeResult());
    cache.insert("inWindow", makeResultWithAnalysisBuffer());
    cache.setAnalysisWindowKeys({"inWindow"});

    // 予算を下げて超えさせる。
    cache.setBudgetBytes(analysisEntryBytes + kImageBytes);

    QVERIFY2(!cache.containsKey("outOfWindow"), "the out-of-window entry should go first");
    QVERIFY2(cache.containsKey("inWindow"), "the in-window entry should survive");
    QVERIFY2(cache.containsKey("plain"),
             "entries without an analysis buffer are cheaper to keep than large ones");
  }

  // 8bit 画像は解析用バッファを持たないため、保持範囲の外でも残る。
  void keepsEntriesWithoutAnalysisBuffersOutsideTheWindow() {
    heiran::ui::DecodeCache cache(kImageBytes * 100);
    cache.insert("a", makeResult());
    cache.insert("b", makeResult());

    cache.setAnalysisWindowKeys({"a"});

    QVERIFY(cache.containsKey("a"));
    QVERIFY(cache.containsKey("b"));
  }

  // 範囲の外から遅れて届いた結果も受け取ること。予算に収まる限り抱えてよい。
  void acceptsAnalysisBuffersArrivingFromOutsideTheWindow() {
    heiran::ui::DecodeCache cache(kImageBytes * 100);
    cache.setAnalysisWindowKeys({"inWindow"});

    cache.insert("inWindow", makeResultWithAnalysisBuffer());
    cache.insert("late", makeResultWithAnalysisBuffer());
    cache.insert("plain", makeResult());

    QVERIFY(cache.containsKey("inWindow"));
    QVERIFY(cache.containsKey("late"));
    QVERIFY(cache.containsKey("plain"));
  }

  void reportsWhetherAnEntryCarriesAnAnalysisBuffer() {
    heiran::ui::DecodeCache cache(kImageBytes * 100);
    cache.insert("plain", makeResult());
    cache.insert("analysis", makeResultWithAnalysisBuffer());

    QVERIFY(!cache.hasAnalysisBuffer("plain"));
    QVERIFY(cache.hasAnalysisBuffer("analysis"));
    QVERIFY(!cache.hasAnalysisBuffer("missing"));
  }

  void keepsLastInsertedEntryEvenWhenOversized() {
    heiran::ui::DecodeCache cache(kImageBytes / 2);
    cache.insert("a", makeResult());

    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(cache.containsKey("a"));
  }

  void doesNotDoubleCountReinsertedKey() {
    heiran::ui::DecodeCache cache(kImageBytes * 4);
    cache.insert("a", makeResult());
    cache.insert("a", makeResult());

    QCOMPARE(cache.entryCount(), 1);
    QCOMPARE(cache.usedBytes(), kImageBytes);
  }

  void shrinksWhenBudgetIsLowered() {
    heiran::ui::DecodeCache cache(kImageBytes * 3);
    cache.insert("a", makeResult());
    cache.insert("b", makeResult());
    cache.insert("c", makeResult());

    cache.setBudgetBytes(kImageBytes);

    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(cache.containsKey("c"));
  }

  void clearsUsedBytes() {
    heiran::ui::DecodeCache cache(kImageBytes * 3);
    cache.insert("a", makeResult());
    cache.clear();

    QCOMPARE(cache.entryCount(), 0);
    QCOMPARE(cache.usedBytes(), 0);
  }

  void returnsEmptyResultForMissingKey() {
    heiran::ui::DecodeCache cache(kImageBytes);
    QVERIFY(!cache.value("missing", true).ok);
  }
};

QTEST_MAIN(DecodeCacheTests)

#include "DecodeCacheTests.moc"
