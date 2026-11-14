#include "heiran/ui/GpuImageRenderer.hpp"

#include "heiran/domain/ImageTransform.hpp"

#include <QDebug>
#include <QMatrix3x3>
#include <QOpenGLContext>
#include <QOpenGLPixelTransferOptions>
#include <QVector3D>
#include <array>
#include <cmath>
#include <optional>

namespace heiran::ui {

namespace {

// 頂点は表示先の矩形、テクスチャ座標は回転と反転を織り込んだ元画像上の位置。
// 回転と反転を頂点側で済ませることで、フラグメントシェーダは色の変換だけを行う。
constexpr auto kVertexShader = R"(
attribute highp vec2 position;
attribute highp vec2 texCoord;
varying highp vec2 vTexCoord;
void main() {
  vTexCoord = texCoord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)";

// CPU 実装と同じ順序で適用する。チャネル選択、露出、ガンマ、白黒反転。
// 透過画像の下地もここで合成する。別の矩形として塗ると、丸めの差で
// 画像の縁から下地がはみ出す。
constexpr auto kFragmentShader = R"(
uniform sampler2D source;
uniform highp vec3 backdrop;
uniform highp vec3 alternateBackdrop;
uniform highp float checkerSize;
uniform highp float viewportHeight;
uniform highp float gain;
uniform highp float gammaExponent;
uniform int channelMode;
uniform int invert;
uniform highp vec3 lumaWeights;
uniform int colorManaged;
uniform int sourceTransfer;
uniform highp float sourceGamma;
uniform int displayTransfer;
uniform highp float displayGamma;
uniform int applyPrimaries;
uniform highp mat3 primaryMatrix;
uniform sampler3D colorLut;
uniform int useColorLut;
uniform highp float lutScale;
uniform highp float lutOffset;
varying highp vec2 vTexCoord;

// 式は ColorManagement.cpp の decodeShaderTransfer / encodeShaderTransfer と同じ。
// 片方だけ直すと GPU と CPU で色がずれるため、両方を合わせて変える。
highp float decodeChannel(highp float value) {
  highp float s = value < 0.0 ? -1.0 : 1.0;
  highp float a = abs(value);
  if (sourceTransfer == 0) {
    return value;
  }
  if (sourceTransfer == 2) {
    return s * pow(a, sourceGamma);
  }
  return s * (a <= 0.04045 ? a / 12.92 : pow((a + 0.055) / 1.055, 2.4));
}

highp float encodeChannel(highp float value) {
  highp float s = value < 0.0 ? -1.0 : 1.0;
  highp float a = abs(value);
  if (displayTransfer == 0) {
    return value;
  }
  if (displayTransfer == 2) {
    return s * pow(a, 1.0 / displayGamma);
  }
  return s * (a <= 0.0031308 ? a * 12.92 : 1.055 * pow(a, 1.0 / 2.4) - 0.055);
}

void main() {
  highp vec4 texel = texture2D(source, vTexCoord);
  highp vec3 rgb = texel.rgb;
  highp float alpha = texel.a;

  if (channelMode == 1) {
    highp float gray = dot(rgb, lumaWeights);
    rgb = vec3(gray);
    alpha = 1.0;
  } else if (channelMode == 2) {
    rgb = vec3(rgb.r);
    alpha = 1.0;
  } else if (channelMode == 3) {
    rgb = vec3(rgb.g);
    alpha = 1.0;
  } else if (channelMode == 4) {
    rgb = vec3(rgb.b);
    alpha = 1.0;
  } else if (channelMode == 5) {
    rgb = vec3(alpha);
    alpha = 1.0;
  }

  if (useColorLut != 0) {
    // 行列と曲線では表せない色空間。変換を焼いた 3 次元の表から線形光を引く。
    // 表の入力は 0.0-1.0。この経路を要する色空間 (LUT を持つ ICC、PQ、HLG) は
    // いずれも符号化された値がこの範囲に収まるため、端で止めてよい。
    highp vec3 lutCoord = clamp(rgb, 0.0, 1.0) * lutScale + lutOffset;
    rgb = texture3D(colorLut, lutCoord).rgb;
    rgb = rgb * gain;
    rgb = vec3(encodeChannel(rgb.r), encodeChannel(rgb.g), encodeChannel(rgb.b));
    rgb = clamp(rgb, 0.0, 1.0);
  } else if (colorManaged != 0) {
    // 元の値を線形光へ戻し、原色を移し、露出を掛けてから表示用に符号化する。
    // 露出は線形光で当てる。stops は光量の倍率を表すため、符号化された値へ
    // 掛けると倍率どおりにならない。
    rgb = vec3(decodeChannel(rgb.r), decodeChannel(rgb.g), decodeChannel(rgb.b));
    if (applyPrimaries != 0) {
      rgb = primaryMatrix * rgb;
    }
    rgb = rgb * gain;
    rgb = vec3(encodeChannel(rgb.r), encodeChannel(rgb.g), encodeChannel(rgb.b));
    rgb = clamp(rgb, 0.0, 1.0);
  } else {
    rgb = clamp(rgb * gain, 0.0, 1.0);
  }
  rgb = pow(rgb, vec3(gammaExponent));
  if (invert != 0) {
    rgb = vec3(1.0) - rgb;
  }

  // 下地は変換の対象にしない。CPU 実装が白の矩形を先に塗ってから
  // 画像を重ねていたのと同じ結果になる。
  highp vec3 background = backdrop;
  if (checkerSize > 0.0) {
    highp vec2 square = floor(vec2(gl_FragCoord.x, viewportHeight - gl_FragCoord.y) / checkerSize);
    background = mix(backdrop, alternateBackdrop, mod(square.x + square.y, 2.0));
  }
  gl_FragColor = vec4(mix(background, rgb, alpha), 1.0);
}
)";

// RGBA16F を線形補間つきで扱えるかを見る。デスクトップ GL 3.0 以降は中核機能。
bool floatTexturesSupported() {
  const QOpenGLContext *context = QOpenGLContext::currentContext();
  if (context == nullptr) {
    return false;
  }
  if (context->isOpenGLES()) {
    return context->format().majorVersion() >= 3 &&
           context->hasExtension(QByteArrayLiteral("GL_OES_texture_half_float_linear"));
  }
  return context->format().majorVersion() >= 3 ||
         context->hasExtension(QByteArrayLiteral("GL_ARB_texture_float"));
}

// ミップマップを含むテクスチャのバイト数。各段は縦横が半分になる。
qint64 textureBytesFor(const QSize &size, int bytesPerTexel) {
  qint64 total = 0;
  int width = size.width();
  int height = size.height();
  while (width >= 1 && height >= 1) {
    total += static_cast<qint64>(width) * height * bytesPerTexel;
    if (width == 1 && height == 1) {
      break;
    }
    width = std::max(1, width / 2);
    height = std::max(1, height / 2);
  }
  return total;
}

int channelModeIndex(ChannelMode channelMode) {
  switch (channelMode) {
    case ChannelMode::Gray:
      return 1;
    case ChannelMode::Red:
      return 2;
    case ChannelMode::Green:
      return 3;
    case ChannelMode::Blue:
      return 4;
    case ChannelMode::Alpha:
      return 5;
    case ChannelMode::Color:
    default:
      return 0;
  }
}

// 表示先の 4 隅に対応する元画像上のテクスチャ座標を返す。
// 回転と反転をここで織り込む。
std::array<QPointF, 4> textureCorners(const DisplayTransformParams &params) {
  // 表示先の左上、右上、左下、右下に対応する元画像の正規化座標。
  std::array<QPointF, 4> corners{QPointF(0.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, 1.0),
                                 QPointF(1.0, 1.0)};

  const int quarterTurns = heiran::domain::normalizeQuarterTurns(params.rotationQuarterTurns);
  for (auto &corner : corners) {
    QPointF rotated = corner;
    switch (quarterTurns) {
      case 1:
        rotated = QPointF(corner.y(), 1.0 - corner.x());
        break;
      case 2:
        rotated = QPointF(1.0 - corner.x(), 1.0 - corner.y());
        break;
      case 3:
        rotated = QPointF(1.0 - corner.y(), corner.x());
        break;
      case 0:
      default:
        break;
    }
    corner = rotated;
  }

  // 反転は回転の後に適用する。表示先の座標側を入れ替える形になる。
  if (params.flipX) {
    std::swap(corners[0], corners[1]);
    std::swap(corners[2], corners[3]);
  }
  if (params.flipY) {
    std::swap(corners[0], corners[2]);
    std::swap(corners[1], corners[3]);
  }

  return corners;
}

}  // namespace

GpuImageRenderer::GpuImageRenderer(TextureBudgetLedger *ledger)
    : ledger_(ledger != nullptr ? ledger : &TextureBudgetLedger::shared()) {
  ledger_->registerOwner(this, [this](const QString &key) {
    evictedKeys_.push_back(key);
    // 自分の描画中に自分の分を捨てる場合は、すぐ消すため再描画は要らない。
    if (!notingTextureUsed_ && evictionWake_) {
      evictionWake_();
    }
  });
}

GpuImageRenderer::~GpuImageRenderer() {
  ledger_->unregisterOwner(this);
}

bool GpuImageRenderer::initialize() {
  if (initialized_) {
    return true;
  }

  initializeOpenGLFunctions();

  program_ = std::make_unique<QOpenGLShaderProgram>();
  if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
      !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
      !program_->link()) {
    qWarning().noquote() << "[HeiranGpu] shader link failed:" << program_->log();
    program_.reset();
    return false;
  }

  if (!vertexBuffer_.create()) {
    program_.reset();
    return false;
  }

  initialized_ = true;
  return true;
}

void GpuImageRenderer::setImage(const heiran::infra::DecodedImage &decodedImage) {
  decodedImage_ = decodedImage;
  sourceSize_ = decodedImage.size();
  textureDirty_ = true;
}

QSize GpuImageRenderer::displaySize(const DisplayTransformParams &params) const {
  return heiran::domain::transformedImageSize(
      sourceSize_, heiran::domain::normalizeQuarterTurns(params.rotationQuarterTurns));
}

// 16bit 整数の解析用バッファをそのまま載せられるか。
//
// 内部形式を正規化整数 (`GL_RGBA16`) にすると、渡す型と一致するため変換が要らない。
// 1-2 チャネル (Gray / GrayAlpha) は灰色を RGB へ複製する必要があり、それは
// 転送では表せないため対象外。
bool GpuImageRenderer::canUploadUInt16Directly(const heiran::infra::PixelBuffer &buffer) {
  return buffer.sampleType == heiran::infra::PixelSampleType::UInt16 && buffer.channelCount >= 3;
}

// half float の解析用バッファをそのまま載せられるか。条件は 16bit 整数と同じ。
bool GpuImageRenderer::canUploadFloat16Directly(const heiran::infra::PixelBuffer &buffer) {
  return buffer.sampleType == heiran::infra::PixelSampleType::Float16 && buffer.channelCount >= 3;
}

// 8bit の表示用画像をテクスチャへ載せる。
//
// 並びが GL の受け取れる形と一致していれば、詰め替えずにそのまま渡す。
// Qt の復号は形式によって並びが違い、JPEG などは `Format_RGB32` (BGRA の並び) で
// 返る。`Format_RGBA8888` へ変換すると 4K で 4-8 ms かかるが、GL は BGRA を
// そのまま受け取れるため、その変換は要らない (2026-09-04 計測)。
std::shared_ptr<QOpenGLTexture> GpuImageRenderer::createUInt8Texture(const QImage &sourceImage) {
  // 並びをそのまま渡せる形式。いずれも 1 画素 4 バイトで、alpha は掛かっていない。
  // `Format_ARGB32_Premultiplied` は alpha が掛かっており、シェーダの合成と
  // 食い違うため対象にしない。
  std::optional<QOpenGLTexture::PixelFormat> directFormat;
  switch (sourceImage.format()) {
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBX8888:
      directFormat = QOpenGLTexture::RGBA;
      break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
      directFormat = QOpenGLTexture::BGRA;
      break;
    default:
      break;
  }

  const QImage uploadImage =
      directFormat ? sourceImage : sourceImage.convertToFormat(QImage::Format_RGBA8888);
  if (uploadImage.isNull()) {
    return nullptr;
  }
  const auto pixelFormat = directFormat.value_or(QOpenGLTexture::RGBA);

  auto texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target2D);
  texture->setSize(uploadImage.width(), uploadImage.height());
  texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
  texture->setMipLevels(texture->maximumMipLevels());
  texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
  if (!texture->isStorageAllocated()) {
    return nullptr;
  }

  QOpenGLPixelTransferOptions options;
  options.setAlignment(1);
  const qsizetype rowBytes = static_cast<qsizetype>(uploadImage.width()) * 4;
  if (uploadImage.bytesPerLine() != rowBytes) {
    // 行に詰め物がある場合に、1 行が何画素分あるかを伝える。
    options.setRowLength(static_cast<int>(uploadImage.bytesPerLine() / 4));
  }
  texture->setData(pixelFormat, QOpenGLTexture::UInt8, uploadImage.constBits(), &options);
  texture->generateMipMaps();
  return texture;
}

bool GpuImageRenderer::createFloatTexture() {
  const auto &buffer = decodedImage_.analysisBuffer;
  if (!buffer.isValid() || buffer.sampleType == heiran::infra::PixelSampleType::UInt8) {
    return false;
  }
  if (!floatTexturesSupported()) {
    return false;
  }

  auto texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target2D);
  texture->setSize(buffer.width, buffer.height);
  texture->setMipLevels(texture->maximumMipLevels());

  if (canUploadUInt16Directly(buffer) || canUploadFloat16Directly(buffer)) {
    const bool isFloat16 = buffer.sampleType == heiran::infra::PixelSampleType::Float16;
    // 内部形式を正規化整数にして、CPU での詰め直しを省く。
    //
    // 内部形式を `RGBA16F` のままにして型だけ `UInt16` で渡す形も試したが、
    // 4K で 79-86 ms かかった。ドライバ側の型変換は 1 スレッドで走るように見える。
    // 内部形式を合わせると変換が要らなくなり 7-9 ms になる (2026-09-04 計測)。
    // CPU で fp16 へ詰め直す経路は 41 ms だった。
    //
    // 精度も上がる。fp16 の仮数は 11 bit しかなく、16bit の値をそのままは表せない。
    //
    // half float は逆に内部形式を `RGBA16F` にして、型を `Float16` のまま渡す。
    // 型が合っていれば変換は要らず、3 チャネルを 4 チャネルへ広げるだけになる。
    texture->setFormat(isFloat16 ? QOpenGLTexture::RGBA16F : QOpenGLTexture::RGBA16_UNorm);
    const auto sourceType = isFloat16 ? QOpenGLTexture::Float16 : QOpenGLTexture::UInt16;
    texture->allocateStorage(QOpenGLTexture::RGBA, sourceType);
    if (!texture->isStorageAllocated()) {
      return false;
    }

    const int sampleBytes = heiran::infra::bytesPerSample(buffer.sampleType);
    QOpenGLPixelTransferOptions options;
    options.setAlignment(1);
    const qsizetype rowBytes =
        static_cast<qsizetype>(buffer.width) * buffer.channelCount * sampleBytes;
    if (buffer.bytesPerRow != rowBytes) {
      // 行に詰め物がある場合に、1 行が何画素分あるかを伝える。
      options.setRowLength(
          static_cast<int>(buffer.bytesPerRow / (buffer.channelCount * sampleBytes)));
    }
    // 3 チャネルで渡すと alpha は 1.0 になる。詰め直しの経路と同じ扱い。
    const auto sourceFormat = buffer.channelCount >= 4 ? QOpenGLTexture::RGBA : QOpenGLTexture::RGB;
    texture->setData(sourceFormat, sourceType, buffer.data(), &options);
  } else {
    // 浮動小数は 1.0 を超える値を持つため、正規化整数では表せない。fp16 へ詰め直す。
    // 型を合わせずにドライバへ変換させると、上と同じ理由で遅くなる。
    const QVector<qfloat16> packed = heiran::infra::packRgbaFloat16(buffer);
    if (packed.isEmpty()) {
      return false;
    }

    texture->setFormat(QOpenGLTexture::RGBA16F);
    texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::Float16);
    if (!texture->isStorageAllocated()) {
      return false;
    }
    texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::Float16, packed.constData());
  }

  texture->generateMipMaps();
  texture->setWrapMode(QOpenGLTexture::ClampToEdge);
  texture_ = texture.get();
  // どちらの形式も 1 テクセル 8 バイト。
  textureBytes_ = textureBytesFor(QSize(buffer.width, buffer.height), 8);
  textureCache_.insert(currentTextureKey_, {.texture = std::move(texture), .bytes = textureBytes_});
  cachedTextureBytes_ += textureBytes_;
  return true;
}

// 画素データを見分ける鍵。ファイルの経路では、同じファイルを別のセルで開いた
// 場合や再読込で中身が変わった場合に取り違える。
QString GpuImageRenderer::textureKeyFor(const heiran::infra::DecodedImage &decodedImage) {
  const auto *storage = decodedImage.analysisBuffer.storage.data();
  return QString("%1|%2|%3")
      .arg(decodedImage.displayImage.cacheKey())
      .arg(reinterpret_cast<quintptr>(storage))
      .arg(decodedImage.analysisBuffer.byteCount());
}

void GpuImageRenderer::noteTextureUsed(const QString &key, qint64 bytes) {
  // 捨てると決められていたものを使い直す場合は、消す予定を取り消す。
  evictedKeys_.removeAll(key);
  notingTextureUsed_ = true;
  ledger_->markUsed(this, key, bytes);
  notingTextureUsed_ = false;
  // 自分の分は今消せる。他のセルの分は、そのセルが次に描くときに消える。
  dropEvictedTextures();
}

void GpuImageRenderer::dropTexture(const QString &key) {
  const auto it = textureCache_.find(key);
  if (it == textureCache_.end()) {
    return;
  }
  cachedTextureBytes_ -= it->bytes;
  textureCache_.erase(it);
}

void GpuImageRenderer::dropEvictedTextures() {
  for (const QString &key : std::as_const(evictedKeys_)) {
    if (key != currentTextureKey_) {
      dropTexture(key);
    }
  }
  evictedKeys_.clear();
}

bool GpuImageRenderer::ensureTexture() {
  // 他のセルの都合で捨てると決められた分を、描く機会に消す。
  dropEvictedTextures();
  if (!textureDirty_) {
    return texture_ != nullptr;
  }

  textureDirty_ = false;
  texture_ = nullptr;
  textureBytes_ = 0;
  currentTextureKey_ = textureKeyFor(decodedImage_);

  // 載せ済みなら作り直さない。ページを戻したときの送りがこれで速くなる。
  const auto cached = textureCache_.find(currentTextureKey_);
  if (cached != textureCache_.end()) {
    texture_ = cached->texture.get();
    textureBytes_ = cached->bytes;
    noteTextureUsed(currentTextureKey_, textureBytes_);
    return texture_ != nullptr;
  }

  // 解析用バッファがある画像は元の精度で載せる。8bit へ丸めてから載せると
  // 露出を上げても 1.0 を超える領域を復元できない。
  if (createFloatTexture()) {
    noteTextureUsed(currentTextureKey_, textureBytes_);
    return true;
  }

  const QImage &sourceImage = decodedImage_.displayImage;
  if (sourceImage.isNull()) {
    return false;
  }

  auto texture = createUInt8Texture(sourceImage);
  if (texture == nullptr) {
    return false;
  }
  texture->setWrapMode(QOpenGLTexture::ClampToEdge);
  texture_ = texture.get();
  textureBytes_ = textureBytesFor(sourceImage.size(), 4);
  textureCache_.insert(currentTextureKey_, {.texture = std::move(texture), .bytes = textureBytes_});
  cachedTextureBytes_ += textureBytes_;
  noteTextureUsed(currentTextureKey_, textureBytes_);
  return true;
}

void GpuImageRenderer::draw(const DisplayTransformParams &params, const QRectF &targetRect,
                            const QSize &widgetSize, double devicePixelRatio, bool smooth,
                            const TransparencyBackground &background) {
  if (!initialized_ || program_ == nullptr || !ensureTexture() || texture_ == nullptr) {
    return;
  }
  if (widgetSize.isEmpty() || targetRect.isEmpty()) {
    return;
  }

  // ウィジェット座標を正規化デバイス座標へ写す。
  const double width = widgetSize.width();
  const double height = widgetSize.height();
  const auto toNdcX = [width](double x) { return static_cast<float>(2.0 * x / width - 1.0); };
  const auto toNdcY = [height](double y) { return static_cast<float>(1.0 - 2.0 * y / height); };

  const std::array<QPointF, 4> texCorners = textureCorners(params);
  const std::array<QPointF, 4> positions{targetRect.topLeft(), targetRect.topRight(),
                                         targetRect.bottomLeft(), targetRect.bottomRight()};

  std::array<float, 16> vertexData{};
  for (int index = 0; index < 4; ++index) {
    vertexData[static_cast<size_t>(index) * 4 + 0] = toNdcX(positions[index].x());
    vertexData[static_cast<size_t>(index) * 4 + 1] = toNdcY(positions[index].y());
    vertexData[static_cast<size_t>(index) * 4 + 2] = static_cast<float>(texCorners[index].x());
    vertexData[static_cast<size_t>(index) * 4 + 3] = static_cast<float>(texCorners[index].y());
  }

  glViewport(0, 0, static_cast<GLsizei>(std::lround(width * devicePixelRatio)),
             static_cast<GLsizei>(std::lround(height * devicePixelRatio)));
  glDisable(GL_DEPTH_TEST);
  // 下地との合成はシェーダ側で済ませるため、不透明として書き込む。
  glDisable(GL_BLEND);

  program_->bind();
  vertexBuffer_.bind();
  vertexBuffer_.allocate(vertexData.data(), static_cast<int>(vertexData.size() * sizeof(float)));

  const int positionLocation = program_->attributeLocation("position");
  const int texCoordLocation = program_->attributeLocation("texCoord");
  program_->enableAttributeArray(positionLocation);
  program_->enableAttributeArray(texCoordLocation);
  program_->setAttributeBuffer(positionLocation, GL_FLOAT, 0, 2, 4 * sizeof(float));
  program_->setAttributeBuffer(texCoordLocation, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

  const double gamma = params.gamma <= 0.0 ? 1.0 : params.gamma;
  const bool checkerboard = background.pattern == TransparencyPattern::Checkerboard;
  const QColor backdropColor = checkerboard ? background.checkerColor1 : background.solidColor;
  const QColor alternateColor = background.checkerColor2;
  program_->setUniformValue("alternateBackdrop",
                            QVector3D(static_cast<float>(alternateColor.redF()),
                                      static_cast<float>(alternateColor.greenF()),
                                      static_cast<float>(alternateColor.blueF())));
  program_->setUniformValue(
      "checkerSize",
      checkerboard ? static_cast<float>(kTransparencyCheckerSize * devicePixelRatio) : 0.0F);
  program_->setUniformValue(
      "viewportHeight", static_cast<float>(std::lround(widgetSize.height() * devicePixelRatio)));
  program_->setUniformValue("backdrop", QVector3D(static_cast<float>(backdropColor.redF()),
                                                  static_cast<float>(backdropColor.greenF()),
                                                  static_cast<float>(backdropColor.blueF())));
  program_->setUniformValue("gain", static_cast<float>(std::pow(2.0, params.exposureStops)));
  program_->setUniformValue("gammaExponent", static_cast<float>(1.0 / gamma));
  program_->setUniformValue("channelMode", channelModeIndex(params.channelMode));
  program_->setUniformValue("invert", params.invert ? 1 : 0);
  program_->setUniformValue("lumaWeights", QVector3D(static_cast<float>(params.lumaWeights.red),
                                                     static_cast<float>(params.lumaWeights.green),
                                                     static_cast<float>(params.lumaWeights.blue)));

  const auto &colorTransform = params.colorTransform;
  program_->setUniformValue("colorManaged", params.colorManaged ? 1 : 0);
  program_->setUniformValue("sourceTransfer", static_cast<int>(colorTransform.sourceTransfer));
  program_->setUniformValue("sourceGamma", colorTransform.sourceGamma);
  program_->setUniformValue("displayTransfer", static_cast<int>(colorTransform.displayTransfer));
  program_->setUniformValue("displayGamma", colorTransform.displayGamma);
  // チャネルを 1 つだけ出しているときは原色を移さない。値を並べ替えた偽色表示で
  // あって色ではないため、移すと意味の無い色が付く。
  const bool applyPrimaries =
      !colorTransform.primariesMatch && params.channelMode == ChannelMode::Color;
  program_->setUniformValue("applyPrimaries", applyPrimaries ? 1 : 0);
  program_->setUniformValue("primaryMatrix", QMatrix3x3(colorTransform.primaryMatrix.data()));

  // 対応表もチャネルをまたぐため、色として出しているときだけ引く。
  const bool useColorLut = params.colorManaged && ensureColorLutTexture(params.colorLut) &&
                           params.channelMode == ChannelMode::Color;
  program_->setUniformValue("useColorLut", useColorLut ? 1 : 0);
  if (useColorLut) {
    // テクセルの中心を通るようにする。端の値がそのまま出る形にしないと、
    // 表の両端が半テクセル分内側へずれる。
    const float size = static_cast<float>(params.colorLut->size);
    program_->setUniformValue("lutScale", (size - 1.0F) / size);
    program_->setUniformValue("lutOffset", 0.5F / size);
    program_->setUniformValue("colorLut", 1);
    colorLutTexture_->bind(1);
  }
  program_->setUniformValue("source", 0);

  texture_->setMinificationFilter(smooth ? QOpenGLTexture::LinearMipMapLinear
                                         : QOpenGLTexture::Nearest);
  texture_->setMagnificationFilter(smooth ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest);
  texture_->bind(0);

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  if (useColorLut) {
    colorLutTexture_->release(1);
  }
  texture_->release();
  program_->disableAttributeArray(positionLocation);
  program_->disableAttributeArray(texCoordLocation);
  vertexBuffer_.release();
  program_->release();
}

// 対応表を 3D テクスチャとして載せる。同じ表なら載せ直さない。
bool GpuImageRenderer::ensureColorLutTexture(const std::shared_ptr<const ColorLut> &colorLut) {
  if (!colorLut || !colorLut->isValid()) {
    return false;
  }
  if (colorLutTexture_ && colorLutSource_ == colorLut) {
    return true;
  }

  // 3D テクスチャは OpenGL ES 2.0 には無い。使えない場合は表の経路を諦める。
  const QOpenGLContext *context = QOpenGLContext::currentContext();
  if (context == nullptr || context->isOpenGLES()) {
    return false;
  }

  auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target3D);
  texture->setFormat(QOpenGLTexture::RGB16F);
  texture->setSize(colorLut->size, colorLut->size, colorLut->size);
  texture->setMipLevels(1);
  texture->allocateStorage(QOpenGLTexture::RGB, QOpenGLTexture::Float32);
  if (!texture->isStorageAllocated()) {
    return false;
  }

  texture->setData(QOpenGLTexture::RGB, QOpenGLTexture::Float32, colorLut->values.constData());
  // 表の間は線形に補間する。CPU 側の三重線形補間と同じ引き方になる。
  texture->setMinificationFilter(QOpenGLTexture::Linear);
  texture->setMagnificationFilter(QOpenGLTexture::Linear);
  texture->setWrapMode(QOpenGLTexture::ClampToEdge);

  colorLutTexture_ = std::move(texture);
  colorLutSource_ = colorLut;
  return true;
}

void GpuImageRenderer::release() {
  texture_ = nullptr;
  currentTextureKey_.clear();
  textureCache_.clear();
  evictedKeys_.clear();
  ledger_->clearOwner(this);
  cachedTextureBytes_ = 0;
  colorLutTexture_.reset();
  colorLutSource_.reset();
  textureBytes_ = 0;
  program_.reset();
  if (vertexBuffer_.isCreated()) {
    vertexBuffer_.destroy();
  }
  initialized_ = false;
  textureDirty_ = true;
}

}  // namespace heiran::ui
