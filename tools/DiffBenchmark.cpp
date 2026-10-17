// 差分の計測用。輝度の差 (いまの差分) と、ΔE2000 の計算の費用を、同じ条件で並べる。
//
//   heiran_diff_benchmark [幅 高さ [反復]]
//
// 合成した 8bit の画像で測る。`differing` は、2 枚で値が違う画素の割合 (ΔE は、同じ画素を飛ばす)。

#include "heiran/domain/ColorDifference.hpp"
#include "heiran/domain/Ssim.hpp"
#include "heiran/infra/ParallelRows.hpp"
#include "heiran/ui/DiffRenderer.hpp"
#include "heiran/ui/ScopeBuilder.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QRandomGenerator>
#include <QTextStream>
#include <algorithm>
#include <vector>

namespace {

heiran::ui::DiffInput makeInput(const QImage &image) {
  heiran::ui::AnalysisSampler sampler;
  sampler.decodedImage = {.displayImage = image};
  sampler.sourceSize = image.size();
  return {.image = image, .sampler = sampler, .origin = QPoint(0, 0)};
}

QImage makeImage(QSize size, quint32 seed, double differing, const QImage *reference) {
  QImage image(size, QImage::Format_RGBA8888);
  QRandomGenerator random(seed);
  for (int y = 0; y < size.height(); ++y) {
    auto *row = reinterpret_cast<uchar *>(image.scanLine(y));
    for (int x = 0; x < size.width(); ++x) {
      const int base = (x * 255 / size.width() + y * 255 / size.height()) / 2;
      uchar *pixel = row + x * 4;
      pixel[0] = static_cast<uchar>(base);
      pixel[1] = static_cast<uchar>(255 - base);
      pixel[2] = static_cast<uchar>((base * 3) & 255);
      pixel[3] = 255;
      if (reference != nullptr && random.generateDouble() < differing) {
        pixel[0] = static_cast<uchar>(std::clamp<int>(pixel[0] + random.bounded(21) - 10, 0, 255));
        pixel[1] = static_cast<uchar>(std::clamp<int>(pixel[1] + random.bounded(21) - 10, 0, 255));
        pixel[2] = static_cast<uchar>(std::clamp<int>(pixel[2] + random.bounded(21) - 10, 0, 255));
      }
    }
  }
  return image;
}

template <typename Function>
double medianMilliseconds(int iterations, Function &&function) {
  std::vector<double> times;
  for (int index = 0; index < iterations + 1; ++index) {
    QElapsedTimer timer;
    timer.start();
    function();
    if (index > 0) {  // 初回は温めるため外す。
      times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
  }
  std::sort(times.begin(), times.end());
  return times[times.size() / 2];
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  const QStringList arguments = QCoreApplication::arguments();
  const int width = arguments.size() >= 3 ? arguments.at(1).toInt() : 3840;
  const int height = arguments.size() >= 3 ? arguments.at(2).toInt() : 2160;
  const int iterations = arguments.size() >= 4 ? std::max(1, arguments.at(3).toInt()) : 5;
  const QSize size(width, height);
  out << "size " << width << "x" << height << ", median of " << iterations << " runs\n";

  const heiran::domain::Matrix3 matrix =
      heiran::domain::rgbToXyzMatrix(heiran::domain::ColorPrimaries::SRgb);

  for (const double differing : {1.0, 0.1}) {
    const QImage baseImage = makeImage(size, 1, 0.0, nullptr);
    // 2 枚目は、1 枚目と同じ絵に、一部の画素へ小さな差を足す。
    QImage targetImage = makeImage(size, 2, differing, &baseImage);
    const auto base = makeInput(baseImage);
    const auto target = makeInput(targetImage);
    out << "\ndiffering pixels: " << differing * 100 << " %\n";

    heiran::domain::DiffSettings settings;
    out << "  luma diff (renderDiff, grayscale):   "
        << medianMilliseconds(iterations, [&] { heiran::ui::renderDiff(base, target, settings); })
        << " ms\n";

    heiran::domain::DiffSettings ssimSettings;
    ssimSettings.mode = heiran::domain::DiffMode::Ssim;
    ssimSettings.threshold = 0.9;
    out << "  SSIM end to end (renderDiff):        "
        << medianMilliseconds(iterations, [&] { heiran::ui::renderDiff(base, target, ssimSettings); })
        << " ms\n";
    {
      // 倍率、見せ方、閾値だけが変わったとき: 取っておいた場から、絵と指標だけを求め直す。
      QString error;
      const auto lumaField = heiran::ui::computeDiffField(base, target, heiran::domain::DiffSettings{}, &error);
      heiran::domain::DiffSettings changed;
      changed.gain = 3.0;
      changed.view = heiran::domain::DiffViewMode::Overlay;
      out << "  luma field only:                     "
          << medianMilliseconds(iterations,
                                [&] {
                                  heiran::ui::computeDiffField(base, target, heiran::domain::DiffSettings{}, &error);
                                })
          << " ms\n";
      out << "  re-render from the field (overlay):  "
          << medianMilliseconds(iterations, [&] { heiran::ui::renderDiffFromField(*lumaField, base, changed); })
          << " ms\n";
      changed.view = heiran::domain::DiffViewMode::Grayscale;
      out << "  re-render from the field (gray):     "
          << medianMilliseconds(iterations, [&] { heiran::ui::renderDiffFromField(*lumaField, base, changed); })
          << " ms\n";
      heiran::domain::DiffSettings deltaEOnly;
      deltaEOnly.mode = heiran::domain::DiffMode::DeltaE;
      const auto deltaEField = heiran::ui::computeDiffField(base, target, deltaEOnly, &error);
      out << "  deltaE field only:                   "
          << medianMilliseconds(iterations,
                                [&] { heiran::ui::computeDiffField(base, target, deltaEOnly, &error); })
          << " ms\n";
      out << "  deltaE re-render from the field:     "
          << medianMilliseconds(iterations, [&] { heiran::ui::renderDiffFromField(*deltaEField, base, deltaEOnly); })
          << " ms\n";
    }
    heiran::domain::DiffSettings deltaESettings;
    deltaESettings.mode = heiran::domain::DiffMode::DeltaE;
    out << "  deltaE end to end (renderDiff):      "
        << medianMilliseconds(iterations, [&] { heiran::ui::renderDiff(base, target, deltaESettings); })
        << " ms\n";

    {
      heiran::ui::AnalysisImage scopeInput;
      scopeInput.label = "bench";
      scopeInput.image = baseImage;
      scopeInput.sampler = base.sampler;
      out << "  waveform (build, all 4 planes):      "
          << medianMilliseconds(iterations, [&] { heiran::ui::buildWaveform(scopeInput); })
          << " ms\n";
      out << "  vectorscope (build):                 "
          << medianMilliseconds(iterations, [&] { heiran::ui::buildVectorscope(scopeInput); })
          << " ms\n";
      const auto waveform = heiran::ui::buildWaveform(scopeInput);
      out << "  waveform image (render, parade):     "
          << medianMilliseconds(iterations,
                                [&] {
                                  heiran::ui::renderWaveformImage(
                                      *waveform, heiran::ui::WaveformMode::Parade, 1.0);
                                })
          << " ms\n";
    }

    QVector<float> planes[6];
    out << "  6 planes, one channel at a time:     "
        << medianMilliseconds(iterations,
                              [&] {
                                const QRect region(QPoint(0, 0), size);
                                for (int channel = 0; channel < 3; ++channel) {
                                  planes[channel] = base.sampler.buildChannelPlane(region, channel);
                                  planes[3 + channel] =
                                      target.sampler.buildChannelPlane(region, channel);
                                }
                              })
        << " ms\n";
    out << "  6 planes, one pass per image:        "
        << medianMilliseconds(iterations,
                              [&] {
                                const QRect region(QPoint(0, 0), size);
                                QVector<float> baseRgb[3];
                                QVector<float> targetRgb[3];
                                base.sampler.buildRgbPlanes(region, baseRgb);
                                target.sampler.buildRgbPlanes(region, targetRgb);
                                for (int channel = 0; channel < 3; ++channel) {
                                  planes[channel] = baseRgb[channel];
                                  planes[3 + channel] = targetRgb[channel];
                                }
                              })
        << " ms\n";

    std::vector<float> deltaE(static_cast<size_t>(width) * height);
    out << "  deltaE2000 (parallel, planes ready): "
        << medianMilliseconds(
               iterations,
               [&] {
                 heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
                   const size_t offset = static_cast<size_t>(firstRow) * width;
                   heiran::domain::deltaE2000Plane(
                       matrix, planes[0].constData() + offset, planes[1].constData() + offset,
                       planes[2].constData() + offset, planes[3].constData() + offset,
                       planes[4].constData() + offset, planes[5].constData() + offset,
                       static_cast<size_t>(lastRow - firstRow) * width, deltaE.data() + offset);
                 });
               })
        << " ms\n";
    QVector<float> lumaBase;
    QVector<float> lumaTarget;
    out << "  2 luma planes:                       "
        << medianMilliseconds(iterations,
                              [&] {
                                const QRect region(QPoint(0, 0), size);
                                lumaBase = base.sampler.buildLumaPlane(region);
                                lumaTarget = target.sampler.buildLumaPlane(region);
                              })
        << " ms\n";
    std::vector<float> ssimMap(static_cast<size_t>(width) * height);
    out << "  SSIM (parallel, planes ready):       "
        << medianMilliseconds(
               iterations,
               [&] {
                 heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
                   heiran::domain::computeSsimRows(lumaBase.constData(), lumaTarget.constData(),
                                                   width, height, firstRow, lastRow, 255.0,
                                                   ssimMap.data());
                 });
               })
        << " ms\n";
    out << "  summary of SSIM:                     "
        << medianMilliseconds(iterations,
                              [&] {
                                heiran::domain::computeSsimMetrics(ssimMap.data(), width, height,
                                                                   0.9);
                              })
        << " ms\n";
    out << "  summary of deltaE:                   "
        << medianMilliseconds(iterations,
                              [&] {
                                heiran::domain::computeColorDifferenceMetrics(deltaE.data(),
                                                                              deltaE.size(), 1.0);
                              })
        << " ms\n";
  }
  return 0;
}
