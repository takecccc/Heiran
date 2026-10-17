#include "heiran/domain/ViewState.hpp"

#include <algorithm>

namespace heiran::domain {

FinalViewState computeFinalViewState(const GlobalViewState &globalState,
                                     const CellViewState &cellState) {
  const double cellScale = cellState.scale > 0.0 ? cellState.scale : 1.0;
  return {
      .scale = globalState.baseScale * cellScale,
      .offsetX = (globalState.baseOffsetX + cellState.offsetDeltaX) / cellScale,
      .offsetY = (globalState.baseOffsetY + cellState.offsetDeltaY) / cellScale,
  };
}

double imageToViewX(const FinalViewState &finalState, double imageX) {
  return (imageX - finalState.offsetX) * finalState.scale;
}

double imageToViewY(const FinalViewState &finalState, double imageY) {
  return (imageY - finalState.offsetY) * finalState.scale;
}

double viewToImageX(const FinalViewState &finalState, double viewX) {
  if (finalState.scale <= 0.0) {
    return finalState.offsetX;
  }
  return viewX / finalState.scale + finalState.offsetX;
}

double viewToImageY(const FinalViewState &finalState, double viewY) {
  if (finalState.scale <= 0.0) {
    return finalState.offsetY;
  }
  return viewY / finalState.scale + finalState.offsetY;
}

double mapImageXBetweenCells(const FinalViewState &referenceState,
                             const FinalViewState &targetState, double imageX) {
  if (referenceState.scale <= 0.0 || targetState.scale <= 0.0) {
    return imageX;
  }
  return viewToImageX(targetState, imageToViewX(referenceState, imageX));
}

double mapImageYBetweenCells(const FinalViewState &referenceState,
                             const FinalViewState &targetState, double imageY) {
  if (referenceState.scale <= 0.0 || targetState.scale <= 0.0) {
    return imageY;
  }
  return viewToImageY(targetState, imageToViewY(referenceState, imageY));
}

FitViewResult computeFitView(const std::vector<FitTarget> &targets, int cellWidth, int cellHeight) {
  if (cellWidth <= 0 || cellHeight <= 0) {
    return {};
  }

  FitViewResult result;
  const FitTarget *reference = nullptr;
  for (const auto &target : targets) {
    if (target.imageWidth <= 0 || target.imageHeight <= 0 || target.cellScale <= 0.0) {
      continue;
    }

    const double displayWidth = static_cast<double>(target.imageWidth) * target.cellScale;
    const double displayHeight = static_cast<double>(target.imageHeight) * target.cellScale;
    const double fitScale = std::min(static_cast<double>(cellWidth) / displayWidth,
                                     static_cast<double>(cellHeight) / displayHeight);
    if (!result.valid || fitScale < result.baseScale) {
      result.valid = true;
      result.baseScale = fitScale;
      reference = &target;
    }
  }

  if (!result.valid || reference == nullptr) {
    return {};
  }

  // 基準にした対象を中央へ置く。他の対象はこれより小さく収まる。
  const double referenceScale = result.baseScale * reference->cellScale;
  result.baseOffsetX = static_cast<double>(reference->imageWidth) / 2.0 -
                       static_cast<double>(cellWidth) / (2.0 * referenceScale);
  result.baseOffsetY = static_cast<double>(reference->imageHeight) / 2.0 -
                       static_cast<double>(cellHeight) / (2.0 * referenceScale);
  return result;
}

void applyPanToGlobal(GlobalViewState *globalState, double deltaX, double deltaY) {
  globalState->baseOffsetX += deltaX;
  globalState->baseOffsetY += deltaY;
}

void applyPanToCell(CellViewState *cellState, double deltaX, double deltaY) {
  cellState->offsetDeltaX += deltaX;
  cellState->offsetDeltaY += deltaY;
}

void applyZoom(GlobalViewState *globalState, double zoomFactor, double anchorImageX,
               double anchorImageY) {
  if (zoomFactor <= 0.0) {
    return;
  }

  const double newScale = globalState->baseScale * zoomFactor;
  globalState->baseOffsetX =
      anchorImageX - ((anchorImageX - globalState->baseOffsetX) / zoomFactor);
  globalState->baseOffsetY =
      anchorImageY - ((anchorImageY - globalState->baseOffsetY) / zoomFactor);
  globalState->baseScale = newScale;
}

}  // namespace heiran::domain
