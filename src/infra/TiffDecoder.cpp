#include "heiran/infra/TiffDecoder.hpp"

#include "heiran/infra/CmykConversion.hpp"
#include "heiran/infra/ParallelRows.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <tiffio.h>
#include <vector>

namespace heiran::infra {

namespace {

// libtiff の失敗の文言を、ファイルごとに受ける (全体の既定の出力先は使わない)。
struct ErrorLog {
  QString lastError;
};

int captureError(TIFF *, void *userData, const char *module, const char *format,
                 va_list arguments) {
  auto *log = static_cast<ErrorLog *>(userData);
  char buffer[512];
  std::vsnprintf(buffer, sizeof(buffer), format, arguments);
  log->lastError = module != nullptr ? QString("%1: %2").arg(module, buffer) : QString(buffer);
  return 1;
}

int ignoreWarning(TIFF *, void *, const char *, const char *, va_list) {
  return 1;
}

struct TiffDeleter {
  void operator()(TIFF *tiff) const {
    if (tiff != nullptr) {
      TIFFClose(tiff);
    }
  }
};

struct OptionsDeleter {
  void operator()(TIFFOpenOptions *options) const { TIFFOpenOptionsFree(options); }
};

TiffResult failure(const QString &message) {
  TiffResult result;
  result.raw = rawImageFailure(message);
  return result;
}

// ---- 向き ----

// 元の画素 (sx, sy) の、向きを直した後の位置。`width` と `height` は元の大きさ。
void mapOrientation(int orientation, int width, int height, int sx, int sy, int *dx, int *dy) {
  switch (orientation) {
    case 2:
      *dx = width - 1 - sx;
      *dy = sy;
      break;
    case 3:
      *dx = width - 1 - sx;
      *dy = height - 1 - sy;
      break;
    case 4:
      *dx = sx;
      *dy = height - 1 - sy;
      break;
    case 5:
      *dx = sy;
      *dy = sx;
      break;
    case 6:
      *dx = height - 1 - sy;
      *dy = sx;
      break;
    case 7:
      *dx = height - 1 - sy;
      *dy = width - 1 - sx;
      break;
    case 8:
      *dx = sy;
      *dy = width - 1 - sx;
      break;
    case 1:
    default:
      *dx = sx;
      *dy = sy;
      break;
  }
}

// ---- 読み方の判断 ----

struct Layout {
  int width = 0;
  int height = 0;
  int bitsPerSample = 0;
  int samplesPerPixel = 0;
  int photometric = 0;
  int planar = PLANARCONFIG_CONTIG;
  int sampleFormat = SAMPLEFORMAT_UINT;
  int orientation = 1;
  bool tiled = false;
  int extraSamples = 0;
  uint16_t firstExtraType = EXTRASAMPLE_UNSPECIFIED;
};

Layout readLayout(TIFF *tiff) {
  Layout layout;
  uint32_t width = 0;
  uint32_t height = 0;
  TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
  layout.width = static_cast<int>(width);
  layout.height = static_cast<int>(height);

  uint16_t value = 0;
  layout.bitsPerSample = TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &value) ? value : 1;
  layout.samplesPerPixel = TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &value) ? value : 1;
  layout.photometric =
      TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &value) ? value : PHOTOMETRIC_MINISBLACK;
  layout.planar =
      TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &value) ? value : PLANARCONFIG_CONTIG;
  layout.sampleFormat =
      TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &value) ? value : SAMPLEFORMAT_UINT;
  layout.orientation = TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &value) ? value : 1;
  layout.tiled = TIFFIsTiled(tiff) != 0;

  uint16_t extraCount = 0;
  uint16_t *extraTypes = nullptr;
  if (TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes) && extraCount > 0 &&
      extraTypes != nullptr) {
    layout.extraSamples = extraCount;
    layout.firstExtraType = extraTypes[0];
  }
  return layout;
}

// そのまま読める形か。出力のチャンネル数と、標本の型を返す。
bool directLayout(const Layout &layout, int *channels, PixelSampleType *sampleType) {
  const bool gray =
      layout.photometric == PHOTOMETRIC_MINISBLACK || layout.photometric == PHOTOMETRIC_MINISWHITE;
  const bool rgb = layout.photometric == PHOTOMETRIC_RGB;
  if (!gray && !rgb) {
    return false;
  }

  // チャンネル数。グレーは 1 か 2 (アルファ)、RGB は 3 か 4 (アルファ)。それ以外は直に読まない。
  const int colorChannels = rgb ? 3 : 1;
  if (layout.samplesPerPixel != colorChannels && layout.samplesPerPixel != colorChannels + 1) {
    return false;
  }
  *channels = layout.samplesPerPixel;

  if (layout.bitsPerSample == 8 && layout.sampleFormat == SAMPLEFORMAT_UINT) {
    *sampleType = PixelSampleType::UInt8;
    return true;
  }
  if (layout.bitsPerSample == 16 && layout.sampleFormat == SAMPLEFORMAT_UINT) {
    *sampleType = PixelSampleType::UInt16;
    return true;
  }
  if (layout.bitsPerSample == 16 && layout.sampleFormat == SAMPLEFORMAT_IEEEFP) {
    *sampleType = PixelSampleType::Float16;
    return layout.photometric != PHOTOMETRIC_MINISWHITE;
  }
  if (layout.bitsPerSample == 32 && layout.sampleFormat == SAMPLEFORMAT_IEEEFP) {
    *sampleType = PixelSampleType::Float32;
    return layout.photometric != PHOTOMETRIC_MINISWHITE;
  }
  return false;
}

// ファイルを開き、指定のページを選ぶ。失敗したら null。
std::unique_ptr<TIFF, TiffDeleter> openPage(const QString &filePath, int pageIndex, ErrorLog *log) {
  std::unique_ptr<TIFFOpenOptions, OptionsDeleter> options(TIFFOpenOptionsAlloc());
  if (options == nullptr) {
    return nullptr;
  }
  TIFFOpenOptionsSetErrorHandlerExtR(options.get(), captureError, log);
  TIFFOpenOptionsSetWarningHandlerExtR(options.get(), ignoreWarning, nullptr);
  // Windows では、ワイド文字のパスで開く (日本語のパスを扱うため)。
  std::unique_ptr<TIFF, TiffDeleter> tiff(
      TIFFOpenWExt(reinterpret_cast<const wchar_t *>(filePath.utf16()), "r", options.get()));
  if (tiff != nullptr && pageIndex > 0 &&
      TIFFSetDirectory(tiff.get(), static_cast<tdir_t>(pageIndex)) == 0) {
    return nullptr;
  }
  return tiff;
}

// ---- そのまま読む ----

// 1 ページを、出力の並び (画素ごとにチャンネル) へ読む。
bool readDirect(TIFF *tiff, const QString &filePath, int pageIndex, const Layout &layout,
                int channels, PixelSampleType sampleType, RawImage *raw, QString *error) {
  const size_t sampleBytes = static_cast<size_t>(bytesPerSample(sampleType));
  const bool separate = layout.planar == PLANARCONFIG_SEPARATE && layout.samplesPerPixel > 1;
  const bool invert = layout.photometric == PHOTOMETRIC_MINISWHITE;
  const int colorChannels = layout.photometric == PHOTOMETRIC_RGB ? 3 : 1;

  raw->width = layout.width;
  raw->height = layout.height;
  raw->channels = channels;
  raw->sampleType = sampleType;
  const qsizetype totalBytes = static_cast<qsizetype>(layout.width) * layout.height * channels *
                               static_cast<qsizetype>(sampleBytes);
  raw->pixels.resize(totalBytes);
  if (raw->pixels.size() != totalBytes) {
    *error = QStringLiteral("Failed to allocate the image");
    return false;
  }
  auto *output = reinterpret_cast<uchar *>(raw->pixels.data());
  const size_t pixelBytes = static_cast<size_t>(channels) * sampleBytes;
  // 平面が 1 つで、反転が要らず、読んだ並びが出力と同じなら、一括でコピーできる。
  const bool fastCopy = !separate && !invert && channels == layout.samplesPerPixel;

  // 「最小が白」は、色のチャンネルだけ反転する (アルファはそのまま)。
  const auto store = [&](int x, int y, int channel, const uchar *sample) {
    uchar *target =
        output + ((static_cast<size_t>(y) * layout.width + x) * channels + channel) * sampleBytes;
    if (invert && channel < colorChannels) {
      if (sampleType == PixelSampleType::UInt8) {
        target[0] = static_cast<uchar>(255 - sample[0]);
        return;
      }
      quint16 value = 0;
      std::memcpy(&value, sample, sizeof(value));
      value = static_cast<quint16>(65535 - value);
      std::memcpy(target, &value, sizeof(value));
      return;
    }
    std::memcpy(target, sample, sampleBytes);
  };

  const int planes = separate ? channels : 1;

  // 読む単位 (ストリップ、またはタイル) を並べ、帯に分けて並列に読む。
  // 圧縮の展開が時間の大半を占めるため、帯ごとに自分のハンドルで開く (ハンドルは、
  // スレッドをまたいで共有できない)。
  uint32_t tileWidth = 0;
  uint32_t tileHeight = 0;
  uint32_t rowsPerStrip = 0;
  uint32_t unitsPerPlane = 0;
  uint32_t unitsAcross = 1;
  if (layout.tiled) {
    TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tileWidth);
    TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tileHeight);
    if (tileWidth == 0 || tileHeight == 0) {
      *error = QStringLiteral("The tile size is invalid");
      return false;
    }
    unitsAcross = (static_cast<uint32_t>(layout.width) + tileWidth - 1) / tileWidth;
    unitsPerPlane =
        unitsAcross * ((static_cast<uint32_t>(layout.height) + tileHeight - 1) / tileHeight);
  } else {
    TIFFGetFieldDefaulted(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    if (rowsPerStrip == 0 || rowsPerStrip > static_cast<uint32_t>(layout.height)) {
      rowsPerStrip = static_cast<uint32_t>(layout.height);
    }
    unitsPerPlane = (static_cast<uint32_t>(layout.height) + rowsPerStrip - 1) / rowsPerStrip;
  }
  const int unitCount = static_cast<int>(unitsPerPlane) * planes;

  // 1 つの単位を読んで、出力へ置く。
  const auto readUnit = [&](TIFF *handle, std::vector<uchar> *buffer, int unit) {
    const int plane = unit / static_cast<int>(unitsPerPlane);
    const uint32_t index = static_cast<uint32_t>(unit) % unitsPerPlane;
    if (!layout.tiled) {
      const uint32_t firstRow = index * rowsPerStrip;
      const uint32_t lastRow =
          std::min<uint32_t>(firstRow + rowsPerStrip, static_cast<uint32_t>(layout.height));
      for (uint32_t row = firstRow; row < lastRow; ++row) {
        const int y = static_cast<int>(row);
        if (TIFFReadScanline(handle, buffer->data(), row, static_cast<uint16_t>(plane)) < 0) {
          return false;
        }
        const uchar *line = buffer->data();
        if (fastCopy) {
          // 詰め方が出力と同じなら、行ごとに一括でコピーする。
          std::memcpy(output + static_cast<size_t>(y) * layout.width * pixelBytes, line,
                      static_cast<size_t>(layout.width) * pixelBytes);
        } else if (separate) {
          for (int x = 0; x < layout.width; ++x) {
            store(x, y, plane, line + static_cast<size_t>(x) * sampleBytes);
          }
        } else {
          for (int x = 0; x < layout.width; ++x) {
            for (int c = 0; c < channels; ++c) {
              store(x, y, c,
                    line + (static_cast<size_t>(x) * layout.samplesPerPixel + c) * sampleBytes);
            }
          }
        }
      }
      return true;
    }

    const uint32_t tileX = (index % unitsAcross) * tileWidth;
    const uint32_t tileY = (index / unitsAcross) * tileHeight;
    if (TIFFReadTile(handle, buffer->data(), tileX, tileY, 0, static_cast<uint16_t>(plane)) < 0) {
      return false;
    }
    const uint32_t usableWidth = std::min<uint32_t>(tileWidth, layout.width - tileX);
    const uint32_t usableHeight = std::min<uint32_t>(tileHeight, layout.height - tileY);
    const uchar *tile = buffer->data();
    for (uint32_t row = 0; row < usableHeight; ++row) {
      if (fastCopy) {
        // 詰め方が出力と同じなら、タイルの 1 行ごとに一括でコピーする。
        std::memcpy(output +
                        ((static_cast<size_t>(tileY) + row) * layout.width + tileX) * pixelBytes,
                    tile + static_cast<size_t>(row) * tileWidth * pixelBytes,
                    static_cast<size_t>(usableWidth) * pixelBytes);
        continue;
      }
      for (uint32_t column = 0; column < usableWidth; ++column) {
        const int x = static_cast<int>(tileX + column);
        const int y = static_cast<int>(tileY + row);
        if (separate) {
          store(x, y, plane, tile + (static_cast<size_t>(row) * tileWidth + column) * sampleBytes);
        } else {
          for (int c = 0; c < channels; ++c) {
            store(x, y, c,
                  tile + ((static_cast<size_t>(row) * tileWidth + column) * layout.samplesPerPixel +
                          c) *
                             sampleBytes);
          }
        }
      }
    }
    return true;
  };

  const size_t bufferBytes = layout.tiled ? static_cast<size_t>(TIFFTileSize(tiff))
                                          : static_cast<size_t>(TIFFScanlineSize(tiff));
  const QString failedMessage = layout.tiled ? QStringLiteral("Failed to read a tile")
                                             : QStringLiteral("Failed to read a scanline");

  // 小さい画像は、ハンドルを開き直す費用のほうが大きいため、1 本で読む。
  constexpr qsizetype kParallelMinimumBytes = 4 * 1024 * 1024;
  if (unitCount <= 1 || totalBytes < kParallelMinimumBytes) {
    std::vector<uchar> buffer(bufferBytes);
    for (int unit = 0; unit < unitCount; ++unit) {
      if (!readUnit(tiff, &buffer, unit)) {
        *error = failedMessage;
        return false;
      }
    }
    return true;
  }

  std::atomic<bool> failed{false};
  QMutex errorMutex;
  QString failedError;
  const auto fail = [&](const QString &message) {
    failed = true;
    const QMutexLocker locker(&errorMutex);
    if (failedError.isEmpty()) {
      failedError = message;
    }
  };
  heiran::infra::forEachRowBand(unitCount, [&](int firstUnit, int lastUnit) {
    if (failed.load()) {
      return;
    }
    // 先頭の帯は、開いてあるハンドルを使う。他の帯は、自分で開く。
    ErrorLog log;
    std::unique_ptr<TIFF, TiffDeleter> own;
    TIFF *handle = tiff;
    if (firstUnit != 0) {
      own = openPage(filePath, pageIndex, &log);
      handle = own.get();
      if (handle == nullptr) {
        fail(log.lastError.isEmpty() ? QStringLiteral("Failed to open the file") : log.lastError);
        return;
      }
    }
    std::vector<uchar> buffer(bufferBytes);
    for (int unit = firstUnit; unit < lastUnit && !failed.load(); ++unit) {
      if (!readUnit(handle, &buffer, unit)) {
        fail(log.lastError.isEmpty() ? failedMessage : log.lastError);
      }
    }
  });
  if (failed) {
    *error = failedError;
    return false;
  }
  return true;
}

// ---- 8bit の RGBA へ変換して読む ----

bool readConverted(TIFF *tiff, const Layout &layout, RawImage *raw, QString *error) {
  char emsg[1024];
  if (TIFFRGBAImageOK(tiff, emsg) == 0) {
    *error = QString::fromLatin1(emsg);
    return false;
  }

  std::vector<uint32_t> raster(static_cast<size_t>(layout.width) * layout.height);
  // 向きは、libtiff には任せず (上を先頭の行で受け)、あとで一括して適用する。
  if (TIFFReadRGBAImageOriented(tiff, static_cast<uint32_t>(layout.width),
                                static_cast<uint32_t>(layout.height), raster.data(),
                                ORIENTATION_TOPLEFT, 0) == 0) {
    *error = QStringLiteral("Failed to convert the image");
    return false;
  }

  const bool hasAlpha = layout.extraSamples > 0;
  const int channels = hasAlpha ? 4 : 3;
  raw->width = layout.width;
  raw->height = layout.height;
  raw->channels = channels;
  raw->sampleType = PixelSampleType::UInt8;
  raw->pixels.resize(static_cast<qsizetype>(layout.width) * layout.height * channels);
  auto *output = reinterpret_cast<uchar *>(raw->pixels.data());
  for (size_t index = 0; index < raster.size(); ++index) {
    const uint32_t pixel = raster[index];
    uchar *target = output + index * static_cast<size_t>(channels);
    target[0] = static_cast<uchar>(TIFFGetR(pixel));
    target[1] = static_cast<uchar>(TIFFGetG(pixel));
    target[2] = static_cast<uchar>(TIFFGetB(pixel));
    if (hasAlpha) {
      target[3] = static_cast<uchar>(TIFFGetA(pixel));
    }
  }
  return true;
}

}  // namespace

void applyOrientationToRawImage(RawImage *image, int orientation) {
  if (image == nullptr || image->pixels.isEmpty() || orientation <= 1 || orientation > 8) {
    return;
  }
  const int width = image->width;
  const int height = image->height;
  const bool transposed = orientation >= 5;
  const int newWidth = transposed ? height : width;
  const int newHeight = transposed ? width : height;
  const size_t pixelBytes =
      static_cast<size_t>(image->channels) * static_cast<size_t>(bytesPerSample(image->sampleType));

  QByteArray result(image->pixels.size(), '\0');
  const char *source = image->pixels.constData();
  char *target = result.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int dx = 0;
      int dy = 0;
      mapOrientation(orientation, width, height, x, y, &dx, &dy);
      std::memcpy(target + (static_cast<size_t>(dy) * newWidth + dx) * pixelBytes,
                  source + (static_cast<size_t>(y) * width + x) * pixelBytes, pixelBytes);
    }
  }
  image->pixels = std::move(result);
  image->width = newWidth;
  image->height = newHeight;
}

TiffResult decodeTiff(const QString &filePath, int pageIndex, bool applyOrientation) {
  ErrorLog log;
  std::unique_ptr<TIFFOpenOptions, OptionsDeleter> options(TIFFOpenOptionsAlloc());
  if (options == nullptr) {
    return failure(QStringLiteral("Failed to set up libtiff"));
  }
  TIFFOpenOptionsSetErrorHandlerExtR(options.get(), captureError, &log);
  TIFFOpenOptionsSetWarningHandlerExtR(options.get(), ignoreWarning, nullptr);

  // Windows では、ワイド文字のパスで開く (日本語のパスを扱うため)。
  std::unique_ptr<TIFF, TiffDeleter> tiff(
      TIFFOpenWExt(reinterpret_cast<const wchar_t *>(filePath.utf16()), "r", options.get()));
  if (tiff == nullptr) {
    return failure(log.lastError.isEmpty() ? QStringLiteral("Failed to open the file")
                                           : log.lastError);
  }

  const int pageCount = static_cast<int>(TIFFNumberOfDirectories(tiff.get()));
  if (pageIndex < 0 || pageIndex >= pageCount) {
    return failure(QStringLiteral("The file has no such page"));
  }
  if (pageIndex > 0 && TIFFSetDirectory(tiff.get(), static_cast<tdir_t>(pageIndex)) == 0) {
    return failure(log.lastError.isEmpty() ? QStringLiteral("Failed to select the page")
                                           : log.lastError);
  }

  const Layout layout = readLayout(tiff.get());
  if (layout.width <= 0 || layout.height <= 0 ||
      static_cast<qint64>(layout.width) * layout.height > kMaxRawImagePixels) {
    return failure(QStringLiteral("The image size is invalid"));
  }

  TiffResult result;
  result.pageCount = pageCount;
  RawImage &raw = result.raw;
  QString error;

  // 埋め込みの ICC プロファイル (ページごと)。
  QByteArray icc;
  uint32_t iccSize = 0;
  const void *iccData = nullptr;
  if (TIFFGetField(tiff.get(), TIFFTAG_ICCPROFILE, &iccSize, &iccData) && iccSize > 0 &&
      iccData != nullptr) {
    icc = QByteArray(static_cast<const char *>(iccData), static_cast<qsizetype>(iccSize));
  }

  // CMYK (インクは C M Y K の 4 つ。アルファがあれば 5 つ目) の 8 / 16bit は、標本をそのまま読んで
  // sRGB へ変換する。プロファイルがあれば色を管理して、無ければ素朴な式で。
  uint16_t inkSet = INKSET_CMYK;
  TIFFGetFieldDefaulted(tiff.get(), TIFFTAG_INKSET, &inkSet);
  const bool directCmyk = layout.photometric == PHOTOMETRIC_SEPARATED && inkSet == INKSET_CMYK &&
                          (layout.samplesPerPixel == 4 || layout.samplesPerPixel == 5) &&
                          (layout.bitsPerSample == 8 || layout.bitsPerSample == 16) &&
                          layout.sampleFormat == SAMPLEFORMAT_UINT &&
                          layout.extraSamples == layout.samplesPerPixel - 4;

  int channels = 0;
  PixelSampleType sampleType = PixelSampleType::Unknown;
  bool ok = false;
  bool colorConverted = false;  // 出力が sRGB へ変換済み (プロファイルは付けない)。
  if (directCmyk) {
    sampleType = layout.bitsPerSample == 8 ? PixelSampleType::UInt8 : PixelSampleType::UInt16;
    const int sourceChannels = layout.samplesPerPixel;
    ok = readDirect(tiff.get(), filePath, pageIndex, layout, sourceChannels, sampleType, &raw,
                    &error);
    if (ok) {
      const int sampleBytes = layout.bitsPerSample / 8;
      const bool hasAlpha = sourceChannels == 5;
      const int outputChannels = hasAlpha ? 4 : 3;
      const qsizetype pixelCount = static_cast<qsizetype>(layout.width) * layout.height;
      // 変換の出力は RGB の 3 つ。アルファは、あとから足す。
      QByteArray converted(pixelCount * 3 * sampleBytes, 0);
      convertCmykToSrgb(icc, reinterpret_cast<const uchar *>(raw.pixels.constData()), layout.width,
                        layout.height, sampleBytes, sourceChannels, false,
                        reinterpret_cast<uchar *>(converted.data()));
      if (hasAlpha) {
        QByteArray withAlpha(pixelCount * outputChannels * sampleBytes, 0);
        for (qsizetype pixel = 0; pixel < pixelCount; ++pixel) {
          std::memcpy(withAlpha.data() + pixel * outputChannels * sampleBytes,
                      converted.constData() + pixel * 3 * sampleBytes,
                      static_cast<size_t>(3 * sampleBytes));
          std::memcpy(withAlpha.data() + (pixel * outputChannels + 3) * sampleBytes,
                      raw.pixels.constData() + (pixel * sourceChannels + 4) * sampleBytes,
                      static_cast<size_t>(sampleBytes));
        }
        converted = std::move(withAlpha);
      }
      raw.pixels = std::move(converted);
      raw.channels = outputChannels;
      colorConverted = true;
    }
  } else if (directLayout(layout, &channels, &sampleType)) {
    ok = readDirect(tiff.get(), filePath, pageIndex, layout, channels, sampleType, &raw, &error);
  } else if (layout.bitsPerSample <= 8 || layout.photometric == PHOTOMETRIC_YCBCR ||
             layout.photometric == PHOTOMETRIC_SEPARATED ||
             layout.photometric == PHOTOMETRIC_CIELAB || layout.photometric == PHOTOMETRIC_ICCLAB ||
             layout.photometric == PHOTOMETRIC_ITULAB ||
             layout.photometric == PHOTOMETRIC_PALETTE) {
    // 8bit 以下 (パレット、1 / 2 / 4bit を含む) と、色の表し方が違うものは、libtiff の変換を使う。
    ok = readConverted(tiff.get(), layout, &raw, &error);
    // CMYK と Lab は、出力が RGB になるため、元のプロファイル (CMYK 用など) は付けない。
    // パレットと YCbCr は、変換後も RGB のため、プロファイルは使える。
    colorConverted =
        layout.photometric == PHOTOMETRIC_SEPARATED || layout.photometric == PHOTOMETRIC_CIELAB ||
        layout.photometric == PHOTOMETRIC_ICCLAB || layout.photometric == PHOTOMETRIC_ITULAB;
  } else {
    error = QStringLiteral("This TIFF layout is not supported");
  }

  if (!ok) {
    result.raw = rawImageFailure(!log.lastError.isEmpty() ? log.lastError : error);
    return result;
  }
  if (!colorConverted) {
    raw.iccProfile = icc;
  }

  if (applyOrientation && layout.orientation >= 2 && layout.orientation <= 8) {
    applyOrientationToRawImage(&raw, layout.orientation);
    result.appliedOrientation = layout.orientation;
  }
  raw.ok = true;
  return result;
}

}  // namespace heiran::infra
