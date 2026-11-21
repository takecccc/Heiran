#include "heiran/ui/AnalysisResultCache.hpp"

namespace heiran::ui {

AnalysisSourceKey makeAnalysisSourceKey(const AnalysisImage &analysisImage) {
  const auto &decodedImage = analysisImage.sampler.decodedImage;
  const auto &orientation = analysisImage.sampler.orientation;
  const bool linear = analysisImage.sampler.valueSpace == AnalysisValueSpace::Linear;
  const auto *storage = decodedImage.analysisBuffer.storage.data();

  return {
      .imageCacheKey = decodedImage.displayImage.cacheKey(),
      .analysisBufferAddress = reinterpret_cast<quintptr>(storage),
      .analysisBufferBytes = decodedImage.analysisBuffer.byteCount(),
      .rotationQuarterTurns = orientation.rotationQuarterTurns,
      .flipX = orientation.flipX,
      .flipY = orientation.flipY,
      .valueSpace = analysisImage.sampler.valueSpace,
      .sourceTransfer = linear ? analysisImage.sampler.sourceTransfer : ShaderTransfer::Linear,
      .sourceGamma = linear ? analysisImage.sampler.sourceGamma : 1.0F,
      .selectionRect = analysisImage.selectionRect,
  };
}

}  // namespace heiran::ui
