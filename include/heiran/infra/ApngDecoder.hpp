#pragma once

#include "heiran/infra/AnimationComposer.hpp"
#include "heiran/infra/ImageDecoder.hpp"

#include <QImage>
#include <QString>
#include <QVector>
#include <memory>

namespace heiran::infra {

// 合成と結果の型は、WebP と共通のものを使う。
using ApngFrame = AnimationFrame;
using ApngResult = AnimationResult;

// アニメーション PNG (APNG) か。最初の IDAT より前のチャンクだけを読んで判定する。
// 通常の PNG を開くたびに通る経路のため、画素を含む部分は読まない。
bool isAnimatedPng(const QString &filePath);

// APNG のヘッダーだけから分かること。画素は読まない。
struct ApngInfo {
  bool animated = false;
  int width = 0;
  int height = 0;
  int frameCount = 0;

  // 全フレームを展開したときの大きさ。展開後は常に RGBA8 のキャンバスで、幅 x 高さ x 4 x
  // フレーム数で決まる。圧縮したファイルの大きさとは関係が薄く、1080x1080 で 150 コマの
  // APNG は、ファイルが 8 MB でも約 700 MB になる。開く前にこれで見積もれる。
  qint64 decodedBytes() const {
    return static_cast<qint64>(width) * height * 4 * static_cast<qint64>(frameCount);
  }
};

// IHDR と acTL だけを読む。どちらも最初の画像データ (IDAT) より前にある。
ApngInfo probeApng(const QString &filePath);

// 全フレームを読む。Qt も libspng も APNG の 2 枚目以降を読めないため、自前で組む。
//
// 各フレームは、それだけで完結した PNG として圧縮されている。そこで、フレームごとに
// 単独の PNG を組み直して復号する (並列に走らせる)。前のフレームとの合成 (disposal と
// blend) だけは順番が要るため、復号が済んでから順に行う。
//
// `maxBytes` を超えるフレームは読まずに、`truncated` を立てて打ち切る。
// `firstFrameOnly` なら 1 枚目だけを読む。サムネイルのように、動きが要らない用途向け。
ApngResult decodeApng(const QString &filePath, qint64 maxBytes, bool firstFrameOnly = false);

// 圧縮したまま持ち、要るフレームだけを背景で作る APNG。大きなアニメーション向け。
//
// 全部を展開すると 幅 x 高さ x 4 x フレーム数 のメモリを使う。フレームは互いに合成が要る
// (前のフレームの上へ描き、描いた後に背景や前の状態へ戻す) ため、順に作るのが基本になる。
// 再生は前へ進むだけなので、その先の数フレームを先に作っておけば間に合う。
// 一定間隔でキャンバスの写しを持ち、コマ戻しや飛び越しは、最も近い写しから作り直す。
//
// 開いた時点で先頭のフレームだけは作り終えている (同期)。
// `snapshotInterval` が 0 以下なら、フレーム数から決める。
std::shared_ptr<FrameStream> openApngStream(const QString &filePath,
                                            QString *errorMessage = nullptr,
                                            int snapshotInterval = 0);

}  // namespace heiran::infra
