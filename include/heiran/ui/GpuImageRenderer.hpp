#pragma once

#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/DisplayTransform.hpp"
#include "heiran/ui/TextureBudgetLedger.hpp"

#include <QColor>
#include <QHash>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace heiran::ui {

// 表示変換と描画を GPU で行う。
// 元画像をテクスチャとして 1 度だけ載せ、露出、チャネル、ガンマ、白黒反転は
// フラグメントシェーダで適用する。表示のたびに全画素を CPU で処理しない。
//
// 解析には使わない。画素値表示、ヒストグラム、差分は解析用バッファの値を使う。
class GpuImageRenderer : protected QOpenGLFunctions {
public:
  // 使用量は台帳へ届ける。省略するとアプリで共有する台帳を使う。
  explicit GpuImageRenderer(TextureBudgetLedger *ledger = nullptr);
  ~GpuImageRenderer();

  // GL コンテキストが現在のスレッドで有効な状態で呼ぶ。
  bool initialize();
  bool isInitialized() const { return initialized_; }

  // 表示対象を差し替える。次の描画でテクスチャを作り直す。
  void setImage(const heiran::infra::DecodedImage &decodedImage);

  // 変換後の表示サイズ。回転で縦横が入れ替わる。
  QSize displaySize(const DisplayTransformParams &params) const;

  // いま描いているテクスチャのバイト数。ミップマップ分を含む。
  // テクスチャを作る前は 0 を返す。
  qint64 textureBytes() const { return textureBytes_; }

  // 取っておいているテクスチャの合計。VRAM の消費はこちらで見る。
  qint64 cachedTextureBytes() const { return cachedTextureBytes_; }

  // 取っておく量の上限は台帳が全セルの合計に対して持つ。溢れた分は、全セルを通して
  // 最も使われていないものから捨てる。
  //
  // ページを送るたびにテクスチャを載せ直すと、4K で 1 回あたり 9 ms 近くかかる。
  // 一度載せたものを取っておけば、戻ったときの送りが速くなる。
  //
  // 別のセルの都合で自分のテクスチャを捨てると決められた場合、消すのは次に描く
  // ときになる (コンテキストが現在の状態でないと消せない)。それまで VRAM は
  // 残るため、`wake` で再描画を促せる。
  void setEvictionWake(std::function<void()> wake) { evictionWake_ = std::move(wake); }

  // targetRect はウィジェット座標での描画先。
  // devicePixelRatio は QOpenGLWidget の値をそのまま渡す。
  // backdropColor は透過画素の下地。別の矩形として塗ると丸めの差で縁からはみ出すため、
  // シェーダ内で合成する。
  void draw(const DisplayTransformParams &params, const QRectF &targetRect, const QSize &widgetSize,
            double devicePixelRatio, bool smooth, const QColor &backdropColor);

  void release();

private:
  bool ensureTexture();
  // 画素データを見分ける鍵。同じ画像なら載せ直さない。
  static QString textureKeyFor(const heiran::infra::DecodedImage &decodedImage);
  // 台帳へ載せた (または使い直した) ことを伝える。溢れていれば他が捨てられる。
  void noteTextureUsed(const QString &key, qint64 bytes);
  // 捨てると決められたテクスチャを消す。コンテキストが現在の状態で呼ぶ。
  void dropEvictedTextures();
  void dropTexture(const QString &key);
  // 解析用バッファを RGBA16F として載せる。載せられない場合は false を返し、
  // 呼び出し側は 8bit の表示用画像へ退避する。
  bool createFloatTexture();
  static bool canUploadUInt16Directly(const heiran::infra::PixelBuffer &buffer);
  static bool canUploadFloat16Directly(const heiran::infra::PixelBuffer &buffer);
  static std::shared_ptr<QOpenGLTexture> createUInt8Texture(const QImage &sourceImage);
  bool ensureColorLutTexture(const std::shared_ptr<const ColorLut> &colorLut);

  heiran::infra::DecodedImage decodedImage_;
  bool textureDirty_ = false;
  bool initialized_ = false;
  QSize sourceSize_;
  qint64 textureBytes_ = 0;

  std::unique_ptr<QOpenGLShaderProgram> program_;
  struct TextureEntry {
    // `QHash` の値はコピーできる必要があるため共有ポインタで持つ。
    // 所有はこのキャッシュだけにある。
    std::shared_ptr<QOpenGLTexture> texture;
    qint64 bytes = 0;
  };

  // 載せ済みのテクスチャ。鍵は画素データの同一性。
  QHash<QString, TextureEntry> textureCache_;
  QString currentTextureKey_;
  qint64 cachedTextureBytes_ = 0;
  TextureBudgetLedger *ledger_ = nullptr;
  // 台帳が捨てると決めた鍵。次に描くときに消す。
  QStringList evictedKeys_;
  std::function<void()> evictionWake_;
  bool notingTextureUsed_ = false;
  // 描画に使う 1 つ。所有はキャッシュ側にある。
  QOpenGLTexture *texture_ = nullptr;
  std::unique_ptr<QOpenGLTexture> colorLutTexture_;
  std::shared_ptr<const ColorLut> colorLutSource_;
  QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
};

}  // namespace heiran::ui
