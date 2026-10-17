#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QImage>
#include <QString>
#include <QVector>
#include <memory>

namespace heiran::infra {

// アニメーション PNG (APNG) と WebP に共通の、フレームの合成。
//
// どちらも「キャンバスの上へ、部分的な矩形のフレームを重ねていく」形式で、描いた後に背景へ戻す
// などの指定を持つ。違うのは、フレームの画素の圧縮の仕方だけ。そこで、フレームの取り出しを
// `AnimationSource` に切り出し、合成 (全部を展開する / 要るフレームだけを作る) は共有する。

// フレームを描いた後の扱い (disposal)。値は APNG の仕様に合わせる。
// WebP は なし / 背景 の 2 つを使う。
inline constexpr quint8 kAnimationDisposeNone = 0;
inline constexpr quint8 kAnimationDisposeBackground = 1;
inline constexpr quint8 kAnimationDisposePrevious = 2;

// フレームを描くときの重ね方 (blend)。置き換えるか、アルファで重ねるか。
inline constexpr quint8 kAnimationBlendSource = 0;
inline constexpr quint8 kAnimationBlendOver = 1;

struct AnimationFrameControl {
  quint32 width = 0;
  quint32 height = 0;
  quint32 xOffset = 0;
  quint32 yOffset = 0;
  // ファイルの値そのまま (ミリ秒)。0 や極端に短い値の扱いは、使う側が決める。
  int delayMs = 0;
  quint8 disposeOp = kAnimationDisposeNone;
  quint8 blendOp = kAnimationBlendSource;
};

// 圧縮したフレームの持ち主。形式ごとに、フレームの取り出し方を持つ。
class AnimationSource {
public:
  virtual ~AnimationSource() = default;

  virtual int canvasWidth() const = 0;
  virtual int canvasHeight() const = 0;
  virtual int frameCount() const = 0;
  virtual const AnimationFrameControl &control(int index) const = 0;
  // フレーム 1 枚を、アルファを掛けない RGBA8888 の画像へ復号する。大きさは control のとおり。
  // 複数のスレッドから同時に呼ばれる。失敗したら null。
  virtual QImage decodeFrame(int index) const = 0;
  // 圧縮したデータの大きさ。逐次展開が抱えるメモリの見積りに使う。
  virtual qint64 compressedBytes() const = 0;

  // 全フレームを展開したときの大きさ。展開後は常に RGBA8 のキャンバスで、幅 x 高さ x 4 x
  // フレーム数で決まる。画素を読まずに求まる。
  qint64 decodedBytes() const {
    return static_cast<qint64>(canvasWidth()) * canvasHeight() * 4 * frameCount();
  }
};

struct AnimationFrame {
  // キャンバス全体の大きさで、直前までの合成が済んだもの。アルファは掛けていない。
  QImage image;
  // 表示時間 (ミリ秒)。ファイルの値そのまま。
  int delayMs = 0;
};

struct AnimationResult {
  bool ok = false;
  QVector<AnimationFrame> frames;
  // メモリの上限で、後ろのフレームを読まずに打ち切った。
  bool truncated = false;
  QString errorMessage;
};

// 全フレームを展開する。復号は並列に走らせ、合成だけを順に行う。
// `maxBytes` を超えるフレームは読まずに、`truncated` を立てて打ち切る。
// `firstFrameOnly` なら 1 枚目だけを読む。
AnimationResult composeAllFrames(const AnimationSource &source, qint64 maxBytes,
                                 bool firstFrameOnly = false);

// 圧縮したまま持ち、要るフレームだけを背景で作る。大きなアニメーション向け。
//
// 全部を展開すると 幅 x 高さ x 4 x フレーム数 のメモリを使う。フレームは互いに合成が要る
// (前のフレームの上へ描き、描いた後に背景や前の状態へ戻す) ため、順に作るのが基本になる。
// 再生は前へ進むだけなので、その先の数フレームを先に作っておけば間に合う。
// 一定間隔でキャンバスの写しを持ち、コマ戻しや飛び越しは、最も近い写しから作り直す。
//
// 開いた時点で先頭のフレームだけは作り終えている (同期)。
// `snapshotInterval` が 0 以下なら、フレーム数から決める。
std::shared_ptr<FrameStream> openAnimationStream(std::shared_ptr<const AnimationSource> source,
                                                 QString *errorMessage = nullptr,
                                                 int snapshotInterval = 0);

}  // namespace heiran::infra
