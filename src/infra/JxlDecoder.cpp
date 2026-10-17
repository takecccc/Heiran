#include "heiran/infra/JxlDecoder.hpp"

#include <QFile>
#include <QThread>
#include <jxl/decode.h>
#include <jxl/resizable_parallel_runner.h>
#include <memory>
#include <vector>

namespace heiran::infra {

namespace {

struct DecoderDeleter {
  void operator()(JxlDecoder *decoder) const { JxlDecoderDestroy(decoder); }
};

struct RunnerDeleter {
  void operator()(void *runner) const { JxlResizableParallelRunnerDestroy(runner); }
};

JxlResult failure(const QString &message) {
  JxlResult result;
  result.errorMessage = message;
  return result;
}

// enum で指定された色空間を、Qt の色空間へ。対応しない組み合わせは無効。
QColorSpace colorSpaceFromEncoding(const JxlColorEncoding &encoding) {
  if (encoding.color_space != JXL_COLOR_SPACE_RGB) {
    return {};
  }

  QColorSpace::Primaries primaries = QColorSpace::Primaries::SRgb;
  switch (encoding.primaries) {
    case JXL_PRIMARIES_SRGB:
      primaries = QColorSpace::Primaries::SRgb;
      break;
    case JXL_PRIMARIES_P3:
      primaries = QColorSpace::Primaries::DciP3D65;
      break;
    case JXL_PRIMARIES_2100:
      primaries = QColorSpace::Primaries::Bt2020;
      break;
    default:
      return {};
  }

  QColorSpace::TransferFunction transfer = QColorSpace::TransferFunction::SRgb;
  switch (encoding.transfer_function) {
    case JXL_TRANSFER_FUNCTION_SRGB:
    case JXL_TRANSFER_FUNCTION_709:
      transfer = QColorSpace::TransferFunction::SRgb;
      break;
    case JXL_TRANSFER_FUNCTION_LINEAR:
      transfer = QColorSpace::TransferFunction::Linear;
      break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    case JXL_TRANSFER_FUNCTION_PQ:
      transfer = QColorSpace::TransferFunction::St2084;
      break;
    case JXL_TRANSFER_FUNCTION_HLG:
      transfer = QColorSpace::TransferFunction::Hlg;
      break;
#endif
    default:
      return {};
  }
  return QColorSpace(primaries, transfer);
}

}  // namespace

JxlResult decodeJxl(const QString &filePath) {
  // ファイルは全部読んでから渡す。日本語などのパスを、自前の文字変換なしで扱える。
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return failure(QStringLiteral("Failed to open the file"));
  }
  const QByteArray bytes = file.readAll();
  if (bytes.isEmpty()) {
    return failure(QStringLiteral("The file is empty"));
  }
  const auto *data = reinterpret_cast<const uint8_t *>(bytes.constData());
  const size_t size = static_cast<size_t>(bytes.size());

  if (JxlSignatureCheck(data, size) == JXL_SIG_INVALID) {
    return failure(QStringLiteral("Not a JPEG XL file"));
  }

  std::unique_ptr<JxlDecoder, DecoderDeleter> decoder(JxlDecoderCreate(nullptr));
  std::unique_ptr<void, RunnerDeleter> runner(JxlResizableParallelRunnerCreate(nullptr));
  if (decoder == nullptr || runner == nullptr) {
    return failure(QStringLiteral("Failed to create the JPEG XL decoder"));
  }

  if (JxlDecoderSetParallelRunner(decoder.get(), JxlResizableParallelRunner, runner.get()) !=
      JXL_DEC_SUCCESS) {
    return failure(QStringLiteral("Failed to set up the parallel runner"));
  }
  const int events = JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE;
  if (JxlDecoderSubscribeEvents(decoder.get(), events) != JXL_DEC_SUCCESS) {
    return failure(QStringLiteral("Failed to subscribe to the decoder events"));
  }
  // アルファは、掛け済みではなく、掛けていない値で受け取る (他の形式と同じ)。
  JxlDecoderSetUnpremultiplyAlpha(decoder.get(), JXL_TRUE);

  if (JxlDecoderSetInput(decoder.get(), data, size) != JXL_DEC_SUCCESS) {
    return failure(QStringLiteral("Failed to pass the input"));
  }
  JxlDecoderCloseInput(decoder.get());

  JxlBasicInfo info{};
  bool haveInfo = false;
  JxlPixelFormat format{};
  QImage image;
  QByteArray icc;
  QColorSpace declared;

  while (true) {
    const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
    if (status == JXL_DEC_ERROR) {
      return failure(QStringLiteral("The JPEG XL data is corrupt"));
    }
    if (status == JXL_DEC_NEED_MORE_INPUT) {
      return failure(QStringLiteral("The JPEG XL data is truncated"));
    }

    if (status == JXL_DEC_BASIC_INFO) {
      if (JxlDecoderGetBasicInfo(decoder.get(), &info) != JXL_DEC_SUCCESS) {
        return failure(QStringLiteral("Failed to read the image header"));
      }
      haveInfo = true;
      JxlResizableParallelRunnerSetThreads(
          runner.get(),
          std::min<size_t>(JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize),
                           static_cast<size_t>(QThread::idealThreadCount())));
    } else if (status == JXL_DEC_COLOR_ENCODING) {
      // enum で指定された色空間なら、それを使う。ICC で指定されていれば、プロファイルを取る。
      JxlColorEncoding encoding{};
      if (JxlDecoderGetColorAsEncodedProfile(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                             &encoding) == JXL_DEC_SUCCESS) {
        declared = colorSpaceFromEncoding(encoding);
      } else {
        size_t iccSize = 0;
        if (JxlDecoderGetICCProfileSize(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA, &iccSize) ==
                JXL_DEC_SUCCESS &&
            iccSize > 0) {
          icc.resize(static_cast<qsizetype>(iccSize));
          if (JxlDecoderGetColorAsICCProfile(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                             reinterpret_cast<uint8_t *>(icc.data()),
                                             iccSize) != JXL_DEC_SUCCESS) {
            icc.clear();
          }
        }
      }
    } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      if (!haveInfo) {
        return failure(QStringLiteral("The image header was missing"));
      }
      // 8bit を超える精度 (浮動小数を含む) は、16bit の整数で受ける。
      const bool highPrecision = info.bits_per_sample > 8;
      format.num_channels = 4;
      format.data_type = highPrecision ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8;
      format.endianness = JXL_NATIVE_ENDIAN;
      format.align = 0;

      image = QImage(static_cast<int>(info.xsize), static_cast<int>(info.ysize),
                     highPrecision ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
      if (image.isNull()) {
        return failure(QStringLiteral("Failed to allocate the image"));
      }
      size_t bufferSize = 0;
      if (JxlDecoderImageOutBufferSize(decoder.get(), &format, &bufferSize) != JXL_DEC_SUCCESS ||
          bufferSize > static_cast<size_t>(image.sizeInBytes())) {
        return failure(QStringLiteral("The output buffer size does not match"));
      }
      // 行の端に隙間 (QImage は 4 バイト境界) があっても、RGBA の 4 バイト、または 8 バイトの
      // 並びは隙間が出ない。そのまま渡せる。
      if (JxlDecoderSetImageOutBuffer(decoder.get(), &format, image.bits(), bufferSize) !=
          JXL_DEC_SUCCESS) {
        return failure(QStringLiteral("Failed to set the output buffer"));
      }
    } else if (status == JXL_DEC_FULL_IMAGE) {
      // 先頭の 1 コマが出来た。アニメーションでも、ここで止める。
      break;
    } else if (status == JXL_DEC_SUCCESS) {
      break;
    }
  }

  if (image.isNull() || !haveInfo) {
    return failure(QStringLiteral("The file has no image"));
  }

  const bool hasAlpha = info.alpha_bits > 0;
  if (!hasAlpha) {
    image = image.convertToFormat(info.bits_per_sample > 8 ? QImage::Format_RGBX64
                                                           : QImage::Format_RGBX8888);
  }

  JxlResult result;
  result.ok = true;
  result.image = std::move(image);
  result.bitDepth = static_cast<int>(info.bits_per_sample);
  result.hasAlpha = hasAlpha;
  result.iccProfile = std::move(icc);
  result.declaredColorSpace = declared;
  result.isAnimation = info.have_animation != JXL_FALSE;
  result.wasFloatingPoint = info.exponent_bits_per_sample > 0;
  return result;
}

}  // namespace heiran::infra
