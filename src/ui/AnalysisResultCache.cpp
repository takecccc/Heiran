#include "heiran/ui/AnalysisResultCache.hpp"

namespace heiran::ui {

AnalysisSourceKey makeAnalysisSourceKey(const AnalysisImage &analysisImage) {
  const auto &decodedImage = analysisImage.sampler.decodedImage;
  const auto &orientation = analysisImage.sampler.orientation;
  const auto *storage = decodedImage.analysisBuffer.storage.data();

  return {
      .imageCacheKey = decodedImage.displayImage.cacheKey(),
      .analysisBufferAddress = reinterpret_cast<quintptr>(storage),
      .analysisBufferBytes = decodedImage.analysisBuffer.byteCount(),
      .rotationQuarterTurns = orientation.rotationQuarterTurns,
      .flipX = orientation.flipX,
      .flipY = orientation.flipY,
      .selectionRect = analysisImage.selectionRect.value_or(QRect()),
  };
}

}  // namespace heiran::ui
