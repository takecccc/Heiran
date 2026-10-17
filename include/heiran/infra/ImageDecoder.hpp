#pragma once

#include "heiran/domain/ColorSpace.hpp"
#include "heiran/infra/EmbeddedColorProfile.hpp"
#include "heiran/infra/ExifOrientation.hpp"

#include <QByteArray>
#include <QFloat16>
#include <QImage>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>

namespace heiran::infra {

enum class PixelSampleType {
  Unknown,
  UInt8,
  UInt16,
  Float16,
  Float32,
};

enum class PixelColorModel {
  Unknown,
  Gray,
  GrayAlpha,
  RGB,
  RGBA,
};

int bytesPerSample(PixelSampleType sampleType);

int channelCountForColorModel(PixelColorModel colorModel);

struct PixelBuffer {
  QSharedPointer<QByteArray> storage;
  qsizetype bytesPerRow = 0;
  int width = 0;
  int height = 0;
  int channelCount = 0;
  PixelSampleType sampleType = PixelSampleType::Unknown;
  PixelColorModel colorModel = PixelColorModel::Unknown;

  bool isValid() const { return storage && !storage->isEmpty() && width > 0 && height > 0; }
  qsizetype byteCount() const { return storage ? storage->size() : 0; }
  const uchar *data() const {
    return storage ? reinterpret_cast<const uchar *>(storage->constData()) : nullptr;
  }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

  // 保持している精度に関わらず正規化した値を返す。
  // 整数型は 0.0-1.0 へ写し、浮動小数型は元の値をそのまま返すため 1.0 を超えうる。
  // 範囲外の座標や保持していないチャネルでは 0.0 を返す。
  float sample(int x, int y, int channel) const;

  // 色モデルを考慮して RGBA の並びで返す。
  // Gray 系は R/G/B に同じ値を割り当て、アルファを持たない場合は 1.0 を返す。
  float sampleRgba(int x, int y, int rgbaChannel) const;
};

// 1 つのファイルが複数の画像を持つ場合の種類。
//
// **ページとフレームは性質が違う。** ページ (マルチページ TIFF、EXR のパート) は独立した
// 画像で、手でめくる。フレーム (アニメーション GIF、WebP) は時間の流れで、再生する。
// 1 つの軸に混ぜると、数百コマのアニメーションがページの並びを埋めてしまい、ファイルの
// 連番 (比較の主役) とコマ送りも区別できなくなる。
enum class SubImageKind {
  None,
  // 独立した画像。指定した 1 つだけを読む。
  Pages,
  // 時間の流れ。全フレームを 1 度に読み、`frameOf()` で取り出す。
  Frames,
};

struct FrameSequence;

// 後から作るアニメーション。全フレームを展開せずに持ち、要るフレームだけを背景で作る。
//
// 展開後の大きさは 幅 x 高さ x 4 x フレーム数 で決まり、圧縮したファイルの大きさとは関係が
// 薄い。1080x1080 で 150 コマの APNG は、ファイルが 8 MB でも展開すると約 700 MB になる。
// 大きなものは全部を展開せず、再生に間に合う分だけを先に作る。
//
// 作り終えたフレームを尋ねる側 (`frameIfReady`) は待たない。作り終えていなければ null を返し、
// `request()` で作らせて、作れたら listener が呼ばれる。
class FrameStream {
public:
  virtual ~FrameStream() = default;

  virtual int frameCount() const = 0;
  // フレームを見せておく時間 (ミリ秒)。0 や極端に短い値は 100 ms へ直してある。
  // index は範囲を超えたら巻き戻す。
  virtual int delayMs(int index) const = 0;
  // 作り終えたフレーム。作っていなければ null。index は範囲を超えたら巻き戻す。
  virtual QImage frameIfReady(int index) = 0;
  // index から先の数フレームを、背景で作らせる。再生の進みに合わせて呼び直す。
  virtual void request(int index) = 0;
  // 全部を保持した場合ではなく、このストリームが実際に抱え得るメモリの上限。
  virtual qint64 residentBytes() const = 0;

  // フレームを作り終えたときに、作った側のスレッドから呼ばれる。owner ごとに 1 つ。
  // 呼び側が消える前に removeListener を呼ぶこと。それが戻れば、もう呼ばれない。
  virtual void setListener(const void *owner, std::function<void()> listener) = 0;
  virtual void removeListener(const void *owner) = 0;

  // 試験用。いま作り終えて持っているフレームの数。
  virtual int residentFrameCountForTesting() const { return 0; }
};

struct DecodedImage {
  QImage displayImage;
  PixelBuffer analysisBuffer;
  QString decoderBackend;
  PixelSampleType sourceSampleType = PixelSampleType::Unknown;
  PixelColorModel sourceColorModel = PixelColorModel::Unknown;
  int sourceChannelCount = 0;
  // ファイルへ埋め込まれていた色情報。無ければ空。
  EmbeddedColorProfile embeddedColorProfile;
  // 使うべき色空間。埋め込みがあればそれ、無ければ形式ごとの既定になる。
  QColorSpace colorSpace;

  // 復号時に適用された EXIF の orientation。適用していない場合は Normal。
  // 画素値の位置を元ファイルの座標で出すために、適用した分を戻す必要がある。
  ExifOrientation appliedExifOrientation = ExifOrientation::Normal;

  // ファイルが複数の画像を持つ場合の情報。持たなければ None、個数と番号は 1 と 0。
  SubImageKind subImageKind = SubImageKind::None;
  int subImageCount = 1;
  // この結果が表すページ / フレームの番号。
  int subImageIndex = 0;
  // Frames のとき、このフレームを見せておく時間 (ミリ秒)。
  int frameDelayMs = 0;
  // Frames のとき、全フレーム。先頭のフレームは、この結果そのものと同じ内容になる。
  QSharedPointer<const FrameSequence> frameSequence;
  // Frames のうち、全部を展開せず後から作るもの。あればこの結果そのものが先頭のフレーム。
  // frameSequence とはどちらか一方だけを持つ。
  std::shared_ptr<FrameStream> frameStream;

  bool isAnimation() const { return subImageKind == SubImageKind::Frames && subImageCount > 1; }

  bool hasAnalysisBuffer() const { return analysisBuffer.isValid(); }

  // 解析用バッファがあればそこから、無ければ表示用画像から RGBA の正規化値を返す。
  // 8bit 画像では表示用画像がそのまま元データなので、解析用バッファを別に持たない。
  float sampleRgba(int x, int y, int rgbaChannel) const;

  // 全画素の輝度を元画像座標の並びで返す。値は 0.0-1.0 へ正規化する。
  // 画素ごとに `sampleRgba()` を呼ぶと、型と色モデルの判定を毎回通るうえ、
  // 解析用バッファを持たない画像では `QImage::pixelColor()` まで通る。
  // 全画素を走査する解析では、判定を走査の外へ出したこちらを使う。
  QVector<float> buildSourceLumaPlane(const heiran::domain::LumaWeights &weights) const;

  // 指定した RGBA チャネルを元画像座標の並びで返す。値は 0.0-1.0 へ正規化する。
  // ヒストグラムのようにチャネルごとに集計する処理で使う。3 チャネル分を同時に
  // 持つと 4K で 100 MiB を超えるため、1 チャネルずつ作って畳む使い方を想定する。
  QVector<float> buildSourceChannelPlane(int rgbaChannel) const;

  // R、G、B の 3 つの平面を、画像を 1 回だけ走査して作る (元画像座標の並び、0.0-1.0)。
  // 色差のように、3 チャネルを同時に要る処理で使う。`buildSourceChannelPlane()` を 3 回呼ぶと、
  // 同じ画像を 3 回読み、平面ごとに 0 で埋め直すことになる。作れないときは false。
  bool buildSourceRgbPlanes(QVector<float> (&planes)[3]) const;

  QSize size() const {
    return !displayImage.isNull() ? displayImage.size()
                                  : QSize(analysisBuffer.width, analysisBuffer.height);
  }
};

struct FrameSequence {
  QVector<DecodedImage> frames;
  // メモリの上限で、後ろのフレームを読まずに打ち切った。
  bool truncated = false;
};

// アニメーションの `index` 番目のフレーム。範囲を超えたら先頭へ巻き戻す。
// アニメーションでなければ、結果そのものを返す。後から作るもの (frameStream) で、まだ作って
// いなければ、代わりに先頭のフレームを返す。待たずに済ませたい側は tryFrameOf を使う。
DecodedImage frameOf(const DecodedImage &decodedImage, int index);

// `frameOf` と同じだが、後から作るものでまだ作っていなければ nullopt を返す。
// 作らせるのは呼び側が `frameStream->request()` で行う。
std::optional<DecodedImage> tryFrameOf(const DecodedImage &decodedImage, int index);

// `index` 番目のフレームの表示時間 (ミリ秒)。アニメーションでなければ 0。
int frameDelayMsOf(const DecodedImage &decodedImage, int index);

// フレームの表示時間の正規化。0 や 10 ms 以下は 100 ms として扱う (ブラウザと同じ)。
// 制作ツールが 0 を「既定」の意味で書き、そのまま従うと目で追えない速さになる。
int normalizedFrameDelayMs(int delayMs);

// アニメーションを全部展開するか、後から作るかの境目 (展開後の大きさ)。既定は 128 MiB。
// 展開後の大きさは、ファイルを開かずに 幅 x 高さ x 4 x フレーム数 で見積もれる。
// 試験が小さなファイルで後から作る経路を通すために、変えられるようにしてある。
qint64 animationStreamingThresholdBytes();
void setAnimationStreamingThresholdBytes(qint64 bytes);

// 解析用バッファから表示用の 8bit 画像を作る。
// gain を掛けてから 0.0-1.0 へ丸めるため、浮動小数画像の 1.0 超の領域を
// 露出調整で確認できる。
QImage renderDisplayImage(const PixelBuffer &buffer, float gain);

// 解析用バッファを RGBA 並びの half float 列へ詰め替える。GPU テクスチャへ載せる用。
// 整数型は 0.0-1.0 へ写し、浮動小数型は元の値をそのまま渡すため 1.0 を超えうる。
// 丸めずに載せることで、露出をシェーダ側で掛けても 1.0 超の領域を復元できる。
QVector<qfloat16> packRgbaFloat16(const PixelBuffer &buffer);

struct DecodeResult {
  bool ok = false;
  DecodedImage decodedImage;
  QString errorMessage;
};

class ImageDecoder {
public:
  // `subImageIndex` はページ (Pages) を持つファイルで、読むページを指す。
  // フレーム (Frames) は全部を読むため無視する。ページを持たないファイルでも無視する。
  //
  // `firstFrameOnly` はアニメーションの先頭のフレームだけを読み、普通の画像として返す。
  // サムネイルのように動きが要らない用途向け。数百フレームを、1 枚のために全部読まない。
  DecodeResult decode(const QString &filePath, bool honorExifOrientation, int subImageIndex = 0,
                      bool firstFrameOnly = false) const;
};

}  // namespace heiran::infra
