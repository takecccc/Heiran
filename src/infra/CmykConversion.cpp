#include "heiran/infra/CmykConversion.hpp"

#include "heiran/infra/ParallelRows.hpp"

#if defined(HEIRAN_HAVE_LCMS2)
#include <lcms2.h>
#endif

#include <cstring>
#include <memory>

namespace heiran::infra {

namespace {

// プロファイルを使わない、素朴な変換。標本ごとに、反転の有無を揃えてから掛ける。
void convertNaive(const uchar *cmyk, int width, int height, int sampleBytes, int sourceChannels,
                  bool inkReversed, uchar *rgb) {
  const int maximum = sampleBytes == 1 ? 255 : 65535;
  forEachRowBand(height, [&](int firstRow, int lastRow) {
    for (qsizetype pixel = static_cast<qsizetype>(firstRow) * width;
         pixel < static_cast<qsizetype>(lastRow) * width; ++pixel) {
      const auto sampleAt = [&](int channel) {
        int value = 0;
        if (sampleBytes == 1) {
          value = cmyk[pixel * sourceChannels + channel];
        } else {
          quint16 word = 0;
          std::memcpy(&word, cmyk + (pixel * sourceChannels + channel) * 2, sizeof(word));
          value = word;
        }
        return inkReversed ? value : maximum - value;  // 「インク無しが最大」へ揃える。
      };
      const int key = sampleAt(3);
      for (int channel = 0; channel < 3; ++channel) {
        const int value = (sampleAt(channel) * key + maximum / 2) / maximum;
        if (sampleBytes == 1) {
          rgb[pixel * 3 + channel] = static_cast<uchar>(value);
        } else {
          const quint16 word = static_cast<quint16>(value);
          std::memcpy(rgb + (pixel * 3 + channel) * 2, &word, sizeof(word));
        }
      }
    }
  });
}

#if defined(HEIRAN_HAVE_LCMS2)

struct ProfileDeleter {
  void operator()(void *profile) const { cmsCloseProfile(static_cast<cmsHPROFILE>(profile)); }
};
struct TransformDeleter {
  void operator()(void *transform) const {
    cmsDeleteTransform(static_cast<cmsHTRANSFORM>(transform));
  }
};

bool convertWithProfile(const QByteArray &iccProfile, const uchar *cmyk, int width, int height,
                        int sampleBytes, int sourceChannels, bool inkReversed, uchar *rgb) {
  if (iccProfile.isEmpty()) {
    return false;
  }
  std::unique_ptr<void, ProfileDeleter> input(cmsOpenProfileFromMem(
      iccProfile.constData(), static_cast<cmsUInt32Number>(iccProfile.size())));
  if (!input || cmsGetColorSpace(static_cast<cmsHPROFILE>(input.get())) != cmsSigCmykData) {
    return false;
  }
  std::unique_ptr<void, ProfileDeleter> output(cmsCreate_sRGBProfile());
  if (!output) {
    return false;
  }

  const cmsUInt32Number inputFormat =
      (sampleBytes == 1 ? (inkReversed ? TYPE_CMYK_8_REV : TYPE_CMYK_8)
                        : (inkReversed ? TYPE_CMYK_16_REV : TYPE_CMYK_16)) |
      EXTRA_SH(sourceChannels - 4);
  const cmsUInt32Number outputFormat = sampleBytes == 1 ? TYPE_RGB_8 : TYPE_RGB_16;
  // 複数のスレッドで同じ変換を使うため、1 画素の記憶 (キャッシュ) は切る。
  std::unique_ptr<void, TransformDeleter> transform(cmsCreateTransform(
      static_cast<cmsHPROFILE>(input.get()), inputFormat, static_cast<cmsHPROFILE>(output.get()),
      outputFormat, INTENT_RELATIVE_COLORIMETRIC,
      cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE));
  if (!transform) {
    return false;
  }

  const qsizetype inputRow = static_cast<qsizetype>(width) * sourceChannels * sampleBytes;
  const qsizetype outputRow = static_cast<qsizetype>(width) * 3 * sampleBytes;
  forEachRowBand(height, [&](int firstRow, int lastRow) {
    for (int row = firstRow; row < lastRow; ++row) {
      cmsDoTransform(static_cast<cmsHTRANSFORM>(transform.get()), cmyk + row * inputRow,
                     rgb + row * outputRow, static_cast<cmsUInt32Number>(width));
    }
  });
  return true;
}

#endif

}  // namespace

bool convertCmykToSrgb(const QByteArray &iccProfile, const uchar *cmyk, int width, int height,
                       int sampleBytes, int sourceChannels, bool inkReversed, uchar *rgb) {
#if defined(HEIRAN_HAVE_LCMS2)
  if (convertWithProfile(iccProfile, cmyk, width, height, sampleBytes, sourceChannels, inkReversed,
                         rgb)) {
    return true;
  }
#else
  Q_UNUSED(iccProfile);
#endif
  convertNaive(cmyk, width, height, sampleBytes, sourceChannels, inkReversed, rgb);
  return false;
}

}  // namespace heiran::infra
