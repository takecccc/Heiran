#include "heiran/infra/WebpDecoder.hpp"

#include <QFile>
#include <QVector>
#include <webp/decode.h>
#include <webp/demux.h>

namespace heiran::infra {

namespace {

class WebpSource final : public AnimationSource {
public:
  struct Frame {
    AnimationFrameControl control;
    // ファイル全体の中での、フレームの画素のデータ (ALPH と VP8 / VP8L) の位置。
    qsizetype offset = 0;
    qsizetype size = 0;
  };

  WebpSource(QByteArray data, int width, int height, QVector<Frame> frames)
      : data_(std::move(data)), width_(width), height_(height), frames_(std::move(frames)) {}

  int canvasWidth() const override { return width_; }
  int canvasHeight() const override { return height_; }
  int frameCount() const override { return static_cast<int>(frames_.size()); }
  const AnimationFrameControl &control(int index) const override {
    return frames_.at(index).control;
  }

  QImage decodeFrame(int index) const override {
    const Frame &frame = frames_.at(index);
    QImage image(static_cast<int>(frame.control.width), static_cast<int>(frame.control.height),
                 QImage::Format_RGBA8888);
    if (image.isNull()) {
      return {};
    }

    // アルファを掛けない RGBA へ、用意した記憶域へ直接復号する。
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
      return {};
    }
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = image.bits();
    config.output.u.RGBA.stride = static_cast<int>(image.bytesPerLine());
    config.output.u.RGBA.size = static_cast<size_t>(image.sizeInBytes());
    const auto *bytes = reinterpret_cast<const uint8_t *>(data_.constData()) + frame.offset;
    if (WebPDecode(bytes, static_cast<size_t>(frame.size), &config) != VP8_STATUS_OK) {
      return {};
    }
    return image;
  }

  qint64 compressedBytes() const override { return data_.size(); }

private:
  const QByteArray data_;
  const int width_;
  const int height_;
  const QVector<Frame> frames_;
};

}  // namespace

std::shared_ptr<const AnimationSource> openWebpSource(const QString &filePath,
                                                      QString *errorMessage) {
  const auto fail = [errorMessage](const QString &text) {
    if (errorMessage != nullptr) {
      *errorMessage = text;
    }
    return std::shared_ptr<const AnimationSource>();
  };

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return fail(file.errorString());
  }
  QByteArray bytes = file.readAll();
  file.close();

  const WebPData webpData{reinterpret_cast<const uint8_t *>(bytes.constData()),
                          static_cast<size_t>(bytes.size())};
  WebPDemuxer *demuxer = WebPDemux(&webpData);
  if (demuxer == nullptr) {
    return fail(QStringLiteral("Not a valid WebP file"));
  }

  const int width = static_cast<int>(WebPDemuxGetI(demuxer, WEBP_FF_CANVAS_WIDTH));
  const int height = static_cast<int>(WebPDemuxGetI(demuxer, WEBP_FF_CANVAS_HEIGHT));
  QVector<WebpSource::Frame> frames;
  WebPIterator iterator;
  if (WebPDemuxGetFrame(demuxer, 1, &iterator)) {
    do {
      WebpSource::Frame frame;
      frame.control.width = static_cast<quint32>(iterator.width);
      frame.control.height = static_cast<quint32>(iterator.height);
      frame.control.xOffset = static_cast<quint32>(iterator.x_offset);
      frame.control.yOffset = static_cast<quint32>(iterator.y_offset);
      frame.control.delayMs = iterator.duration;
      // WebP の disposal は なし / 背景 の 2 つ。背景は透明に戻す (既定の扱い)。
      frame.control.disposeOp = iterator.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND
                                    ? kAnimationDisposeBackground
                                    : kAnimationDisposeNone;
      // 重ねる (アルファで混ぜる) か、混ぜずに置き換えるか。
      frame.control.blendOp =
          iterator.blend_method == WEBP_MUX_BLEND ? kAnimationBlendOver : kAnimationBlendSource;
      frame.offset = static_cast<qsizetype>(iterator.fragment.bytes -
                                            reinterpret_cast<const uint8_t *>(bytes.constData()));
      frame.size = static_cast<qsizetype>(iterator.fragment.size);
      frames.push_back(frame);
    } while (WebPDemuxNextFrame(&iterator));
    WebPDemuxReleaseIterator(&iterator);
  }
  WebPDemuxDelete(demuxer);

  if (width <= 0 || height <= 0 || frames.isEmpty()) {
    return fail(QStringLiteral("The WebP file has no frames"));
  }
  return std::make_shared<WebpSource>(std::move(bytes), width, height, std::move(frames));
}

}  // namespace heiran::infra
